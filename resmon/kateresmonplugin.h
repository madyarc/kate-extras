#pragma once

#include <KTextEditor/Plugin>

#include <QPointer>
#include <QVariantList>

namespace KTextEditor
{
class MainWindow;
}

class ResourceMeter;
class ResourceSampler;
class ResourceStatusItem;

class KateResMonPlugin : public KTextEditor::Plugin
{
    Q_OBJECT

public:
    explicit KateResMonPlugin(QObject *parent, const QVariantList & = QVariantList());

    QObject *createView(KTextEditor::MainWindow *mainWindow) override;

    ResourceSampler *sampler() const;
    void showStatusItem();

private:
    // One sampler for all windows so the GPU is only queried once per interval
    ResourceSampler *m_sampler;
    // A single menu bar extra for all windows when the menu bar is the native global one
    ResourceStatusItem *m_statusItem = nullptr;
};

class KateResMonPluginView : public QObject
{
    Q_OBJECT

public:
    KateResMonPluginView(KateResMonPlugin *plugin, KTextEditor::MainWindow *mainWindow);
    ~KateResMonPluginView() override;

private:
    QPointer<ResourceMeter> m_meter;
};
