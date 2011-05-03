#include <stdlib.h>
#include <windows.h>
#include "util.h"
#include "audio.h"
#include "spotifyplayer.h"
#include "appkey.h"

SpotifyPlayer::SpotifyPlayer(audio_fifo_t *lpAudioFifo, HANDLE hEvent)
: m_lpAudioFifo(lpAudioFifo), m_hEvent(hEvent), m_lpSession(NULL),
  m_lpPlaylistContainer(NULL), m_plCurrentPlaylist(NULL), m_szListName(NULL),
  m_lpCurrentTrack(NULL), m_dwTrackIndex(0), m_bPlaybackDone(FALSE),
  m_bNewPlaylists(FALSE), m_bLoginFailed(FALSE)
{
	memset(&m_playlistCallbacks, 0, sizeof(m_playlistCallbacks));
	m_playlistCallbacks.tracks_added = &SpotifyPlayer::tracksAdded;
	m_playlistCallbacks.tracks_removed = &SpotifyPlayer::tracksRemoved;
	m_playlistCallbacks.tracks_moved = &SpotifyPlayer::tracksMoved;
	m_playlistCallbacks.playlist_renamed = &SpotifyPlayer::playlistRenamed;
	m_playlistCallbacks.playlist_state_changed = NULL;
	m_playlistCallbacks.playlist_update_in_progress = NULL;
	m_playlistCallbacks.playlist_metadata_updated = NULL;
	m_playlistCallbacks.track_created_changed = NULL;
	m_playlistCallbacks.track_seen_changed = NULL;
	m_playlistCallbacks.description_changed = NULL;
	m_playlistCallbacks.image_changed = NULL;

	memset(&m_playlistContainerCallbacks, 0, sizeof(m_playlistContainerCallbacks));
	m_playlistContainerCallbacks.playlist_added = &SpotifyPlayer::playlistAdded;
	m_playlistContainerCallbacks.playlist_removed = &SpotifyPlayer::playlistRemoved;
	m_playlistContainerCallbacks.playlist_moved = NULL;
	m_playlistContainerCallbacks.container_loaded = &SpotifyPlayer::containerLoaded;

	memset(&m_sessionCallbacks, 0, sizeof(m_sessionCallbacks));
	m_sessionCallbacks.logged_in = &SpotifyPlayer::loggedIn;
	m_sessionCallbacks.logged_out = NULL;
	m_sessionCallbacks.metadata_updated = &SpotifyPlayer::metadataUpdated;
	m_sessionCallbacks.connection_error = &SpotifyPlayer::connectionError;
	m_sessionCallbacks.message_to_user = NULL;
	m_sessionCallbacks.notify_main_thread = &SpotifyPlayer::notifyMainThread;
	m_sessionCallbacks.music_delivery = &SpotifyPlayer::musicDelivery;
	m_sessionCallbacks.play_token_lost = &SpotifyPlayer::playTokenLost;
	m_sessionCallbacks.log_message = &SpotifyPlayer::logMessage;
	m_sessionCallbacks.end_of_track = &SpotifyPlayer::endOfTrack;
	m_sessionCallbacks.play_token_lost = &SpotifyPlayer::playTokenLost;
	m_sessionCallbacks.streaming_error = &SpotifyPlayer::streamingError;
	m_sessionCallbacks.userinfo_updated = NULL;
	m_sessionCallbacks.start_playback = NULL;
	m_sessionCallbacks.stop_playback = NULL;
	m_sessionCallbacks.get_audio_buffer_stats = NULL;

	m_hMutex = CreateMutex(NULL, FALSE, NULL);
}

SpotifyPlayer::~SpotifyPlayer()
{

}

BOOL SpotifyPlayer::connect(LPCSTR szUsername, LPCSTR szPassword)
{
	sp_session_config spconfig;
    sp_error err;

	if (!m_lpSession) {
		spconfig.api_version = SPOTIFY_API_VERSION;
		spconfig.cache_location = "tmp";
		spconfig.settings_location = "tmp";
		spconfig.application_key = g_appkey;
		spconfig.application_key_size = g_appkey_size;
		spconfig.user_agent = "spshell";
		spconfig.callbacks = &m_sessionCallbacks;
		spconfig.userdata = (LPVOID)this;

		err = sp_session_create(&spconfig, &m_lpSession);
		if (SP_ERROR_OK != err) {
			return FALSE;
		}
	}

	err = sp_session_login(m_lpSession, szUsername, szPassword);
	if (SP_ERROR_OK != err) {
		return FALSE;
	}

	return TRUE;
}

