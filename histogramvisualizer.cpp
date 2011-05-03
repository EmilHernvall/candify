#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <math.h>
#include <string>
#include <sstream>
#include "audio.h"
#include "spotifyplayer.h"
#include "histogramvisualizer.h"
#include "complex.h"
#include "fft.h"

// Calculate the a value using a quadratic formula
// with root at l and u, and a maximum value of max.
float f(float v, float l, float u, float max)
{
    float m = (l + u) / 2.0f;
    float k = max / ((l - m) * (u - m));
    return max(0, k * (l - v) * (u - v));
}

// Return the maximum value if the requested coordinate
// is within the range [l, u] and 0 if otherwise.
float g(float v, float l, float u, float max)
{
        if (v >= l && v <= u) {
                return max;
        }

        return 0.0f;
}


HistogramVisualizer::HistogramVisualizer()
: Visualizer(), m_pPrevImage(NULL), m_iColorSeq(0),
  m_pDirectWriteFactory(NULL), m_pTextFormat(NULL)
{
	m_pAvgFreqDomain = new float[FFT_SIZE];
	for (int i = 0; i < FFT_SIZE; i++) {
		m_pAvgFreqDomain[i] = 0.0f;
	}

	m_fNorm = 0.0f;
}

HistogramVisualizer::~HistogramVisualizer()
{
	SafeRelease(&m_pTextFormat);
	SafeRelease(&m_pDirectWriteFactory);
	Visualizer::~Visualizer();
}

HRESULT HistogramVisualizer::CreateDeviceIndependentResources()
{
	HRESULT hr;
	
	hr = Visualizer::CreateDeviceIndependentResources();
	if (FAILED(hr)) {
		return hr;
	}

	hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
		__uuidof(IDWriteFactory),
		reinterpret_cast<IUnknown**>(&m_pDirectWriteFactory));
	if (FAILED(hr)) {
		return hr;
	}

	hr = m_pDirectWriteFactory->CreateTextFormat(
		L"Georgia",
		NULL,
		DWRITE_FONT_WEIGHT_REGULAR,
		DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL,
		48.0f,
		L"en-us",
		&m_pTextFormat);

	m_pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);

	return hr;
}

// Return a rgb color based on an integer counter.
INT HistogramVisualizer::getColor(float i, float max)
{
	float segment = max / 6.0f;

    float bc = g(i, 1 * segment, 3 * segment, 0xFF)
       + g(i, 0 * segment, 1 * segment, f(i, 0 * segment, 2 * segment, 0xFF))
       + g(i, 3 * segment, 4 * segment, f(i, 2 * segment, 4 * segment, 0xFF));
    float gc = g(i, 3 * segment, 5 * segment, 0xFF)
       + g(i, 2 * segment, 3 * segment, f(i, 2 * segment, 4 * segment, 0xFF))
       + g(i, 5 * segment, 6 * segment, f(i, 4 * segment, 6 * segment, 0xFF));
    float rc = g(i, 0 * segment, 1 * segment, 0xFF)
       + g(i, 1 * segment, 2 * segment, f(i, 0 * segment, 2 * segment, 0xFF))
       + g(i, 5 * segment, 6 * segment, 0xFF)
       + g(i, 4 * segment, 5 * segment, f(i, 4 * segment, 6 * segment, 0xFF));

	int res = 
		(((int)bc & 0xFF) << 16) |
		(((int)gc & 0xFF) << 8) |
		((int)rc & 0xFF);

	return res;
}

