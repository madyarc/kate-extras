#pragma once

#include <QObject>
#include <QVariantMap>

class QProcess;

// Talks to the Spotify desktop client: over MPRIS (D-Bus) on Linux and AppleScript on
// macOS. Both report player state as MPRIS properties ("PlaybackStatus", "Metadata", ...)
// so the panel does not need to know which one it is using.
class SpotifyPlayer : public QObject
{
    Q_OBJECT

public:
    explicit SpotifyPlayer(QObject *parent = nullptr);

    bool isAvailable() const;

    // Asks for a fresh state; the answer arrives through propertiesChanged()
    void refresh();

    void playPause();
    void next();
    void previous();
    void seek(qint64 positionMs);
    void setVolume(int percent);
    void raise();
    void launch();

Q_SIGNALS:
    void availableChanged(bool available);
    void propertiesChanged(const QVariantMap &properties);
    // Why the player is unavailable, when it is something the user can fix
    void problem(const QString &message);

private:
    void setAvailable(bool available);

    bool m_available = false;
    bool m_refreshPending = false;

#ifdef Q_OS_MACOS
    void runScript(const QString &script);
    void onStateScriptFinished();

    QProcess *m_stateProcess;
#else
private Q_SLOTS:
    void onPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);

private:
    void call(const QString &method, const QVariantList &args = {});
    QVariantMap normalized(QVariantMap properties);

    QString m_trackId;
#endif
};