VOID SpotifyPlayer::setPlaylist(LPCSTR szPlaylist)
{
	sp_playlistcontainer *pc;

	WaitForSingleObject(m_hMutex, INFINITE);
	if (m_szListName != NULL) {
		free((LPVOID)m_szListName);
	}

	if (szPlaylist) {
		m_szListName = strdup(szPlaylist);
	} else {
		szPlaylist = NULL;
	}

	m_plCurrentPlaylist = NULL;

	pc = sp_session_playlistcontainer(m_lpSession);
    for (int i = 0; i < sp_playlistcontainer_num_playlists(pc); ++i) {
        sp_playlist *pl = sp_playlistcontainer_playlist(pc, i);
		if (strcasecmp(sp_playlist_name(pl), szPlaylist) == 0) {
			m_plCurrentPlaylist = pl;
		}
    }

	if (m_plCurrentPlaylist != NULL) {
		tryStart();
	}

	ReleaseMutex(m_hMutex);
}

INT SpotifyPlayer::handleEvents()
{
	INT dwNextTimeOut;

	if (!m_lpSession) {
		return 0;
	}

	sp_connectionstate state = sp_session_connectionstate(m_lpSession);
	if (m_lpPlaylistContainer == NULL && state == SP_CONNECTION_STATE_LOGGED_IN) {
		m_lpPlaylistContainer = sp_session_playlistcontainer(m_lpSession);
		sp_playlistcontainer_add_callbacks(m_lpPlaylistContainer, &m_playlistContainerCallbacks, (LPVOID)this);
	}

	if (m_bPlaybackDone) {
		WaitForSingleObject(m_hMutex, INFINITE);
		if (m_lpCurrentTrack) {
			m_lpCurrentTrack = NULL;
			sp_session_player_unload(m_lpSession);
		}

		m_dwTrackIndex++;
		m_bPlaybackDone = FALSE;

		ReleaseMutex(m_hMutex);

		tryStart();
	}

	do {
		sp_session_process_events(m_lpSession, &dwNextTimeOut);
	} while (dwNextTimeOut == 0);

	return dwNextTimeOut;
}

VOID SpotifyPlayer::tryStart()
{
    sp_track *t;
	INT iTracks;

    if (!m_plCurrentPlaylist)
        return;

    if (!sp_playlist_num_tracks(m_plCurrentPlaylist)) {
        return;
    }

	iTracks = sp_playlist_num_tracks(m_plCurrentPlaylist);
    if (iTracks < m_dwTrackIndex) {
        return;
    }

    t = sp_playlist_track(m_plCurrentPlaylist, m_dwTrackIndex);

	WaitForSingleObject(m_hMutex, INFINITE);

    if (m_lpCurrentTrack && t != m_lpCurrentTrack) {
        /* Someone changed the current track */
        audio_fifo_flush(m_lpAudioFifo);
        sp_session_player_unload(m_lpSession);
        m_lpCurrentTrack = NULL;
    }

    if (!t)
        goto done;

	if (!sp_track_is_available(m_lpSession, t)) {
		m_bPlaybackDone = TRUE;
		SetEvent(m_hEvent);
		goto done;
	}

	if (!sp_track_is_loaded(t)) {
		sp_error err = sp_track_error(t);
        goto done;
	}

    if (m_lpCurrentTrack == t)
        goto done;

    m_lpCurrentTrack = t;

    sp_session_player_load(m_lpSession, t);
    sp_session_player_play(m_lpSession, 1);

done:
	ReleaseMutex(m_hMutex);
}

void SpotifyPlayer::nextSong()
{
	audio_fifo_flush(m_lpAudioFifo);
	m_bPlaybackDone = TRUE;
}

BOOL SpotifyPlayer::isConnected()
{
	if (!m_lpSession) {
		return FALSE;
	}

	sp_connectionstate state = sp_session_connectionstate(m_lpSession);
	return state == SP_CONNECTION_STATE_LOGGED_IN;
}

BOOL SpotifyPlayer::hasFailedLogin()
{
	BOOL bState;

	WaitForSingleObject(m_hMutex, INFINITE);
	bState = m_bLoginFailed;
	m_bLoginFailed = FALSE;
	ReleaseMutex(m_hMutex);

	return bState;
}

BOOL SpotifyPlayer::hasNewPlaylists()
{
	BOOL bState;

	WaitForSingleObject(m_hMutex, INFINITE);
	bState = m_bNewPlaylists;
	m_bNewPlaylists = FALSE;
	ReleaseMutex(m_hMutex);

	return bState;
}

