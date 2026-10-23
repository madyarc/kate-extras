#pragma once

#include <QJsonObject>
#include <QObject>
#include <QProcess>

// A Jupyter kernel, run through kernelbridge.py with the notebook's Python interpreter.
// Requests are queued by the caller; this class sends them as they come.
class Kernel : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Stopped,
        Starting,
        Ready,
        Failed, // could not start; see problem()
    };

    explicit Kernel(QObject *parent = nullptr);
    ~Kernel() override;

    // The nearest virtualenv above the notebook (venv, .venv, env), else a python3 on
    // the usual install locations; GUI apps on macOS do not inherit the shell's PATH
    static QString findPython(const QString &notebookDir);

    void start(const QString &python, const QString &kernelName, const QString &workingDir);
    void execute(const QString &requestId, const QString &code);
    void interrupt();
    void restart();
    void shutdown();

    State state() const;
    QString displayName() const;

Q_SIGNALS:
    void stateChanged(Kernel::State state);
    void output(const QString &requestId, const QJsonObject &output);
    void clearOutput(const QString &requestId, bool wait);
    // status is "ok", "error" or "aborted" (interrupted by a restart or a dead kernel)
    void finished(const QString &requestId, const QJsonValue &executionCount, const QString &status);
    void restarted();
    void died();
    // Something the user should know about, already phrased for them
    void problem(const QString &message, bool missingJupyter);

private:
    void send(const QJsonObject &command);
    void readEvents();
    void handleEvent(const QJsonObject &event);
    void onProcessFinished();
    void setState(State state);

    QProcess *m_process;
    QByteArray m_buffer;
    QByteArray m_stderr;
    QString m_python;
    QString m_displayName;
    State m_state = State::Stopped;
    bool m_reportedFatal = false;
};
