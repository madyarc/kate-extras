#pragma once

#include <QObject>
#include <QString>

class QProcess;
class QTimer;

struct ResourceSnapshot {
    enum class GpuState {
        None, // no discrete GPU with readable VRAM
        Asleep, // powered down; deliberately not queried
        Ok,
        Error,
    };

    qint64 ramUsed = 0;
    qint64 ramTotal = 0;
    qint64 swapUsed = 0;
    qint64 swapTotal = 0;
    qint64 diskUsed = 0;
    qint64 diskTotal = 0;

    GpuState gpuState = GpuState::None;
    QString gpuName;
    qint64 vramUsed = 0;
    qint64 vramTotal = 0;
    int gpuUtilization = -1;
    int gpuTemperature = -1;
};

// Periodically samples memory, disk and GPU usage; one instance is shared by all Kate windows
class ResourceSampler : public QObject
{
    Q_OBJECT

public:
    explicit ResourceSampler(QObject *parent = nullptr);

    ResourceSnapshot snapshot() const;

Q_SIGNALS:
    void updated();

private:
    void detectGpu();
    void sample();
    void sampleMemory();
    void sampleDisk();
    void sampleGpu();
    void onNvidiaSmiFinished();

    ResourceSnapshot m_snapshot;
    QTimer *m_timer;

    QString m_nvidiaSmi;
    QString m_nvidiaPowerStatusPath;
    QProcess *m_nvidiaProcess = nullptr;
    QString m_amdDevicePath;
};