int SpotifyPlayer::getPlaylists(LPCSTR **out)
{
	int iPlaylists, iLoaded;
	LPCSTR *playlists;

	if (!m_lpPlaylistContainer) {
		*out = NULL;
		return 0;
	}

	iPlaylists = sp_playlistcontainer_num_playlists(m_lpPlaylistContainer);
	iLoaded = 0;
	for (int i = 0; i < iPlaylists; i++) {
		sp_playlist *pl;

		pl = sp_playlistcontainer_playlist(m_lpPlaylistContainer, i);
		if (sp_playlist_is_loaded(pl)) {
			iLoaded++;
		}
	}

	playlists = new LPCSTR[iLoaded];
	for (int i = 0; i < iLoaded; i++) {
		sp_playlist *pl;

		pl = sp_playlistcontainer_playlist(m_lpPlaylistContainer, i);
		playlists[i] = strdup(sp_playlist_name(pl));
	}

	*out = playlists;

	return iLoaded;

}

LPSTR SpotifyPlayer::getCurrentTrackName()
{
	LPSTR szTrackName = NULL;

	WaitForSingleObject(m_hMutex, INFINITE);
	if (!m_lpCurrentTrack) {
		goto done;
	}

	szTrackName = strdup(sp_track_name(m_lpCurrentTrack));

done:
	ReleaseMutex(m_hMutex);
	return szTrackName;
}

LPSTR SpotifyPlayer::getCurrentTrackArtist()
{
	LPSTR szArtistName = NULL;
	sp_artist *lpArtist;

	WaitForSingleObject(m_hMutex, INFINITE);
	if (!m_lpCurrentTrack) {
		goto done;
	}

	lpArtist = sp_track_artist(m_lpCurrentTrack, 0);
	szArtistName = strdup(sp_artist_name(lpArtist));

done:
	ReleaseMutex(m_hMutex);
	return szArtistName;
}

// Session callbacks

VOID SpotifyPlayer::loggedIn(sp_session *sess, sp_error error)
{
    sp_playlistcontainer *pc;
	SpotifyPlayer *lpPlayer;

	lpPlayer = (SpotifyPlayer*)sp_session_userdata(sess);

     if (SP_ERROR_OK != error) {
		WaitForSingleObject(lpPlayer->m_hMutex, INFINITE);
		lpPlayer->m_bLoginFailed = TRUE;
		ReleaseMutex(lpPlayer->m_hMutex);

        return;
    }

	pc = sp_session_playlistcontainer(sess);
    for (int i = 0; i < sp_playlistcontainer_num_playlists(pc); ++i) {
        sp_playlist *pl = sp_playlistcontainer_playlist(pc, i);
		sp_playlist_add_callbacks(pl, &lpPlayer->m_playlistCallbacks, (LPVOID)lpPlayer);
    }

	// Notify main thread that we've succesfully logged in
	SetEvent(lpPlayer->m_hEvent);
}

VOID SpotifyPlayer::notifyMainThread(sp_session *sess)
{
	SpotifyPlayer *lpPlayer;
	lpPlayer = (SpotifyPlayer*)sp_session_userdata(sess);

	SetEvent(lpPlayer->m_hEvent);
}

INT SpotifyPlayer::musicDelivery(sp_session *sess, const sp_audioformat *format,
	const void *frames, int num_frames)
{
	audio_fifo_data_t *afd;
	SpotifyPlayer *lpPlayer;

	lpPlayer = (SpotifyPlayer*)sp_session_userdata(sess);

	// Rate limit the number of frames in the queue to one second
	// of playback.
	if (audio_available_frames(lpPlayer->m_lpAudioFifo) > format->sample_rate) {
		return 0;
	}

	afd = (audio_fifo_data_t*)malloc(sizeof(audio_fifo_data_t) + num_frames * format->channels * 2);
	afd->channels = format->channels;
	afd->nframes = num_frames;
	afd->rate = format->sample_rate;
	
	memcpy(afd->frames, frames, num_frames * format->channels * 2);

	audio_fifo_push(lpPlayer->m_lpAudioFifo, afd);

    return num_frames;
}


VOID SpotifyPlayer::endOfTrack(sp_session *sess)
{
	SpotifyPlayer *lpPlayer;
	lpPlayer = (SpotifyPlayer*)sp_session_userdata(sess);

	lpPlayer->m_bPlaybackDone = TRUE;
	SetEvent(lpPlayer->m_hEvent);
}


VOID SpotifyPlayer::metadataUpdated(sp_session *sess)
{
	SpotifyPlayer *lpPlayer;
	lpPlayer = (SpotifyPlayer*)sp_session_userdata(sess);

	/*WaitForSingleObject(lpPlayer->m_hMutex, INFINITE);
	lpPlayer->m_bNewPlaylists = TRUE;
	ReleaseMutex(lpPlayer->m_hMutex);*/

	lpPlayer->tryStart();
}

