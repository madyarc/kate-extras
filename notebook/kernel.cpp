#include "kernel.h"

#include <KLocalizedString>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>

namespace
{
// Bytes of the bridge's stderr kept for error messages
constexpr qsizetype s_stderrTail = 4000;

QString bridgeScript()
{
    QFile file(QStringLiteral(":/katenotebook/kernelbridge.py"));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}
}

Kernel::Kernel(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
{
    connect(m_process, &QProcess::readyReadStandardOutput, this, &Kernel::readEvents);
    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        m_stderr += m_process->readAllStandardError();
        if (m_stderr.size() > s_stderrTail) {
            m_stderr = m_stderr.right(s_stderrTail);
        }
    });
    connect(m_process, &QProcess::finished, this, &Kernel::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            setState(State::Failed);
            Q_EMIT problem(i18n("Could not run %1.", m_python), false);
        }
    });
}

Kernel::~Kernel()
{
    shutdown();
}

QString Kernel::findPython(const QString &notebookDir)
{
    static const QStringList envDirs{QStringLiteral(".venv"), QStringLiteral("venv"), QStringLiteral(".env"), QStringLiteral("env")};
    for (QDir dir(notebookDir);;) {
        for (const QString &env : envDirs) {
            const QString python = dir.filePath(env + QStringLiteral("/bin/python"));
            if (QFileInfo(python).isExecutable()) {
                return python;
            }
        }
        if (dir.isRoot() || !dir.cdUp()) {
            break;
        }
    }

    const QString home = QDir::homePath();
    const QStringList extraPaths{
        QStringLiteral("/opt/homebrew/bin"),
        QStringLiteral("/usr/local/bin"),
        home + QStringLiteral("/.local/bin"),
        home + QStringLiteral("/miniforge3/bin"),
        home + QStringLiteral("/miniconda3/bin"),
        home + QStringLiteral("/anaconda3/bin"),
        home + QStringLiteral("/.pyenv/shims"),
    };
    QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (python.isEmpty()) {
        python = QStandardPaths::findExecutable(QStringLiteral("python3"), extraPaths);
    }
    return python;
}

void Kernel::start(const QString &python, const QString &kernelName, const QString &workingDir)
{
    if (m_process->state() != QProcess::NotRunning) {
        return;
    }
    m_python = python;
    m_buffer.clear();
    m_stderr.clear();
    m_reportedFatal = false;
    setState(State::Starting);

    // The environment's own tools (pip, CLIs used with `!`) should be found from cells
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const QString binDir = QFileInfo(python).absolutePath();
    environment.insert(QStringLiteral("PATH"), binDir + QLatin1Char(':') + environment.value(QStringLiteral("PATH")));
    m_process->setProcessEnvironment(environment);
    m_process->setWorkingDirectory(workingDir);

    // -I keeps the notebook's directory and PYTHON* variables from altering the bridge
    m_process->start(python, {QStringLiteral("-I"), QStringLiteral("-u"), QStringLiteral("-c"), bridgeScript(), kernelName, workingDir});
}

void Kernel::execute(const QString &requestId, const QString &code)
{
    send({{QStringLiteral("cmd"), QStringLiteral("execute")}, {QStringLiteral("id"), requestId}, {QStringLiteral("code"), code}});
}

void Kernel::interrupt()
{
    send({{QStringLiteral("cmd"), QStringLiteral("interrupt")}});
}

void Kernel::restart()
{
    send({{QStringLiteral("cmd"), QStringLiteral("restart")}});
}

void Kernel::shutdown()
{
    if (m_process->state() == QProcess::NotRunning) {
        return;
    }
    send({{QStringLiteral("cmd"), QStringLiteral("shutdown")}});
    m_process->closeWriteChannel();
    // The bridge stops the kernel and exits; do not leave orphans behind if it hangs
    if (!m_process->waitForFinished(3000)) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
    setState(State::Stopped);
}

Kernel::State Kernel::state() const
{
    return m_state;
}

QString Kernel::displayName() const
{
    return m_displayName;
}

void Kernel::send(const QJsonObject &command)
{
    if (m_process->state() == QProcess::Running) {
        m_process->write(QJsonDocument(command).toJson(QJsonDocument::Compact) + '\n');
    }
}

void Kernel::readEvents()
{
    m_buffer += m_process->readAllStandardOutput();
    qsizetype newline;
    while ((newline = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);
        const QJsonDocument document = QJsonDocument::fromJson(line);
        if (document.isObject()) {
            handleEvent(document.object());
        }
    }
}

void Kernel::handleEvent(const QJsonObject &event)
{
    const QString type = event.value(QLatin1String("event")).toString();
    const QString id = event.value(QLatin1String("id")).toString();

    if (type == QLatin1String("output")) {
        Q_EMIT output(id, event.value(QLatin1String("output")).toObject());
    } else if (type == QLatin1String("clear")) {
        Q_EMIT clearOutput(id, event.value(QLatin1String("wait")).toBool());
    } else if (type == QLatin1String("done")) {
        Q_EMIT finished(id, event.value(QLatin1String("execution_count")), event.value(QLatin1String("status")).toString());
    } else if (type == QLatin1String("ready")) {
        m_displayName = event.value(QLatin1String("name")).toString();
        setState(State::Ready);
    } else if (type == QLatin1String("restarted")) {
        Q_EMIT restarted();
    } else if (type == QLatin1String("died")) {
        Q_EMIT died();
    } else if (type == QLatin1String("notice")) {
        Q_EMIT problem(i18n("The kernel “%1” is not installed for %2, so the default Python kernel is used.",
                            event.value(QLatin1String("kernel")).toString(),
                            m_python),
                       false);
    } else if (type == QLatin1String("fatal")) {
        m_reportedFatal = true;
        setState(State::Failed);
        if (event.value(QLatin1String("reason")).toString() == QLatin1String("missing-jupyter")) {
            Q_EMIT problem(i18n("%1 cannot run notebooks. Install a kernel for it with “%1 -m pip install ipykernel”, or choose another Python.", m_python),
                           true);
        } else {
            Q_EMIT problem(i18n("The kernel did not start: %1", event.value(QLatin1String("message")).toString()), false);
        }
    }
}

void Kernel::onProcessFinished()
{
    readEvents();
    if (m_state == State::Stopped || m_reportedFatal) {
        return;
    }
    setState(State::Failed);
    const QString details = QString::fromUtf8(m_stderr).trimmed().section(QLatin1Char('\n'), -3);
    Q_EMIT problem(details.isEmpty() ? i18n("The kernel stopped unexpectedly.") : i18n("The kernel stopped unexpectedly: %1", details), false);
}

void Kernel::setState(State state)
{
    if (state != m_state) {
        m_state = state;
        Q_EMIT stateChanged(state);
    }
}
