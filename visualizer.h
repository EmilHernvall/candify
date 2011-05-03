#pragma once

class SpotifyPlayer;

class Visualizer
{
public:
	Visualizer();
	~Visualizer();

	virtual HRESULT Render() = 0;

	void SetSpotifyPlayer(SpotifyPlayer *lpSpotify)
	{
		m_lpSpotify = lpSpotify;
	}

	void SetHWND(HWND hWnd)
	{
		m_hWnd = hWnd;
	}

	void SetAudioData(audio_fifo_data_t *pAfd)
	{
		m_pCurrentAfd = pAfd;
	}

    // Initialize device-independent resources.
    virtual HRESULT CreateDeviceIndependentResources();

    // Release device-dependent resource. Forces reallocation of buffers
	// during the next attempt to draw.
    void DiscardDeviceResources();

protected:
	HWND m_hWnd;

	// Player
	SpotifyPlayer *m_lpSpotify;

	// Direct2D
	ID2D1Factory *m_pDirect2dFactory;
	ID2D1HwndRenderTarget *m_pRenderTarget;
	ID2D1BitmapRenderTarget *m_pBuffer1;
	ID2D1BitmapRenderTarget *m_pBuffer2;

	// Drawing context
	audio_fifo_data_t *m_pCurrentAfd;

protected:
    // Initialize device-dependent resources.
    HRESULT CreateDeviceResources();
};
