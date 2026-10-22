#include "resourcesampler.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>

namespace
{
QString readSysfs(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromLatin1(file.readAll()).trimmed();
}
}

void ResourceSampler::detectGpu()
{
    const QDir drm(QStringLiteral("/sys/class/drm"));
    const QStringList cards = drm.entryList({QStringLiteral("card*")}, QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &card : cards) {
        // Skip connector entries such as card0-HDMI-A-1
        if (card.contains(QLatin1Char('-'))) {
            continue;
        }
        const QString device = drm.filePath(card) + QStringLiteral("/device");
        const QString vendor = readSysfs(device + QStringLiteral("/vendor"));

        if (vendor == QLatin1String("0x10de") && m_nvidiaPowerStatusPath.isEmpty()) {
            m_nvidiaSmi = QStandardPaths::findExecutable(QStringLiteral("nvidia-smi"));
            if (!m_nvidiaSmi.isEmpty()) {
                m_nvidiaPowerStatusPath = device + QStringLiteral("/power/runtime_status");
            }
        } else if (vendor == QLatin1String("0x1002") && m_amdDevicePath.isEmpty() && QFile::exists(device + QStringLiteral("/mem_info_vram_total"))) {
            m_amdDevicePath = device;
        }
    }

    if (!m_nvidiaSmi.isEmpty()) {
        m_nvidiaProcess = new QProcess(this);
        connect(m_nvidiaProcess, &QProcess::finished, this, &ResourceSampler::onNvidiaSmiFinished);
        connect(m_nvidiaProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                m_snapshot.gpuState = ResourceSnapshot::GpuState::Error;
                Q_EMIT updated();
            }
        });
    }
}

void ResourceSampler::sampleMemory()
{
    QFile file(QStringLiteral("/proc/meminfo"));
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    qint64 total = 0, available = 0, swapTotal = 0, swapFree = 0;
    const QList<QByteArray> lines = file.readAll().split('\n');
    for (const QByteArray &line : lines) {
        const QList<QByteArray> fields = line.simplified().split(' ');
        if (fields.size() < 2) {
            continue;
        }
        const qint64 bytes = fields.at(1).toLongLong() * 1024;
        if (fields.at(0) == "MemTotal:") {
            total = bytes;
        } else if (fields.at(0) == "MemAvailable:") {
            available = bytes;
        } else if (fields.at(0) == "SwapTotal:") {
            swapTotal = bytes;
        } else if (fields.at(0) == "SwapFree:") {
            swapFree = bytes;
        }
    }

    // Same notion of "used" as Colab and `free`: memory that is not reclaimable
    m_snapshot.ramTotal = total;
    m_snapshot.ramUsed = total - available;
    m_snapshot.swapTotal = swapTotal;
    m_snapshot.swapUsed = swapTotal - swapFree;
}

void ResourceSampler::sampleGpu()
{
    if (!m_nvidiaPowerStatusPath.isEmpty()) {
        // Querying a runtime-suspended GPU would wake it up and drain the battery
        if (readSysfs(m_nvidiaPowerStatusPath) == QLatin1String("suspended")) {
            m_snapshot.gpuState = ResourceSnapshot::GpuState::Asleep;
            return;
        }
        if (m_nvidiaProcess->state() == QProcess::NotRunning) {
            m_nvidiaProcess->start(m_nvidiaSmi,
                                   {QStringLiteral("--query-gpu=name,memory.used,memory.total,utilization.gpu,temperature.gpu"),
                                    QStringLiteral("--format=csv,noheader,nounits"),
                                    QStringLiteral("--id=0")});
        }
        return;
    }

    if (!m_amdDevicePath.isEmpty()) {
        m_snapshot.gpuState = ResourceSnapshot::GpuState::Ok;
        m_snapshot.gpuName = QStringLiteral("AMD GPU");
        m_snapshot.vramUsed = readSysfs(m_amdDevicePath + QStringLiteral("/mem_info_vram_used")).toLongLong();
        m_snapshot.vramTotal = readSysfs(m_amdDevicePath + QStringLiteral("/mem_info_vram_total")).toLongLong();
        const QString busy = readSysfs(m_amdDevicePath + QStringLiteral("/gpu_busy_percent"));
        m_snapshot.gpuUtilization = busy.isEmpty() ? -1 : busy.toInt();
    }
}

void ResourceSampler::onNvidiaSmiFinished()
{
    const QString output = QString::fromUtf8(m_nvidiaProcess->readAllStandardOutput()).trimmed();
    const QStringList fields = output.section(QLatin1Char('\n'), 0, 0).split(QStringLiteral(", "));

    if (m_nvidiaProcess->exitCode() != 0 || fields.size() < 5) {
        m_snapshot.gpuState = ResourceSnapshot::GpuState::Error;
    } else {
        constexpr qint64 mebibyte = 1024 * 1024;
        m_snapshot.gpuState = ResourceSnapshot::GpuState::Ok;
        m_snapshot.gpuName = fields.at(0);
        m_snapshot.vramUsed = fields.at(1).toLongLong() * mebibyte;
        m_snapshot.vramTotal = fields.at(2).toLongLong() * mebibyte;
        bool ok = false;
        m_snapshot.gpuUtilization = fields.at(3).toInt(&ok);
        if (!ok) {
            m_snapshot.gpuUtilization = -1;
        }
        m_snapshot.gpuTemperature = fields.at(4).toInt(&ok);
        if (!ok) {
            m_snapshot.gpuTemperature = -1;
        }
    }
    Q_EMIT updated();
}
