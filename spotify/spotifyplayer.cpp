#include "spotifyplayer.h"

// The platform backends live in spotifyplayer_mpris.cpp and spotifyplayer_macos.mm

bool SpotifyPlayer::isAvailable() const
{
    return m_available;
}

void SpotifyPlayer::setAvailable(bool available)
{
    if (available == m_available) {
        return;
    }
    m_available = available;
    Q_EMIT availableChanged(available);
}