HRESULT HistogramVisualizer::Render()
{
	HRESULT hr;
	RECT rc;
	audio_fifo_data_t *apt;
	int width, height;

	if (!m_pRenderTarget) {
		hr = CreateDeviceResources();
	} else {
		hr = S_OK;
	}

	if (SUCCEEDED(hr)) {

		GetClientRect(m_hWnd, &rc);

		width = rc.right - rc.left;
		height = rc.bottom - rc.top;

		apt = m_pCurrentAfd;
		if (apt == NULL) {
			goto enddraw;
		}

		m_dwLastSeq = apt->seqid;

		// Setup for the fourier transform
		complex *data1 = new complex[apt->nframes];
		for (int i = 0; i < apt->nframes; i++) {
			data1[i] = complex::complex(apt->frames[i*2]);
		}

		// Calculate the FFT of the packet piece by piece. Use an
		// exponential moving average with a weight factor alpha
		// to average the results together.
		int pos = 0;
		float alpha = 0.1f;
		while (pos < apt->nframes) {
			// Take the transform of FFT_SIZE values if possible, or
			// whatever remains otherwise.
			int fftSize = apt->nframes > FFT_SIZE + pos ? FFT_SIZE : apt->nframes - pos;

			CFFT::Forward(&data1[pos], fftSize);

			// Calculate the EMA
			for (int i = 0; i < FFT_SIZE; i++) {
				float re;
				if (i > fftSize) {
					re = 0.0f;
				} else {
					re = (float)data1[pos+i].re();
				}

				m_pAvgFreqDomain[i] = re * alpha + (1 - alpha) * m_pAvgFreqDomain[i];
			}

			pos += FFT_SIZE;
		}

		delete[] data1;

		// Calculate a norm for the current frequency domain
		float norm = 0.0f;
		for (int i = 10; i < FFT_SIZE; i++) {
			float re = m_pAvgFreqDomain[i];
			if (re > norm) {
				norm = re;
			}
		}

		// Use the exponential moving average again to even
		// out sudden changes.
		m_fNorm = alpha * norm + (1 - alpha) * m_fNorm;

		// Retrieve the song info from the spotify object
		std::wstringstream songInfo;
		LPSTR szArtist, szTrackName;
		szArtist = m_lpSpotify->getCurrentTrackArtist();
		szTrackName = m_lpSpotify->getCurrentTrackName();

		if (szArtist != NULL && szTrackName != NULL) {
			songInfo << szArtist << " - " << szTrackName;

			free(szArtist);
			free(szTrackName);
		}

		std::wstring strSongInfo = songInfo.str();

		// Start drawing onto cached image
		ID2D1BitmapRenderTarget *pNewImage;
		{
			m_pRenderTarget->CreateCompatibleRenderTarget(&pNewImage);

			pNewImage->BeginDraw();

			// Scale the previous visualization frame to
			// a fraction of the real image size, and
			// draw it to the center of the new image
			// to create an effect of "flying over" 
			// the histogram
			if (m_pPrevImage) {
				ID2D1Bitmap *pBitmap;

				m_pPrevImage->GetBitmap(&pBitmap);

				float diff = 0.98f;
				float left = width - width * diff;
				float right = width * diff;
				float top = height - height * diff;
				float bottom = height * diff;
				pNewImage->DrawBitmap(pBitmap, 
					D2D1::RectF(left, top, right, bottom), 
					0.9f, 
					D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
					D2D1::RectF(0.0f, 0.0f, (float)width, (float)height));

				SafeRelease(&pBitmap);
				SafeRelease(&m_pPrevImage);
			}

			hr = pNewImage->Flush(NULL, NULL);

			// Allocate a brush with a color based on the current
			// position in the color sequence.
			int color = getColor((float)m_iColorSeq, 100.0f);

			ID2D1SolidColorBrush *pBrush;
			pNewImage->CreateSolidColorBrush(
				D2D1::ColorF(color),
				&pBrush);

			// Draw histogram based on the current frequency domain
			float re = 0.0f;
			int magnitude;
			int pillarWidth = width / FFT_SIZE + 1;
			for (int i = 10; (i+1)*pillarWidth < width - pillarWidth*10; i++) {
				magnitude = (int)(m_pAvgFreqDomain[i] / m_fNorm * height);

				pNewImage->FillRectangle(
					D2D1::RectF((float)(i*pillarWidth), (float)height, (float)((i + 1) *pillarWidth), (float)(height - magnitude)), pBrush);
			}

			// Draw artist and track name
			pNewImage->DrawTextA(strSongInfo.c_str(), strSongInfo.length(), m_pTextFormat, 
				D2D1::RectF(0.0f, (float)height / 6.0f, (float)width, (float)height / 3.0f), pBrush);

			SafeRelease(&pBrush);

			pNewImage->EndDraw();
		}

		m_pPrevImage = pNewImage;

		// Draw it all on to the window
		{
			m_pRenderTarget->BeginDraw();

			// Clear drawing area
			ID2D1SolidColorBrush *pBrush;
			m_pRenderTarget->CreateSolidColorBrush(
				D2D1::ColorF(D2D1::ColorF::Black, 1.0),
				&pBrush);

			m_pRenderTarget->FillRectangle(D2D1::RectF(0.0f, 0.0f, (float)width, (float)height), pBrush);

			// Draw the current visualization frame onto the surface
			ID2D1Bitmap *pBitmap;
			pNewImage->GetBitmap(&pBitmap);

			m_pRenderTarget->DrawBitmap(pBitmap, D2D1::RectF(0.0f, 0.0f, (float)width, (float)height));

			// Draw the song info again, but this time in white, to make it easier to read.
			pBrush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
			m_pRenderTarget->DrawTextA(strSongInfo.c_str(), strSongInfo.length(), m_pTextFormat, 
				D2D1::RectF(0.0f, (float)height / 6.0f, (float)width, (float)height / 3.0f), pBrush);

			SafeRelease(&pBitmap);
			SafeRelease(&pBrush);

			m_pRenderTarget->EndDraw();
		}

		if (++m_iColorSeq > 100) {
			m_iColorSeq = 0;
		}

		//Sleep(100);
	}

	enddraw:
    if (hr == D2DERR_RECREATE_TARGET) {
		DiscardDeviceResources();
    }

	return hr;
}
