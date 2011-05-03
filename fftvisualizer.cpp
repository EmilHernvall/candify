#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include "audio.h"
#include "fftvisualizer.h"
#include "complex.h"
#include "fft.h"

FFTVisualizer::FFTVisualizer()
: Visualizer()
{
}

FFTVisualizer::~FFTVisualizer()
{
	Visualizer::~Visualizer();
}

HRESULT FFTVisualizer::Render()
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

		if (m_dwCurrentPos > 2*width) {
			m_dwCurrentPos = 0;
		}

		ID2D1RenderTarget *pTarget = NULL;
		if (m_dwCurrentPos == 0) {
			ID2D1SolidColorBrush *pBlackBrush;
			m_pBuffer1->CreateSolidColorBrush(
				D2D1::ColorF(D2D1::ColorF::Black, 1.0),
				&pBlackBrush);

			m_pBuffer1->FillRectangle(D2D1::RectF(0.0f, 0.0f, (float)width, (float)height), pBlackBrush);

			SafeRelease(&pBlackBrush);
		}
		else if (m_dwCurrentPos == width) {
			ID2D1SolidColorBrush *pBlackBrush;
			m_pBuffer2->CreateSolidColorBrush(
				D2D1::ColorF(D2D1::ColorF::Black, 1.0),
				&pBlackBrush);

			m_pBuffer2->FillRectangle(D2D1::RectF(0.0f, 0.0f, (float)width, (float)height), pBlackBrush);

			SafeRelease(&pBlackBrush);
		}

		if (m_dwCurrentPos < width) {
			pTarget = m_pBuffer1;
		}
		else {
			pTarget = m_pBuffer2;
		}

		apt = m_pCurrentAfd;
		if (apt == NULL) {
			goto enddraw;
		}

		int skipped = apt->seqid - m_dwLastSeq - 1;
		if (skipped > 0) {
			m_dwSkipped += skipped;
		}
		m_dwLastSeq = apt->seqid;

		complex *data1 = new complex[apt->nframes];
		complex *data2 = new complex[apt->nframes];
		for (int i = 0; i < apt->nframes; i++) {
			data1[i] = complex::complex(apt->frames[i*2]);
			data2[i] = complex::complex(apt->frames[i*2+1]);
		}

		CFFT::Forward(data1, apt->nframes);
		CFFT::Forward(data2, apt->nframes);
		float normR1 = 0.0f, normR2 = 0.0f;
		for (int i = 0; i < apt->nframes; i++) {
			float re1, re2;
			re1 = (float)data1[i].re();
			re2 = (float)data2[i].re();

			if (mabs(re1) > normR1) {
				normR1 = mabs(re1);
			}
			if (mabs(re2) > normR2) {
				normR2 = mabs(re2);
			}
		}

		pTarget->BeginDraw();

		ID2D1SolidColorBrush *pBrush;
		pTarget->CreateSolidColorBrush(
			D2D1::ColorF(0xFF, 0xFF, 0xFF),
			&pBrush);

		int freqs = apt->nframes;
		int framesPerPixel = 2 * freqs / height + 1;
		float re1 = 0.0f, re2 = 0.0f;
		for (int i = 0; i < freqs; i++) {
			re1 += (float)data1[i].re() / normR1;
			re2 += (float)data2[i].re() / normR2;

			if (i % framesPerPixel == 0) {
				re1 /= framesPerPixel;
				re2 /= framesPerPixel;

				re1 *= 0xFF;
				re2 *= 0xFF;

				pBrush->SetColor(D2D1::ColorF(re1, re1, re1));

				pTarget->DrawLine(
					D2D1::Point2F((float)(m_dwCurrentPos % width), (float)(i/framesPerPixel)), 
					D2D1::Point2F((float)(m_dwCurrentPos % width + 1), (float)(i/framesPerPixel)), 
					pBrush);

				pBrush->SetColor(D2D1::ColorF(re2, re2, re2));

				pTarget->DrawLine(
					D2D1::Point2F((float)(m_dwCurrentPos % width), (float)(height/2 + 1 + i/framesPerPixel)), 
					D2D1::Point2F((float)(m_dwCurrentPos % width + 1), (float)(height/2 + 1 + i/framesPerPixel)), 
					pBrush);

				re1 = 0.0;
				re2 = 0.0;
			}
		}

		SafeRelease(&pBrush);

		pTarget->EndDraw();

		delete[] data1;
		delete[] data2;

		m_pRenderTarget->BeginDraw();

		float newPos;
		if (m_dwCurrentPos < width) {
			ID2D1Bitmap *pBitmap1, *pBitmap2;

			m_pBuffer1->GetBitmap(&pBitmap1);
			m_pBuffer2->GetBitmap(&pBitmap2);

			newPos = (float)m_dwCurrentPos;

			m_pRenderTarget->DrawBitmap(pBitmap2, 
				D2D1::RectF(-newPos, 0.0f, (float)width - newPos, (float)height));
			m_pRenderTarget->DrawBitmap(pBitmap1, 
				D2D1::RectF(width - newPos, 0.0f, 2.0f*(float)width - newPos, (float)height));

			SafeRelease(&pBitmap1);
			SafeRelease(&pBitmap2);
		}
		else {
			ID2D1Bitmap *pBitmap1, *pBitmap2;

			m_pBuffer1->GetBitmap(&pBitmap1);
			m_pBuffer2->GetBitmap(&pBitmap2);

			newPos = (float)(m_dwCurrentPos - width);

			m_pRenderTarget->DrawBitmap(pBitmap1, 
				D2D1::RectF(-newPos, 0.0f, (float)width - newPos, (float)height));
			m_pRenderTarget->DrawBitmap(pBitmap2, 
				D2D1::RectF(width - newPos, 0.0f, 2.0f*(float)width - newPos, (float)height));
			
			SafeRelease(&pBitmap1);
			SafeRelease(&pBitmap2);
		}

		m_dwCurrentPos += 4;

		enddraw:
			hr = m_pRenderTarget->EndDraw();

		//Sleep(100);
	}

    if (hr == D2DERR_RECREATE_TARGET) {
		DiscardDeviceResources();
    }

	return hr;
}
