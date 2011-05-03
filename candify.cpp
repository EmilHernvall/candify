#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <Windowsx.h>
#include <Commctrl.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <math.h>

#include "resource.h"
#include "util.h"
#include "audio.h"
#include "candify.h"
#include "spotifyplayer.h"
#include "visualizer.h"
#include "fftvisualizer.h"
#include "histogramvisualizer.h"

#define WM_DLGLOGGEDIN (WM_USER+1)
#define WM_DLGUPDATEPLAYLISTS (WM_USER+2)
#define WM_DLGLOGINFAILED (WM_USER+3)


Candify::Candify(SpotifyPlayer *lpSpotify, Visualizer *pVisualizer, HANDLE hSpotifyEvent)
: m_hWnd(NULL), m_lpSpotify(lpSpotify), m_pVisualizer(pVisualizer), m_hSpotifyEvent(hSpotifyEvent)
{
}

Candify::~Candify()
{
}

VOID Candify::RunMessageLoop()
{
	DWORD dwEvent;
	HANDLE hEvents[2];
	BOOL bConnectionState;
    int next_timeout = 0;
	int iPlaylistCount = 0;

	hEvents[0] = m_hSpotifyEvent;
	hEvents[1] = m_hWnd;

	// Handle events
	bConnectionState = FALSE;
    while (TRUE) {
		dwEvent = MsgWaitForMultipleObjectsEx(2, hEvents, FALSE, next_timeout > 0 ? next_timeout : INFINITE, 0xFFFF);

		next_timeout = m_lpSpotify->handleEvents();

		if (!bConnectionState && m_lpSpotify->isConnected()) {
			PostMessage(m_hDialogWnd, WM_DLGLOGGEDIN, 0, 0);
			bConnectionState = TRUE;
		}

		if (m_lpSpotify->hasFailedLogin()) {
			PostMessage(m_hDialogWnd, WM_DLGLOGINFAILED, 0, 0);
		}

		if (m_lpSpotify->hasNewPlaylists()) {
			PostMessage(m_hDialogWnd, WM_DLGUPDATEPLAYLISTS, 0, 0);
		}

		MSG msg;
		while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)){ 
			if (msg.message == WM_QUIT) {
				return;
			}
			if (IsWindow(m_hDialogWnd) && IsDialogMessage(m_hDialogWnd, &msg)) {
				continue;
			}
			TranslateMessage(&msg); 
			DispatchMessage(&msg); 
		}
    }
}

LRESULT CALLBACK Candify::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	LRESULT result = 0;
    if (message == WM_CREATE) {
        LPCREATESTRUCT pcs = (LPCREATESTRUCT)lParam;
        Candify *pApp = (Candify *)pcs->lpCreateParams;

        SetWindowLongPtr(hWnd, GWLP_USERDATA,PtrToUlong(pApp));

        result = 1;
    }
    else {
        Candify *pApp = reinterpret_cast<Candify*>(static_cast<LONG_PTR>(
            ::GetWindowLongPtrW(hWnd, GWLP_USERDATA)));

        bool wasHandled = false;

		switch (message)
		{
			case WM_USER:
				pApp->m_pVisualizer->SetAudioData((audio_fifo_data_t*)wParam);
				InvalidateRect(hWnd, NULL, FALSE);
                result = 0;
                wasHandled = true;
                break;
			case WM_KEYDOWN:
				// Look for the key N
				if (wParam == 0x4E) {
					pApp->m_lpSpotify->nextSong();
				}
				break;
			case WM_TIMER:
				InvalidateRect(hWnd, NULL, FALSE);
                result = 0;
                wasHandled = true;
                break;
            case WM_DISPLAYCHANGE:
                {
                    InvalidateRect(hWnd, NULL, FALSE);
                }
                result = 0;
                wasHandled = true;
                break;
			case WM_SIZE:
				{
					pApp->m_pVisualizer->DiscardDeviceResources();
				}
				result = 0;
				wasHandled = true;
				break;
            case WM_PAINT:
                {
                    pApp->m_pVisualizer->Render();
                    ValidateRect(hWnd, NULL);
                }
                result = 0;
                wasHandled = true;
                break;
            case WM_DESTROY:
                {
					PostQuitMessage(0);
                }
                result = 1;
                wasHandled = true;
                break;

		}

        if (!wasHandled) {
            result = DefWindowProc(hWnd, message, wParam, lParam);
        }
    }

    return result;
}

