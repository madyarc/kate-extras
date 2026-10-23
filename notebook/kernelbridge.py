"""Runs a Jupyter kernel for Kate's notebook plugin.

Kate starts this with the notebook's Python interpreter:
    python -I -u -c <this file> <kernel name> <working directory>

Commands arrive on stdin and events leave on stdout, one JSON object per line.
    commands: {"cmd": "execute", "id": ..., "code": ...}, {"cmd": "interrupt"},
              {"cmd": "restart"}, {"cmd": "shutdown"}
    events:   ready, notice, output, clear, done, restarted, died, fatal
Outputs are already in nbformat shape, so Kate can store them as they are.
"""

import json
import os
import queue
import sys
import threading

# The kernel and the libraries it loads may write to file descriptor 1 at any time.
# Keep a private copy of it for the protocol and send everything else to stderr.
_protocol = os.fdopen(os.dup(1), "w", buffering=1, encoding="utf-8")
os.dup2(2, 1)
sys.stdout = sys.stderr
_protocol_lock = threading.Lock()


def emit(event, **fields):
    line = json.dumps(dict(fields, event=event), default=str)
    with _protocol_lock:
        _protocol.write(line + "\n")
        _protocol.flush()


try:
    from jupyter_client.kernelspec import NoSuchKernel
    from jupyter_client.manager import KernelManager
except ImportError:
    emit("fatal", reason="missing-jupyter")
    sys.exit(1)


def to_output(msg):
    """An IOPub message as an nbformat output, or None if it is not an output."""
    kind = msg["msg_type"]
    content = msg["content"]
    if kind == "stream":
        return {"output_type": "stream", "name": content["name"], "text": content["text"]}
    if kind in ("display_data", "execute_result"):
        output = {"output_type": kind, "data": content.get("data", {}), "metadata": content.get("metadata", {})}
        if kind == "execute_result":
            output["execution_count"] = content.get("execution_count")
        return output
    if kind == "error":
        return {
            "output_type": "error",
            "ename": content.get("ename", ""),
            "evalue": content.get("evalue", ""),
            "traceback": content.get("traceback", []),
        }
    return None


class Bridge:
    def __init__(self, kernel_name, cwd):
        self.jobs = queue.Queue()
        # Bumped on restart; requests from an older generation are abandoned
        self.generation = 0

        self.manager = self.make_manager(kernel_name)
        try:
            spec = self.manager.kernel_spec
        except NoSuchKernel:
            emit("notice", reason="no-such-kernel", kernel=kernel_name)
            self.manager = self.make_manager("python3")
            spec = self.manager.kernel_spec

        self.manager.start_kernel(cwd=cwd)
        self.client = self.manager.client()
        self.client.start_channels()
        self.client.wait_for_ready(timeout=120)

        threading.Thread(target=self.work, daemon=True).start()
        emit("ready", name=spec.display_name, language=spec.language)

    @staticmethod
    def make_manager(kernel_name):
        if os.name == "nt":
            return KernelManager(kernel_name=kernel_name)
        # Unix sockets in the user's private runtime directory instead of TCP ports
        # that any local user could connect to. The path must be absolute: the kernel
        # runs in the notebook's directory, this bridge does not.
        from jupyter_core.paths import jupyter_runtime_dir

        runtime = jupyter_runtime_dir()
        os.makedirs(runtime, mode=0o700, exist_ok=True)
        return KernelManager(kernel_name=kernel_name, transport="ipc", ip=os.path.join(runtime, f"kate-{os.getpid()}"))

    def work(self):
        while True:
            job = self.jobs.get()
            if job is None:
                return
            try:
                self.run(job)
            except Exception as error:  # never let one request take the bridge down
                emit("output", id=job["id"], output={"output_type": "stream", "name": "stderr", "text": f"Kate notebook bridge: {error}\n"})
                emit("done", id=job["id"], execution_count=None, status="error")

    def run(self, job):
        cell = job["id"]
        if job["generation"] != self.generation:
            emit("done", id=cell, execution_count=None, status="aborted")
            return

        msg_id = self.client.execute(job["code"], allow_stdin=False, stop_on_error=False)

        # Outputs until the kernel reports idle for this request
        while True:
            if job["generation"] != self.generation:
                emit("done", id=cell, execution_count=None, status="aborted")
                return
            try:
                msg = self.client.get_iopub_msg(timeout=0.2)
            except queue.Empty:
                if not self.manager.is_alive():
                    emit("died")
                    emit("done", id=cell, execution_count=None, status="aborted")
                    return
                continue
            if msg["parent_header"].get("msg_id") != msg_id:
                continue
            kind = msg["msg_type"]
            if kind == "status":
                if msg["content"]["execution_state"] == "idle":
                    break
            elif kind == "clear_output":
                emit("clear", id=cell, wait=msg["content"].get("wait", False))
            else:
                output = to_output(msg)
                if output is not None:
                    emit("output", id=cell, output=output)

        count, status = None, "ok"
        while True:
            try:
                reply = self.client.get_shell_msg(timeout=10)
            except queue.Empty:
                break
            if reply["parent_header"].get("msg_id") == msg_id:
                count = reply["content"].get("execution_count")
                status = reply["content"].get("status", "ok")
                break
        emit("done", id=cell, execution_count=count, status=status)

    def serve(self):
        for line in sys.stdin:
            command = json.loads(line)
            kind = command.get("cmd")
            if kind == "execute":
                command["generation"] = self.generation
                self.jobs.put(command)
            elif kind == "interrupt":
                self.manager.interrupt_kernel()
            elif kind == "restart":
                self.generation += 1
                self.manager.restart_kernel(now=True)
                self.client.wait_for_ready(timeout=120)
                emit("restarted")
            elif kind == "shutdown":
                break

    def close(self):
        self.jobs.put(None)
        self.client.stop_channels()
        self.manager.shutdown_kernel(now=True)


def main():
    kernel_name, cwd = sys.argv[1], sys.argv[2]
    try:
        bridge = Bridge(kernel_name, cwd)
    except Exception as error:
        emit("fatal", reason="start-failed", message=str(error))
        sys.exit(1)
    try:
        bridge.serve()
    finally:
        bridge.close()


main()
