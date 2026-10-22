#pragma once

#include <KTextEditor/Plugin>
#include <KXMLGUIClient>

#include <QPointer>
#include <QVariantList>

#include <memory>

namespace KTextEditor
{
class MainWindow;
}

class SpotifyWidget;

class KateSpotifyPlugin : public KTextEditor::Plugin
{
    Q_OBJECT

public:
    explicit KateSpotifyPlugin(QObject *parent, const QVariantList & = QVariantList());

    QObject *createView(KTextEditor::MainWindow *mainWindow) override;
};

class KateSpotifyPluginView : public QObject, public KXMLGUIClient
{
    Q_OBJECT

public:
    KateSpotifyPluginView(KateSpotifyPlugin *plugin, KTextEditor::MainWindow *mainWindow);
    ~KateSpotifyPluginView() override;

private:
    KTextEditor::MainWindow *m_mainWindow;
    std::unique_ptr<QWidget> m_toolView;
    QPointer<SpotifyWidget> m_widget;
};