LRESULT CALLBACK Candify::DialogProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	Candify *pApp;

	pApp = (Candify*)GetWindowLong(hWnd, GWL_USERDATA);

	switch (message) {
	case WM_INITDIALOG: 
		{
			HWND hUsernameEdit, hPasswordEdit, hConnectButton, hExitButton;
			HWND hPlaylistCombo, hPlayButton;
			
			hUsernameEdit = GetDlgItem(hWnd, IDC_USERNAMEEDIT);
			hPasswordEdit = GetDlgItem(hWnd, IDC_PASSWORDEDIT);
			hConnectButton = GetDlgItem(hWnd, IDC_CONNECTBUTTON);
			hExitButton = GetDlgItem(hWnd, IDC_EXITBUTTON);
			hPlaylistCombo = GetDlgItem(hWnd, IDC_PLAYLISTCOMBO);
			hPlayButton = GetDlgItem(hWnd, IDC_PLAYBUTTON);

			pApp->SubclassControl(hUsernameEdit);
			pApp->SubclassControl(hPasswordEdit);
			pApp->SubclassControl(hConnectButton);
			pApp->SubclassControl(hExitButton);
			pApp->SubclassControl(hPlaylistCombo);
			pApp->SubclassControl(hPlayButton);

			ComboBox_SetMinVisible(hPlaylistCombo, 5);

			SetFocus(hUsernameEdit);
		}
		break;
	case WM_DLGLOGGEDIN:
		{
			HWND hUsernameEdit, hPasswordEdit, hConnectButton;
			hUsernameEdit = GetDlgItem(hWnd, IDC_USERNAMEEDIT);
			hPasswordEdit = GetDlgItem(hWnd, IDC_PASSWORDEDIT);
			hConnectButton = GetDlgItem(hWnd, IDC_CONNECTBUTTON);

			Edit_Enable(hUsernameEdit, FALSE);
			Edit_Enable(hPasswordEdit, FALSE);
			Button_Enable(hConnectButton, FALSE);
		}
		break;
	case WM_DLGUPDATEPLAYLISTS:
		{
			HWND hPlaylistCombo, hPlayButton;
			LPCSTR *playlists;
			int nPlaylists;

			hPlaylistCombo = GetDlgItem(hWnd, IDC_PLAYLISTCOMBO);
			hPlayButton = GetDlgItem(hWnd, IDC_PLAYBUTTON);

			ComboBox_Enable(hPlaylistCombo, TRUE);
			Button_Enable(hPlayButton, TRUE);

			ComboBox_ResetContent(hPlaylistCombo);

			nPlaylists = pApp->m_lpSpotify->getPlaylists(&playlists);
			for (int i = 0; i < nPlaylists; i++) {
				ComboBox_AddString(hPlaylistCombo, playlists[i]);
				free((LPVOID)playlists[i]);
			}
			delete[] playlists;
		}
		break;
	case WM_DLGLOGINFAILED:
		{
			HWND hConnectButton;

			MessageBox(hWnd, "Login failed!", "Candify", MB_ICONINFORMATION);

			hConnectButton = GetDlgItem(hWnd, IDC_CONNECTBUTTON);
			Button_Enable(hConnectButton, true);
		}
		break;
	case WM_COMMAND:
		{
			switch (LOWORD(wParam)) {
			case IDC_EXITBUTTON:
				{
					PostQuitMessage(0);
				}
				break;
			case IDC_CONNECTBUTTON:
				{
					HWND hConnectButton;
					CHAR szUsername[100], szPassword[100];

					hConnectButton = GetDlgItem(hWnd, IDC_CONNECTBUTTON);
					Button_Enable(hConnectButton, false);

					GetDlgItemText(hWnd, IDC_USERNAMEEDIT, szUsername, 
						sizeof(szUsername)/sizeof(*szUsername));
					GetDlgItemText(hWnd, IDC_PASSWORDEDIT, szPassword, 
						sizeof(szPassword)/sizeof(*szPassword));
					pApp->Connect(szUsername, szPassword);
				}
				break;
			case IDC_PLAYBUTTON:
				{
					CHAR szPlaylist[100];
					GetDlgItemText(hWnd, IDC_PLAYLISTCOMBO, 
						szPlaylist, sizeof(szPlaylist)/sizeof(*szPlaylist));
					pApp->m_lpSpotify->setPlaylist(szPlaylist);
					ShowWindow(pApp->m_hWnd, SW_SHOW);
					EndDialog(hWnd, 0);
				}
				break;
			}
		}
		break;
	}

	return FALSE;
}

