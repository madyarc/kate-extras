#include "katespotifyplugin.h"
#include "spotifywidget.h"

#include <KActionCollection>
#include <KLocalizedString>
#include <KPluginFactory>
#include <KTextEditor/MainWindow>
#include <KXMLGUIFactory>

#include <QAction>
#include <QIcon>

K_PLUGIN_FACTORY_WITH_JSON(KateSpotifyPluginFactory, "katespotifyplugin.json", registerPlugin<KateSpotifyPlugin>();)

KateSpotifyPlugin::KateSpotifyPlugin(QObject *parent, const QVariantList &)
    : KTextEditor::Plugin(parent)
{
}

QObject *KateSpotifyPlugin::createView(KTextEditor::MainWindow *mainWindow)
{
    return new KateSpotifyPluginView(this, mainWindow);
}

KateSpotifyPluginView::KateSpotifyPluginView(KateSpotifyPlugin *plugin, KTextEditor::MainWindow *mainWindow)
    : QObject(mainWindow)
    , m_mainWindow(mainWindow)
{
    KXMLGUIClient::setComponentName(QStringLiteral("katespotify"), i18n("Spotify"));

    const QIcon icon = QIcon::fromTheme(QStringLiteral("spotify-client"), QIcon::fromTheme(QStringLiteral("media-playback-start")));
    m_toolView.reset(mainWindow->createToolView(plugin, QStringLiteral("kate_spotify"), KTextEditor::MainWindow::Right, icon, i18n("Spotify")));
    m_widget = new SpotifyWidget(m_toolView.get());

    // Actions show up in Tools > Spotify and can be bound in Settings > Configure Keyboard Shortcuts
    auto addPlayerAction = [this](const QString &name, const QString &text, const QString &iconName, void (SpotifyWidget::*slot)()) {
        QAction *action = actionCollection()->addAction(name);
        action->setText(text);
        action->setIcon(QIcon::fromTheme(iconName));
        connect(action, &QAction::triggered, this, [this, slot]() {
            if (m_widget) {
                (m_widget->*slot)();
            }
        });
    };
    addPlayerAction(QStringLiteral("spotify_playpause"), i18n("Play / Pause"), QStringLiteral("media-playback-start"), &SpotifyWidget::playPause);
    addPlayerAction(QStringLiteral("spotify_next"), i18n("Next Track"), QStringLiteral("media-skip-forward"), &SpotifyWidget::next);
    addPlayerAction(QStringLiteral("spotify_previous"), i18n("Previous Track"), QStringLiteral("media-skip-backward"), &SpotifyWidget::previous);
    addPlayerAction(QStringLiteral("spotify_show"), i18n("Show Spotify"), QStringLiteral("spotify-client"), &SpotifyWidget::raiseSpotify);

    setXML(QStringLiteral(R"(<!DOCTYPE gui SYSTEM "kpartgui.dtd">
<gui name="katespotify" version="1">
  <MenuBar>
    <Menu name="tools">
      <Menu name="spotify">
        <text>Spotify</text>
        <Action name="spotify_playpause"/>
        <Action name="spotify_previous"/>
        <Action name="spotify_next"/>
        <Separator/>
        <Action name="spotify_show"/>
      </Menu>
    </Menu>
  </MenuBar>
</gui>)"));

    m_mainWindow->guiFactory()->addClient(this);
}

KateSpotifyPluginView::~KateSpotifyPluginView()
{
    m_mainWindow->guiFactory()->removeClient(this);
}

#include "katespotifyplugin.moc"
