#include "spotifyplayer.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

namespace
{
const QString s_service = QStringLiteral("org.mpris.MediaPlayer2.spotify");
const QString s_path = QStringLiteral("/org/mpris/MediaPlayer2");
const QString s_rootInterface = QStringLiteral("org.mpris.MediaPlayer2");
const QString s_playerInterface = QStringLiteral("org.mpris.MediaPlayer2.Player");
const QString s_propertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");

// Nested a{sv} values arrive still marshalled as QDBusArgument
QVariantMap toVariantMap(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QDBusArgument>()) {
        return qdbus_cast<QVariantMap>(value.value<QDBusArgument>());
    }
    return value.toMap();
}
}

SpotifyPlayer::SpotifyPlayer(QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::sessionBus();

    auto *serviceWatcher = new QDBusServiceWatcher(s_service, bus, QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this);
    connect(serviceWatcher, &QDBusServiceWatcher::serviceRegistered, this, [this]() {
        setAvailable(true);
    });
    connect(serviceWatcher, &QDBusServiceWatcher::serviceUnregistered, this, [this]() {
        setAvailable(false);
    });

    bus.connect(s_service, s_path, s_propertiesInterface, QStringLiteral("PropertiesChanged"), this, SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));

    m_available = bus.interface()->isServiceRegistered(s_service);
}

void SpotifyPlayer::playPause()
{
    call(QStringLiteral("PlayPause"));
}

void SpotifyPlayer::next()
{
    call(QStringLiteral("Next"));
}

void SpotifyPlayer::previous()
{
    call(QStringLiteral("Previous"));
}

void SpotifyPlayer::seek(qint64 positionMs)
{
    if (m_trackId.isEmpty()) {
        return;
    }
    call(QStringLiteral("SetPosition"), {QVariant::fromValue(QDBusObjectPath(m_trackId)), QVariant::fromValue(positionMs * 1000)});
}

void SpotifyPlayer::setVolume(int percent)
{
    if (!m_available) {
        return;
    }
    QDBusMessage message = QDBusMessage::createMethodCall(s_service, s_path, s_propertiesInterface, QStringLiteral("Set"));
    message << s_playerInterface << QStringLiteral("Volume") << QVariant::fromValue(QDBusVariant(percent / 100.0));
    QDBusConnection::sessionBus().asyncCall(message);
}

void SpotifyPlayer::raise()
{
    if (!m_available) {
        launch();
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
            launch();
        }
    });
}

void SpotifyPlayer::launch()
{
    for (const QString &name : {QStringLiteral("spotify-launcher"), QStringLiteral("spotify")}) {
        const QString executable = QStandardPaths::findExecutable(name);
        if (!executable.isEmpty()) {
            QProcess::startDetached(executable, {});
            return;
        }
    }
}

void SpotifyPlayer::refresh()
{
    if (!m_available || m_refreshPending) {
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
            Q_EMIT propertiesChanged(normalized(reply.value()));
        }
    });
}

void SpotifyPlayer::onPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &)
{
    if (interface == s_playerInterface) {
        Q_EMIT propertiesChanged(normalized(changed));
    }
}

QVariantMap SpotifyPlayer::normalized(QVariantMap properties)
{
    if (properties.contains(QStringLiteral("Metadata"))) {
        QVariantMap metadata = toVariantMap(properties.value(QStringLiteral("Metadata")));
        const QVariant trackId = metadata.value(QStringLiteral("mpris:trackid"));
        m_trackId = trackId.metaType() == QMetaType::fromType<QDBusObjectPath>() ? trackId.value<QDBusObjectPath>().path() : trackId.toString();
        metadata.insert(QStringLiteral("mpris:trackid"), m_trackId);
        properties.insert(QStringLiteral("Metadata"), metadata);
    }
    return properties;
}

void SpotifyPlayer::call(const QString &method, const QVariantList &args)
{
    if (!m_available) {
        return;
    }
    QDBusMessage message = QDBusMessage::createMethodCall(s_service, s_path, s_playerInterface, method);
    message.setArguments(args);
    QDBusConnection::sessionBus().asyncCall(message);

    // Pick up the new state quickly instead of waiting for the next poll
    QTimer::singleShot(300, this, &SpotifyPlayer::refresh);
}
