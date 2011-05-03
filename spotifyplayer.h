#pragma once

#include <libspotify/api.h>
#include "audio.h"

class SpotifyPlayer
{
public:
	SpotifyPlayer(audio_fifo_t *lpAudioFifo, HANDLE hEvent);
	~SpotifyPlayer();
	BOOL connect(LPCSTR szUsername, LPCSTR szPassword);
	VOID setPlaylist(LPCSTR szPlaylist);
	VOID tryStart();
	void nextSong();
	INT handleEvents();

	BOOL isConnected();
	BOOL hasFailedLogin();
	BOOL hasNewPlaylists();
	int getPlaylists(LPCSTR **out);
	LPSTR getCurrentTrackName();
	LPSTR getCurrentTrackArtist();

private:
	// playlist
	static VOID tracksAdded(sp_playlist *pl, sp_track * const *tracks,
		int num_tracks, int position, void *userdata);
	static VOID tracksRemoved(sp_playlist *pl, const int *tracks,
		int num_tracks, void *userdata);
	static VOID tracksMoved(sp_playlist *pl, const int *tracks,
		int num_tracks, int new_position, void *userdata);
	static VOID playlistRenamed(sp_playlist *pl, void *userdata);

	// playlist container
	static VOID playlistAdded(sp_playlistcontainer *pc, sp_playlist *pl,
		int position, void *userdata);
	static VOID playlistRemoved(sp_playlistcontainer *pc, sp_playlist *pl,
		int position, void *userdata);
	static VOID containerLoaded(sp_playlistcontainer *pc, void *userdata);

	// session
	static VOID loggedIn(sp_session *sess, sp_error error);
	static VOID notifyMainThread(sp_session *sess);
	static INT musicDelivery(sp_session *sess, const sp_audioformat *format,
		const void *frames, int num_frames);
	static VOID endOfTrack(sp_session *sess);
	static VOID metadataUpdated(sp_session *sess);
	static VOID playTokenLost(sp_session *sess);
	static VOID connectionError(sp_session *sp, sp_error err);
	static VOID logMessage(sp_session *session, const char *data);
	static VOID streamingError(sp_session *session, sp_error error);

private:
	HANDLE m_hEvent;
	HANDLE m_hMutex;
	audio_fifo_t *m_lpAudioFifo;

	sp_session *m_lpSession;
	sp_playlistcontainer *m_lpPlaylistContainer;

	sp_playlist *m_plCurrentPlaylist;
	LPCSTR m_szListName;
	sp_track *m_lpCurrentTrack;
	DWORD m_dwTrackIndex;
	BOOL m_bPlaybackDone;
	BOOL m_bNewPlaylists;
	BOOL m_bLoginFailed;

	sp_playlist_callbacks m_playlistCallbacks;
	sp_playlistcontainer_callbacks m_playlistContainerCallbacks;
	sp_session_callbacks m_sessionCallbacks;
};

