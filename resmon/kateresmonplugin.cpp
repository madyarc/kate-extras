#include "kateresmonplugin.h"
#include "resourcemeter.h"
#include "resourcesampler.h"
#include "resourcestatusitem.h"

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
    return new KateResMonPluginView(this, mainWindow);
}

ResourceSampler *KateResMonPlugin::sampler() const
{
    return m_sampler;
}

void KateResMonPlugin::showStatusItem()
{
    if (!m_statusItem) {
        m_statusItem = new ResourceStatusItem(m_sampler, this);
    }
}

KateResMonPluginView::KateResMonPluginView(KateResMonPlugin *plugin, KTextEditor::MainWindow *mainWindow)
    : QObject(mainWindow)
{
    auto *window = qobject_cast<QMainWindow *>(mainWindow->window());
    if (!window) {
        return;
    }

    // A global menu bar (macOS, or a Linux desktop with a global menu) has no room
    // for corner widgets, so the meter goes into the menu bar extras instead
    QMenuBar *menuBar = window->menuBar();
    if (menuBar->isNativeMenuBar()) {
        plugin->showStatusItem();
        return;
    }

    // Top-right of the menu bar, where Colab shows its RAM / Disk meter
    m_meter = new ResourceMeter(plugin->sampler(), menuBar);
    menuBar->setCornerWidget(m_meter, Qt::TopRightCorner);
    m_meter->show();
}

KateResMonPluginView::~KateResMonPluginView()
{
    delete m_meter;
}

#include "kateresmonplugin.moc"
