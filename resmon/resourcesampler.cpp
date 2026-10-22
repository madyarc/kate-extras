#include "resourcesampler.h"

#include <QDir>
#include <QStorageInfo>
#include <QTimer>

namespace
{
constexpr int s_intervalMs = 2000;
}

ResourceSampler::ResourceSampler(QObject *parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
{
    detectGpu();

    m_timer->setInterval(s_intervalMs);
    connect(m_timer, &QTimer::timeout, this, &ResourceSampler::sample);
    m_timer->start();
    sample();
}

ResourceSnapshot ResourceSampler::snapshot() const
{
    return m_snapshot;
}

void ResourceSampler::setPaused(bool paused)
{
    if (paused) {
        m_timer->stop();
    } else if (!m_timer->isActive()) {
        m_timer->start();
        sample();
    }
}

void ResourceSampler::sample()
{
    sampleMemory();
    sampleDisk();
    sampleGpu();
    Q_EMIT updated();
}

void ResourceSampler::sampleDisk()
{
    const QStorageInfo storage(QDir::homePath());
    m_snapshot.diskTotal = storage.bytesTotal();
    m_snapshot.diskUsed = storage.bytesTotal() - storage.bytesFree();
}
