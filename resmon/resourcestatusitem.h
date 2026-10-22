#pragma once

#include <QObject>

class QSystemTrayIcon;
class ResourceSampler;

// The meter as a menu bar extra, for when the window's menu bar is the global native one
// (always on macOS) and so cannot hold a corner widget. Click it for the full details.
class ResourceStatusItem : public QObject
{
    Q_OBJECT

public:
    explicit ResourceStatusItem(ResourceSampler *sampler, QObject *parent = nullptr);

private:
    void update();
    void showDetails();

    ResourceSampler *m_sampler;
    QSystemTrayIcon *m_trayIcon;
};
