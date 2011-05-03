#pragma once

#define CANDIFY_WNDCLASS "candify_wnd_"
#define CANDIFY_TITLE "Candify"

#include <d2d1.h>
#include "audio.h"

class SpotifyPlayer;
class Visualizer;

class Candify
{
public:
    Candify(SpotifyPlayer *sp, Visualizer *pVisualizer, HANDLE hSpotifyEvent);
    ~Candify();

    // Register the window class and call methods for instantiating drawing resources
    HRESULT Initialize();

    // Process and dispatch messages
    void RunMessageLoop();

	HWND GetHWND() { return m_hWnd; }

private:
	// Window state
	HWND m_hWnd;
	HWND m_hDialogWnd;

	// Spotify
	SpotifyPlayer *m_lpSpotify;
	HANDLE m_hSpotifyEvent;

	// Visualizer
	Visualizer *m_pVisualizer;

private:
    // Resize the render target.
    void OnResize(
        UINT width,
        UINT height
        );

	void Connect(LPCSTR szUsername, LPCSTR szPassword);

	void SubclassControl(HWND hControl);

    // The windows procedure.
    static LRESULT CALLBACK WndProc(
        HWND hWnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam
        );

    // The windows procedure.
    static LRESULT CALLBACK DialogProc(
        HWND hWnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam
        );

    static LRESULT CALLBACK TabControlProc(
        HWND hWnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam
        );
};
