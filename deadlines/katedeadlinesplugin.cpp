#include "katedeadlinesplugin.h"
#include "deadlinestore.h"
#include "deadlinewidget.h"

#include <KActionCollection>
#include <KLocalizedString>
#include <KPluginFactory>
#include <KTextEditor/MainWindow>
#include <KXMLGUIFactory>

#include <QAction>
#include <QIcon>

K_PLUGIN_FACTORY_WITH_JSON(KateDeadlinesPluginFactory, "katedeadlinesplugin.json", registerPlugin<KateDeadlinesPlugin>();)

KateDeadlinesPlugin::KateDeadlinesPlugin(QObject *parent, const QVariantList &)
    : KTextEditor::Plugin(parent)
    , m_store(new DeadlineStore(this))
{
}

QObject *KateDeadlinesPlugin::createView(KTextEditor::MainWindow *mainWindow)
{
    return new KateDeadlinesPluginView(this, m_store, mainWindow);
}

KateDeadlinesPluginView::KateDeadlinesPluginView(KateDeadlinesPlugin *plugin, DeadlineStore *store, KTextEditor::MainWindow *mainWindow)
    : QObject(mainWindow)
    , m_mainWindow(mainWindow)
{
    KXMLGUIClient::setComponentName(QStringLiteral("katedeadlines"), i18n("Deadlines"));

    m_toolView.reset(mainWindow->createToolView(plugin,
                                                QStringLiteral("kate_deadlines"),
                                                KTextEditor::MainWindow::Right,
                                                QIcon::fromTheme(QStringLiteral("view-calendar")),
                                                i18n("Deadlines")));
    m_widget = new DeadlineWidget(store, m_toolView.get());

    // Available from Tools > Deadlines and bindable in Settings > Configure Keyboard Shortcuts
    QAction *addAction = actionCollection()->addAction(QStringLiteral("deadlines_add"));
    addAction->setText(i18n("Add Deadline…"));
    addAction->setIcon(QIcon::fromTheme(QStringLiteral("appointment-new")));
    connect(addAction, &QAction::triggered, this, [this]() {
        if (m_widget) {
            m_widget->addDeadline();
        }
    });

    QAction *showAction = actionCollection()->addAction(QStringLiteral("deadlines_show"));
    showAction->setText(i18n("Show Deadlines"));
    showAction->setIcon(QIcon::fromTheme(QStringLiteral("view-calendar")));
    connect(showAction, &QAction::triggered, this, [this]() {
        m_mainWindow->showToolView(m_toolView.get());
    });

    setXML(QStringLiteral(R"(<!DOCTYPE gui SYSTEM "kpartgui.dtd">
<gui name="katedeadlines" version="1">
  <MenuBar>
    <Menu name="tools">
      <Menu name="deadlines">
        <text>Deadlines</text>
        <Action name="deadlines_add"/>
        <Action name="deadlines_show"/>
      </Menu>
    </Menu>
  </MenuBar>
</gui>)"));

    m_mainWindow->guiFactory()->addClient(this);
}

KateDeadlinesPluginView::~KateDeadlinesPluginView()
{
    m_mainWindow->guiFactory()->removeClient(this);
}

#include "katedeadlinesplugin.moc"
