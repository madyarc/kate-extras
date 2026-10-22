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

class KateResMonPlugin : public KTextEditor::Plugin
{
    Q_OBJECT

public:
    explicit KateResMonPlugin(QObject *parent, const QVariantList & = QVariantList());

    QObject *createView(KTextEditor::MainWindow *mainWindow) override;

private:
    // One sampler for all windows so the GPU is only queried once per interval
    ResourceSampler *m_sampler;
};

class KateResMonPluginView : public QObject
{
    Q_OBJECT

public:
    KateResMonPluginView(ResourceSampler *sampler, KTextEditor::MainWindow *mainWindow);
    ~KateResMonPluginView() override;

private:
    QPointer<ResourceMeter> m_meter;
};
