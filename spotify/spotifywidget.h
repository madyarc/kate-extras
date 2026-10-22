#pragma once

#include <QUrl>
#include <QVariantMap>
#include <QWidget>

class QLabel;
class QNetworkAccessManager;
class QSlider;
class QTimer;
class QToolButton;
class SpotifyPlayer;

// Side panel that drives the Spotify desktop client through SpotifyPlayer
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

private:
    void setAvailable(bool available);
    void refresh();
    void applyProperties(const QVariantMap &properties);
    void applyMetadata(const QVariantMap &metadata);
    void updatePlayButton();
    void updateTimeLabels(qint64 positionMs);
    void loadArt(const QUrl &url);

    SpotifyPlayer *m_player = nullptr;
    QWidget *m_offlinePanel = nullptr;
    QLabel *m_offlineLabel = nullptr;
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

    QString m_playbackStatus;
    qint64 m_lengthMs = 0;
    QUrl m_artUrl;
};
