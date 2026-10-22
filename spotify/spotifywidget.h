#pragma once

#include <QDBusObjectPath>
#include <QUrl>
#include <QVariantMap>
#include <QWidget>

class QLabel;
class QNetworkAccessManager;
class QSlider;
class QTimer;
class QToolButton;

// Side panel that drives the Spotify desktop client over MPRIS (D-Bus)
class SpotifyWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SpotifyWidget(QWidget *parent = nullptr);

public Q_SLOTS:
    void playPause();
    void next();
    void previous();
    void raiseSpotify();

private Q_SLOTS:
    void onPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);

private:
    void setAvailable(bool available);
    void refresh();
    void applyProperties(const QVariantMap &properties);
    void applyMetadata(const QVariantMap &metadata);
    void updatePlayButton();
    void updateTimeLabels(qint64 positionMs);
    void loadArt(const QUrl &url);
    void callPlayer(const QString &method, const QVariantList &args = {});
    void seekTo(qint64 positionMs);
    void setVolume(int percent);
    void launchSpotify();

    QWidget *m_offlinePanel = nullptr;
    QWidget *m_playerPanel = nullptr;
    QLabel *m_artLabel = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_artistLabel = nullptr;
    QLabel *m_albumLabel = nullptr;
    QLabel *m_elapsedLabel = nullptr;
    QLabel *m_lengthLabel = nullptr;
    QSlider *m_seekSlider = nullptr;
    QSlider *m_volumeSlider = nullptr;
    QToolButton *m_prevButton = nullptr;
    QToolButton *m_playButton = nullptr;
    QToolButton *m_nextButton = nullptr;

    QTimer *m_pollTimer = nullptr;
    QNetworkAccessManager *m_network = nullptr;

    bool m_available = false;
    bool m_refreshPending = false;
    QString m_playbackStatus;
    QDBusObjectPath m_trackId;
    qint64 m_lengthMs = 0;
    QUrl m_artUrl;
};
