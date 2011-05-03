#pragma once

#include "visualizer.h"

#define FFT_SIZE 256

class HistogramVisualizer : public Visualizer
{
public:
	HistogramVisualizer();
	~HistogramVisualizer();
	virtual HRESULT Render();
	virtual HRESULT CreateDeviceIndependentResources();

private:
	int getColor(float v, float max);

private:
	// DirectWrite
	IDWriteFactory *m_pDirectWriteFactory;
	IDWriteTextFormat *m_pTextFormat;

	ID2D1BitmapRenderTarget *m_pPrevImage;

	int m_iColorSeq;
	float *m_pAvgFreqDomain;
	float m_fNorm;

	DWORD m_dwCurrentPos;
	DWORD m_dwSkipped;
	DWORD m_dwLastSeq;
};
