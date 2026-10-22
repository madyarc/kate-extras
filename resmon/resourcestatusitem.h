#pragma once

#include <QObject>

class QSystemTrayIcon;
class ResourceSampler;

// The meter as a menu bar extra, for when the window's menu bar is the global native one
// (always on macOS) and so cannot hold a corner widget. Click it for the full details.
// macOS gets a native status item (resourcestatusitem_macos.mm): a tray icon there is
// squeezed into a square slot, which shrinks a wide meter to an unreadable size.
class ResourceStatusItem : public QObject
{
    Q_OBJECT

public:
    explicit ResourceStatusItem(ResourceSampler *sampler, QObject *parent = nullptr);
    ~ResourceStatusItem() override;

private:
    void update();

    ResourceSampler *m_sampler;
#ifdef Q_OS_MACOS
    void *m_statusItem; // NSStatusItem
#else
    void showDetails();

    QSystemTrayIcon *m_trayIcon;
#endif
};
