#include "spotifyplayer.h"

#include <KLocalizedString>

#include <QProcess>
#include <QStringList>

#import <AppKit/AppKit.h>

namespace
{
const QString s_bundleId = QStringLiteral("com.spotify.client");
const QString s_osascript = QStringLiteral("/usr/bin/osascript");
const QChar s_separator(0x1f);

// The "is running" guard keeps a poll from relaunching Spotify right after the user quits it
QString guarded(const QString &commands)
{
    return QStringLiteral("if application id \"%1\" is running then\ntell application id \"%1\"\n%2\nend tell\nend if").arg(s_bundleId, commands);
}

// One line of unit-separated fields, so titles with commas or quotes survive
const QString s_stateScript = guarded(QStringLiteral(R"(
set sep to character id 31
set out to (player state as text) & sep & (sound volume as text) & sep & (((player position) * 1000) as integer as text)
try
    set t to current track
    set out to out & sep & (name of t) & sep & (artist of t) & sep & (album of t) & sep & (duration of t as text) & sep & (artwork url of t) & sep & (id of t)
end try
return out)"));

// Asking the window server is free; osascript is a process launch, so skip it while Spotify is closed
bool spotifyRunning()
{
    @autoreleasepool {
        return [NSRunningApplication runningApplicationsWithBundleIdentifier:s_bundleId.toNSString()].count > 0;
    }
}
}

SpotifyPlayer::SpotifyPlayer(QObject *parent)
    : QObject(parent)
    , m_stateProcess(new QProcess(this))
{
    connect(m_stateProcess, &QProcess::finished, this, &SpotifyPlayer::onStateScriptFinished);
    connect(m_stateProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_refreshPending = false;
        }
    });

    m_available = spotifyRunning();
}

void SpotifyPlayer::playPause()
{
    runScript(QStringLiteral("playpause"));
}

void SpotifyPlayer::next()
{
    runScript(QStringLiteral("next track"));
}

void SpotifyPlayer::previous()
{
    runScript(QStringLiteral("previous track"));
}

void SpotifyPlayer::seek(qint64 positionMs)
{
    runScript(QStringLiteral("set player position to %1").arg(QString::number(positionMs / 1000.0, 'f', 3)));
}

void SpotifyPlayer::setVolume(int percent)
{
    runScript(QStringLiteral("set sound volume to %1").arg(percent));
}

void SpotifyPlayer::raise()
{
    launch();
}

void SpotifyPlayer::launch()
{
    // `open` starts Spotify, or brings it to the front if it is already running
    QProcess::startDetached(QStringLiteral("/usr/bin/open"), {QStringLiteral("-b"), s_bundleId});
}

void SpotifyPlayer::refresh()
{
    if (m_refreshPending) {
        return;
    }
    if (!spotifyRunning()) {
        setAvailable(false);
        return;
    }
    m_refreshPending = true;
    m_stateProcess->start(s_osascript, {QStringLiteral("-e"), s_stateScript});
}

void SpotifyPlayer::onStateScriptFinished()
{
    m_refreshPending = false;

    if (m_stateProcess->exitCode() != 0) {
        // -1743: the user has not allowed Kate to control Spotify
        if (m_stateProcess->readAllStandardError().contains("-1743")) {
            setAvailable(false);
            Q_EMIT problem(i18n("Kate is not allowed to control Spotify. Turn it on in System Settings → Privacy & Security → Automation."));
        }
        return;
    }

    const QString output = QString::fromUtf8(m_stateProcess->readAllStandardOutput()).trimmed();
    if (output.isEmpty()) {
        // Spotify quit between the running check and the script
        setAvailable(false);
        return;
    }
    setAvailable(true);

    const QStringList fields = output.split(s_separator);
    QVariantMap properties;

    const QString state = fields.value(0);
    properties.insert(QStringLiteral("PlaybackStatus"), state.isEmpty() ? QString() : state.at(0).toUpper() + state.mid(1));
    properties.insert(QStringLiteral("Volume"), fields.value(1).toInt() / 100.0);
    properties.insert(QStringLiteral("Position"), fields.value(2).toLongLong() * 1000);

    const bool hasTrack = fields.size() >= 9;
    properties.insert(QStringLiteral("CanGoNext"), hasTrack);
    properties.insert(QStringLiteral("CanGoPrevious"), hasTrack);
    properties.insert(QStringLiteral("CanSeek"), hasTrack);

    QVariantMap metadata;
    if (hasTrack) {
        metadata.insert(QStringLiteral("xesam:title"), fields.at(3));
        metadata.insert(QStringLiteral("xesam:artist"), QStringList{fields.at(4)});
        metadata.insert(QStringLiteral("xesam:album"), fields.at(5));
        metadata.insert(QStringLiteral("mpris:length"), fields.at(6).toLongLong() * 1000);
        metadata.insert(QStringLiteral("mpris:artUrl"), fields.at(7));
        metadata.insert(QStringLiteral("mpris:trackid"), fields.at(8));
    }
    properties.insert(QStringLiteral("Metadata"), metadata);

    Q_EMIT propertiesChanged(properties);
}

void SpotifyPlayer::runScript(const QString &commands)
{
    if (!m_available) {
        return;
    }
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, process, &QObject::deleteLater);
    connect(process, &QProcess::errorOccurred, process, &QObject::deleteLater);
    connect(process, &QProcess::finished, this, &SpotifyPlayer::refresh);
    process->start(s_osascript, {QStringLiteral("-e"), guarded(commands)});
}