LRESULT CALLBACK Candify::TabControlProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	WNDPROC proc = (WNDPROC)GetWindowLong(hWnd, GWL_USERDATA);

	switch (message)
	{
	case WM_KEYDOWN:
		{
			if (wParam == VK_TAB) {
				HWND hParent, hNextCtrl;

				hParent = GetParent(hWnd);
				hNextCtrl = GetNextDlgTabItem(hParent, hWnd, FALSE);
				SetFocus(hNextCtrl);
				return 0;
			}
		}
	}

	return proc(hWnd, message, wParam, lParam);
}

void Candify::SubclassControl(HWND hControl)
{
	WNDPROC currentProc;

	currentProc = (WNDPROC)GetWindowLong(hControl, GWL_WNDPROC);

	SetWindowLong(hControl, GWL_USERDATA, (LONG)currentProc);
	SetWindowLong(hControl, GWL_WNDPROC, (LONG)&Candify::TabControlProc);
}

void Candify::Connect(LPCSTR szUsername, LPCSTR szPassword)
{
	if (!m_lpSpotify->connect(szUsername, szPassword)) {
		MessageBox(m_hWnd, "Connection failed!", "Candify", MB_ICONEXCLAMATION);
	}
}

HRESULT Candify::Initialize()
{
	WNDCLASSEX wcex;
	DWORD dwStyle;
	HRESULT hr;

	hr = m_pVisualizer->CreateDeviceIndependentResources();
	if (FAILED(hr)) {
		return FALSE;
	}

	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra	= 0;
	wcex.cbWndExtra	= 0;
	wcex.hInstance = HINST;
	wcex.hIcon = NULL;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW);
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = CANDIFY_WNDCLASS;
	wcex.hIconSm = NULL;

	if (!RegisterClassEx(&wcex)) {
		return E_FAIL;
	}

	dwStyle = WS_OVERLAPPEDWINDOW; // & ~WS_SIZEBOX & ~WS_MAXIMIZEBOX & ~WS_MINIMIZEBOX;

	m_hWnd = CreateWindowEx(0, CANDIFY_WNDCLASS, CANDIFY_TITLE, dwStyle,
		CW_USEDEFAULT, 0, 1000, 400, NULL, NULL, HINST, this);

	m_pVisualizer->SetHWND(m_hWnd);

	//ShowWindow(m_hWnd, 1);

	// Create control dialog
	m_hDialogWnd = CreateDialog(HINST, MAKEINTRESOURCE(IDD_CONNECTDIALOG), 
		NULL, (DLGPROC)&Candify::DialogProc);
	SetWindowLongPtrA(m_hDialogWnd, GWL_USERDATA, (LONG_PTR)this);
	ShowWindow(m_hDialogWnd, SW_SHOW);

	return S_OK;
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
	LPSTR lpCmdLine, int nCmdShow)
{
	SpotifyPlayer *sp;
	HANDLE hSpotifyEvent;
	audio_fifo_t audioFifo;
	Visualizer *pVisualizer;

	// Setup an event to use for notification
	hSpotifyEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (hSpotifyEvent == NULL) {
        return 0;
    }

	// Connect to spotify
	sp = new SpotifyPlayer(&audioFifo, hSpotifyEvent);

	// Create visualizer
	pVisualizer = new HistogramVisualizer();
	pVisualizer->SetSpotifyPlayer(sp);

	// Create window and run messageloop
	Candify app(sp, pVisualizer, hSpotifyEvent);

	if (FAILED(app.Initialize())) {
		MessageBox(NULL, "Application failed to initialize.", "Candify", MB_ICONEXCLAMATION);
		return 0;
	}

	// Initialize sound
	audio_init(&audioFifo, app.GetHWND());

	app.RunMessageLoop();

    return 0;
}