VOID SpotifyPlayer::playTokenLost(sp_session *sess)
{
	SpotifyPlayer *lpPlayer;

	lpPlayer = (SpotifyPlayer*)sp_session_userdata(sess);
    audio_fifo_flush(lpPlayer->m_lpAudioFifo);
}

VOID SpotifyPlayer::connectionError(sp_session *sp, sp_error err)
{
}

VOID SpotifyPlayer::logMessage(sp_session *session, const char *data)
{
}

VOID SpotifyPlayer::streamingError(sp_session *session, sp_error error)
{
}

// Playlist callbacks

VOID SpotifyPlayer::tracksAdded(sp_playlist *pl, sp_track * const *tracks,
	int num_tracks, int position, void *userdata)
{
	SpotifyPlayer *lpPlayer;
	lpPlayer = (SpotifyPlayer*)userdata;

	WaitForSingleObject(lpPlayer->m_hMutex, INFINITE);
	lpPlayer->m_bNewPlaylists = TRUE;
	ReleaseMutex(lpPlayer->m_hMutex);

    if (pl != lpPlayer->m_plCurrentPlaylist)
        return;

    lpPlayer->tryStart();
}

VOID SpotifyPlayer::tracksRemoved(sp_playlist *pl, const int *tracks,
    int num_tracks, void *userdata)
{
	SpotifyPlayer *lpPlayer;
    int i, k;

	lpPlayer = (SpotifyPlayer*)userdata;

    if (pl != lpPlayer->m_plCurrentPlaylist)
        return;

	k = 0;
	for (i = 0; i < num_tracks; ++i) {
		if (tracks[i] < lpPlayer->m_dwTrackIndex) {
            ++k;
		}
	}

    lpPlayer->m_dwTrackIndex -= k;

	lpPlayer->tryStart();
}

VOID SpotifyPlayer::tracksMoved(sp_playlist *pl, const int *tracks,
	int num_tracks, int new_position, void *userdata)
{
	SpotifyPlayer *lpPlayer;
	lpPlayer = (SpotifyPlayer*)userdata;

    if (pl != lpPlayer->m_plCurrentPlaylist)
        return;

    lpPlayer->tryStart();
}

VOID SpotifyPlayer::playlistRenamed(sp_playlist *pl, void *userdata)
{
	SpotifyPlayer *lpPlayer;
    LPCSTR szName;

	lpPlayer = (SpotifyPlayer*)userdata;
	szName = sp_playlist_name(pl);

	WaitForSingleObject(lpPlayer->m_hMutex, INFINITE);
    if (!strcasecmp(szName, lpPlayer->m_szListName)) {
        lpPlayer->m_plCurrentPlaylist = pl;
        lpPlayer->m_dwTrackIndex = 0;
        lpPlayer->tryStart();
    } else if (lpPlayer->m_plCurrentPlaylist == pl) {
        lpPlayer->m_plCurrentPlaylist = NULL;
        lpPlayer->m_lpCurrentTrack = NULL;
		sp_session_player_unload(lpPlayer->m_lpSession);
    }
	ReleaseMutex(lpPlayer->m_hMutex);
}

// Playlist container callbacks

VOID SpotifyPlayer::playlistAdded(sp_playlistcontainer *pc, sp_playlist *pl,
    int position, void *userdata)
{
	SpotifyPlayer *lpPlayer;
	lpPlayer = (SpotifyPlayer*)userdata;

    sp_playlist_add_callbacks(pl, &lpPlayer->m_playlistCallbacks, userdata);

	if (!strcasecmp(sp_playlist_name(pl), lpPlayer->m_szListName)) {
        lpPlayer->m_plCurrentPlaylist = pl;
		lpPlayer->tryStart();
    }

	WaitForSingleObject(lpPlayer->m_hMutex, INFINITE);
	lpPlayer->m_bNewPlaylists = TRUE;
	ReleaseMutex(lpPlayer->m_hMutex);

	// Notify main thread that playlists are available
	SetEvent(lpPlayer->m_hEvent);
}

VOID SpotifyPlayer::playlistRemoved(sp_playlistcontainer *pc, sp_playlist *pl,
    int position, void *userdata)
{
	SpotifyPlayer *lpPlayer;
	lpPlayer = (SpotifyPlayer*)userdata;

    sp_playlist_remove_callbacks(pl, &lpPlayer->m_playlistCallbacks, userdata);

	WaitForSingleObject(lpPlayer->m_hMutex, INFINITE);
	lpPlayer->m_bNewPlaylists = TRUE;
	ReleaseMutex(lpPlayer->m_hMutex);
}

VOID SpotifyPlayer::containerLoaded(sp_playlistcontainer *pc, void *userdata)
{
}
