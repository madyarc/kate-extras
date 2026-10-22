#include "spotifywidget.h"
#include "spotifyplayer.h"

#include <KLocalizedString>

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPixmap>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
constexpr int s_artSize = 220;

QString formatTime(qint64 ms)
{
    const qint64 totalSeconds = ms / 1000;
    return QStringLiteral("%1:%2").arg(totalSeconds / 60).arg(totalSeconds % 60, 2, 10, QLatin1Char('0'));
}

QToolButton *makeButton(const QString &iconName, const QString &toolTip, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setIcon(QIcon::fromTheme(iconName));
    button->setToolTip(toolTip);
    button->setAutoRaise(true);
    button->setIconSize(QSize(28, 28));
    return button;
}
}

SpotifyWidget::SpotifyWidget(QWidget *parent)
    : QWidget(parent)
    , m_player(new SpotifyPlayer(this))
    , m_pollTimer(new QTimer(this))
    , m_network(new QNetworkAccessManager(this))
{
    auto *layout = new QVBoxLayout(this);

    // Shown while the Spotify client is not running
    m_offlinePanel = new QWidget(this);
    auto *offlineLayout = new QVBoxLayout(m_offlinePanel);
    m_offlineLabel = new QLabel(m_offlinePanel);
    m_offlineLabel->setAlignment(Qt::AlignCenter);
    m_offlineLabel->setWordWrap(true);
    auto *launchButton = new QPushButton(QIcon::fromTheme(QStringLiteral("spotify-client")), i18n("Launch Spotify"), m_offlinePanel);
    connect(launchButton, &QPushButton::clicked, m_player, &SpotifyPlayer::launch);
    offlineLayout->addStretch();
    offlineLayout->addWidget(m_offlineLabel);
    offlineLayout->addWidget(launchButton, 0, Qt::AlignHCenter);
    offlineLayout->addStretch();
    layout->addWidget(m_offlinePanel);

    m_playerPanel = new QWidget(this);
    auto *playerLayout = new QVBoxLayout(m_playerPanel);

    m_artLabel = new QLabel(m_playerPanel);
    m_artLabel->setAlignment(Qt::AlignCenter);
    m_artLabel->setMinimumHeight(s_artSize);
    playerLayout->addWidget(m_artLabel);

    m_titleLabel = new QLabel(m_playerPanel);
    QFont titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.2);
    m_titleLabel->setFont(titleFont);
    m_artistLabel = new QLabel(m_playerPanel);
    m_albumLabel = new QLabel(m_playerPanel);
    m_albumLabel->setEnabled(false);
    for (QLabel *label : {m_titleLabel, m_artistLabel, m_albumLabel}) {
        label->setAlignment(Qt::AlignCenter);
        label->setWordWrap(true);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        playerLayout->addWidget(label);
    }

    m_seekSlider = new QSlider(Qt::Horizontal, m_playerPanel);
    connect(m_seekSlider, &QSlider::sliderMoved, this, &SpotifyWidget::updateTimeLabels);
    connect(m_seekSlider, &QSlider::sliderReleased, this, [this]() {
        m_player->seek(m_seekSlider->value());
    });
    playerLayout->addWidget(m_seekSlider);

    auto *timeLayout = new QHBoxLayout;
    m_elapsedLabel = new QLabel(QStringLiteral("0:00"), m_playerPanel);
    m_lengthLabel = new QLabel(QStringLiteral("0:00"), m_playerPanel);
    timeLayout->addWidget(m_elapsedLabel);
    timeLayout->addStretch();
    timeLayout->addWidget(m_lengthLabel);
    playerLayout->addLayout(timeLayout);

    auto *controlsLayout = new QHBoxLayout;
    m_prevButton = makeButton(QStringLiteral("media-skip-backward"), i18n("Previous"), m_playerPanel);
    m_playButton = makeButton(QStringLiteral("media-playback-start"), i18n("Play"), m_playerPanel);
    m_nextButton = makeButton(QStringLiteral("media-skip-forward"), i18n("Next"), m_playerPanel);
    m_playButton->setIconSize(QSize(36, 36));
    connect(m_prevButton, &QToolButton::clicked, this, &SpotifyWidget::previous);
    connect(m_playButton, &QToolButton::clicked, this, &SpotifyWidget::playPause);
    connect(m_nextButton, &QToolButton::clicked, this, &SpotifyWidget::next);
    controlsLayout->addStretch();
    controlsLayout->addWidget(m_prevButton);
    controlsLayout->addWidget(m_playButton);
    controlsLayout->addWidget(m_nextButton);
    controlsLayout->addStretch();
    playerLayout->addLayout(controlsLayout);

    auto *volumeLayout = new QHBoxLayout;
    auto *volumeIcon = new QLabel(m_playerPanel);
    volumeIcon->setPixmap(QIcon::fromTheme(QStringLiteral("audio-volume-medium")).pixmap(16, 16));
    m_volumeSlider = new QSlider(Qt::Horizontal, m_playerPanel);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setToolTip(i18n("Volume"));
    connect(m_volumeSlider, &QSlider::sliderMoved, m_player, &SpotifyPlayer::setVolume);
    volumeLayout->addWidget(volumeIcon);
    volumeLayout->addWidget(m_volumeSlider);
    playerLayout->addLayout(volumeLayout);

    auto *showButton = new QPushButton(QIcon::fromTheme(QStringLiteral("spotify-client")), i18n("Show Spotify"), m_playerPanel);
    showButton->setToolTip(i18n("Bring the Spotify window to the front"));
    connect(showButton, &QPushButton::clicked, this, &SpotifyWidget::raiseSpotify);
    playerLayout->addWidget(showButton, 0, Qt::AlignHCenter);

    playerLayout->addStretch();
    layout->addWidget(m_playerPanel);

    connect(m_player, &SpotifyPlayer::availableChanged, this, &SpotifyWidget::setAvailable);
    connect(m_player, &SpotifyPlayer::propertiesChanged, this, &SpotifyWidget::applyProperties);
    connect(m_player, &SpotifyPlayer::problem, m_offlineLabel, &QLabel::setText);

    // Spotify does not announce position or volume changes, so poll while visible
    m_pollTimer->setInterval(1000);
    connect(m_pollTimer, &QTimer::timeout, this, &SpotifyWidget::refresh);
    m_pollTimer->start();

    setAvailable(m_player->isAvailable());
}

