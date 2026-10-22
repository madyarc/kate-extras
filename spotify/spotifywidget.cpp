#include "spotifywidget.h"

#include <KLocalizedString>

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QSlider>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
const QString s_service = QStringLiteral("org.mpris.MediaPlayer2.spotify");
const QString s_path = QStringLiteral("/org/mpris/MediaPlayer2");
const QString s_rootInterface = QStringLiteral("org.mpris.MediaPlayer2");
const QString s_playerInterface = QStringLiteral("org.mpris.MediaPlayer2.Player");
const QString s_propertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");
constexpr int s_artSize = 220;

// Nested a{sv} values arrive still marshalled as QDBusArgument
QVariantMap toVariantMap(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QDBusArgument>()) {
        return qdbus_cast<QVariantMap>(value.value<QDBusArgument>());
    }
    return value.toMap();
}

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
    , m_pollTimer(new QTimer(this))
    , m_network(new QNetworkAccessManager(this))
{
    auto *layout = new QVBoxLayout(this);

    // Shown while the Spotify client is not on the bus
    m_offlinePanel = new QWidget(this);
    auto *offlineLayout = new QVBoxLayout(m_offlinePanel);
    auto *offlineLabel = new QLabel(i18n("Spotify is not running."), m_offlinePanel);
    offlineLabel->setAlignment(Qt::AlignCenter);
    auto *launchButton = new QPushButton(QIcon::fromTheme(QStringLiteral("spotify-client")), i18n("Launch Spotify"), m_offlinePanel);
    connect(launchButton, &QPushButton::clicked, this, &SpotifyWidget::launchSpotify);
    offlineLayout->addStretch();
    offlineLayout->addWidget(offlineLabel);
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
        seekTo(m_seekSlider->value());
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
    connect(m_volumeSlider, &QSlider::sliderMoved, this, &SpotifyWidget::setVolume);
    volumeLayout->addWidget(volumeIcon);
    volumeLayout->addWidget(m_volumeSlider);
    playerLayout->addLayout(volumeLayout);

    auto *showButton = new QPushButton(QIcon::fromTheme(QStringLiteral("spotify-client")), i18n("Show Spotify"), m_playerPanel);
    showButton->setToolTip(i18n("Bring the Spotify window to the front"));
    connect(showButton, &QPushButton::clicked, this, &SpotifyWidget::raiseSpotify);
    playerLayout->addWidget(showButton, 0, Qt::AlignHCenter);

    playerLayout->addStretch();
    layout->addWidget(m_playerPanel);

    QDBusConnection bus = QDBusConnection::sessionBus();

    auto *serviceWatcher = new QDBusServiceWatcher(s_service, bus, QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this);
    connect(serviceWatcher, &QDBusServiceWatcher::serviceRegistered, this, [this]() {
        setAvailable(true);
    });
    connect(serviceWatcher, &QDBusServiceWatcher::serviceUnregistered, this, [this]() {
        setAvailable(false);
    });

    bus.connect(s_service, s_path, s_propertiesInterface, QStringLiteral("PropertiesChanged"), this, SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));

    // Spotify does not announce position or volume changes, so poll while visible
    m_pollTimer->setInterval(1000);
    connect(m_pollTimer, &QTimer::timeout, this, &SpotifyWidget::refresh);

    setAvailable(bus.interface()->isServiceRegistered(s_service));
}

void SpotifyWidget::playPause()
{
    callPlayer(QStringLiteral("PlayPause"));
}

void SpotifyWidget::next()
{
    callPlayer(QStringLiteral("Next"));
}

void SpotifyWidget::previous()
{
    callPlayer(QStringLiteral("Previous"));
}

void SpotifyWidget::raiseSpotify()
{
    if (!m_available) {
        launchSpotify();
        return;
    }

    QDBusMessage message = QDBusMessage::createMethodCall(s_service, s_path, s_propertiesInterface, QStringLiteral("Get"));
    message << s_rootInterface << QStringLiteral("CanRaise");

    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        QDBusPendingReply<QDBusVariant> reply = *call;
        call->deleteLater();
        if (!reply.isError() && reply.value().variant().toBool()) {
            QDBusConnection::sessionBus().asyncCall(QDBusMessage::createMethodCall(s_service, s_path, s_rootInterface, QStringLiteral("Raise")));
        } else {
            // Spotify is single-instance, so starting it again focuses the running window
            launchSpotify();
        }
    });
}

void SpotifyWidget::onPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &)
{
    if (interface == s_playerInterface) {
        applyProperties(changed);
    }
}

void SpotifyWidget::setAvailable(bool available)
{
    m_available = available;
    m_offlinePanel->setVisible(!available);
    m_playerPanel->setVisible(available);

    if (available) {
        m_pollTimer->start();
        refresh();
    } else {
        m_pollTimer->stop();
        m_artUrl.clear();
        m_artLabel->clear();
    }
}

void SpotifyWidget::refresh()
{
    if (!m_available || m_refreshPending || !isVisible()) {
        return;
    }
    m_refreshPending = true;

    QDBusMessage message = QDBusMessage::createMethodCall(s_service, s_path, s_propertiesInterface, QStringLiteral("GetAll"));
    message << s_playerInterface;

    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        m_refreshPending = false;
        QDBusPendingReply<QVariantMap> reply = *call;
        call->deleteLater();
        if (!reply.isError()) {
            applyProperties(reply.value());
        }
    });
}

void SpotifyWidget::applyProperties(const QVariantMap &properties)
{
    if (properties.contains(QStringLiteral("Metadata"))) {
        applyMetadata(toVariantMap(properties.value(QStringLiteral("Metadata"))));
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
    const QVariant trackId = metadata.value(QStringLiteral("mpris:trackid"));
    m_trackId = trackId.metaType() == QMetaType::fromType<QDBusObjectPath>() ? trackId.value<QDBusObjectPath>() : QDBusObjectPath(trackId.toString());

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

void SpotifyWidget::callPlayer(const QString &method, const QVariantList &args)
{
    if (!m_available) {
        return;
    }
    QDBusMessage message = QDBusMessage::createMethodCall(s_service, s_path, s_playerInterface, method);
    message.setArguments(args);
    QDBusConnection::sessionBus().asyncCall(message);

    // Pick up the new state quickly instead of waiting for the next poll
    QTimer::singleShot(300, this, &SpotifyWidget::refresh);
}

void SpotifyWidget::seekTo(qint64 positionMs)
{
    if (m_trackId.path().isEmpty()) {
        return;
    }
    callPlayer(QStringLiteral("SetPosition"), {QVariant::fromValue(m_trackId), QVariant::fromValue(positionMs * 1000)});
}

void SpotifyWidget::setVolume(int percent)
{
    if (!m_available) {
        return;
    }
    QDBusMessage message = QDBusMessage::createMethodCall(s_service, s_path, s_propertiesInterface, QStringLiteral("Set"));
    message << s_playerInterface << QStringLiteral("Volume") << QVariant::fromValue(QDBusVariant(percent / 100.0));
    QDBusConnection::sessionBus().asyncCall(message);
}

void SpotifyWidget::launchSpotify()
{
    for (const QString &name : {QStringLiteral("spotify-launcher"), QStringLiteral("spotify")}) {
        const QString executable = QStandardPaths::findExecutable(name);
        if (!executable.isEmpty()) {
            QProcess::startDetached(executable, {});
            return;
        }
    }
}
