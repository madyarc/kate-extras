#include "kateresmonplugin.h"
#include "resourcemeter.h"
#include "resourcesampler.h"

#include <KPluginFactory>
#include <KTextEditor/MainWindow>

#include <QMainWindow>
#include <QMenuBar>

K_PLUGIN_FACTORY_WITH_JSON(KateResMonPluginFactory, "kateresmonplugin.json", registerPlugin<KateResMonPlugin>();)

KateResMonPlugin::KateResMonPlugin(QObject *parent, const QVariantList &)
    : KTextEditor::Plugin(parent)
    , m_sampler(new ResourceSampler(this))
{
}

QObject *KateResMonPlugin::createView(KTextEditor::MainWindow *mainWindow)
{
    return new KateResMonPluginView(m_sampler, mainWindow);
}

KateResMonPluginView::KateResMonPluginView(ResourceSampler *sampler, KTextEditor::MainWindow *mainWindow)
    : QObject(mainWindow)
{
    auto *window = qobject_cast<QMainWindow *>(mainWindow->window());
    if (!window) {
        return;
    }

    // Top-right of the menu bar, where Colab shows its RAM / Disk meter
    QMenuBar *menuBar = window->menuBar();
    m_meter = new ResourceMeter(sampler, menuBar);
    menuBar->setCornerWidget(m_meter, Qt::TopRightCorner);
    m_meter->show();
}

KateResMonPluginView::~KateResMonPluginView()
{
    delete m_meter;
}

#include "kateresmonplugin.moc"