void SpotifyWidget::playPause()
{
    m_player->playPause();
}

void SpotifyWidget::next()
{
    m_player->next();
}

void SpotifyWidget::previous()
{
    m_player->previous();
}

void SpotifyWidget::raiseSpotify()
{
    m_player->raise();
}

void SpotifyWidget::setAvailable(bool available)
{
    m_offlinePanel->setVisible(!available);
    m_playerPanel->setVisible(available);

    if (available) {
        refresh();
    } else {
        m_offlineLabel->setText(i18n("Spotify is not running."));
        m_artUrl.clear();
        m_artLabel->clear();
    }
}

void SpotifyWidget::refresh()
{
    if (isVisible()) {
        m_player->refresh();
    }
}

void SpotifyWidget::applyProperties(const QVariantMap &properties)
{
    if (properties.contains(QStringLiteral("Metadata"))) {
        applyMetadata(properties.value(QStringLiteral("Metadata")).toMap());
    }

    if (properties.contains(QStringLiteral("PlaybackStatus"))) {
        m_playbackStatus = properties.value(QStringLiteral("PlaybackStatus")).toString();
        updatePlayButton();
    }

    if (properties.contains(QStringLiteral("Position")) && !m_seekSlider->isSliderDown()) {
        const qint64 positionMs = properties.value(QStringLiteral("Position")).toLongLong() / 1000;
        m_seekSlider->setValue(int(positionMs));
        updateTimeLabels(positionMs);
    }

    if (properties.contains(QStringLiteral("Volume")) && !m_volumeSlider->isSliderDown()) {
        m_volumeSlider->setValue(qRound(properties.value(QStringLiteral("Volume")).toDouble() * 100));
    }

    if (properties.contains(QStringLiteral("CanGoNext"))) {
        m_nextButton->setEnabled(properties.value(QStringLiteral("CanGoNext")).toBool());
    }
    if (properties.contains(QStringLiteral("CanGoPrevious"))) {
        m_prevButton->setEnabled(properties.value(QStringLiteral("CanGoPrevious")).toBool());
    }
    if (properties.contains(QStringLiteral("CanSeek"))) {
        m_seekSlider->setEnabled(properties.value(QStringLiteral("CanSeek")).toBool());
    }
}

void SpotifyWidget::applyMetadata(const QVariantMap &metadata)
{
    const QString title = metadata.value(QStringLiteral("xesam:title")).toString();
    m_titleLabel->setText(title.isEmpty() ? i18n("Nothing playing") : title);
    m_artistLabel->setText(metadata.value(QStringLiteral("xesam:artist")).toStringList().join(QStringLiteral(", ")));
    m_albumLabel->setText(metadata.value(QStringLiteral("xesam:album")).toString());

    m_lengthMs = metadata.value(QStringLiteral("mpris:length")).toLongLong() / 1000;
    m_seekSlider->setRange(0, int(m_lengthMs));
    m_lengthLabel->setText(formatTime(m_lengthMs));

    QString artUrl = metadata.value(QStringLiteral("mpris:artUrl")).toString();
    // Older clients report a dead open.spotify.com host for cover art
    artUrl.replace(QStringLiteral("https://open.spotify.com/image/"), QStringLiteral("https://i.scdn.co/image/"));
    loadArt(QUrl(artUrl));
}

void SpotifyWidget::updatePlayButton()
{
    const bool playing = m_playbackStatus == QLatin1String("Playing");
    m_playButton->setIcon(QIcon::fromTheme(playing ? QStringLiteral("media-playback-pause") : QStringLiteral("media-playback-start")));
    m_playButton->setToolTip(playing ? i18n("Pause") : i18n("Play"));
}

void SpotifyWidget::updateTimeLabels(qint64 positionMs)
{
    m_elapsedLabel->setText(formatTime(positionMs));
}

void SpotifyWidget::loadArt(const QUrl &url)
{
    if (url == m_artUrl) {
        return;
    }
    m_artUrl = url;

    if (!url.isValid() || url.isEmpty()) {
        m_artLabel->setPixmap(QIcon::fromTheme(QStringLiteral("media-optical-audio")).pixmap(s_artSize / 2));
        return;
    }

    QNetworkReply *reply = m_network->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, url]() {
        reply->deleteLater();
        // Ignore covers for tracks that were skipped while downloading
        if (url != m_artUrl || reply->error() != QNetworkReply::NoError) {
            return;
        }
        QPixmap pixmap;
        if (pixmap.loadFromData(reply->readAll())) {
            const qreal dpr = devicePixelRatioF();
            pixmap = pixmap.scaled(QSize(s_artSize, s_artSize) * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            pixmap.setDevicePixelRatio(dpr);
            m_artLabel->setPixmap(pixmap);
        }
    });
}
