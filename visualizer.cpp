#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include "audio.h"
#include "visualizer.h"

Visualizer::Visualizer()
: m_hWnd(NULL), m_pCurrentAfd(NULL), m_pDirect2dFactory(NULL), m_pRenderTarget(NULL), m_pBuffer1(NULL), 
  m_pBuffer2(NULL)
{
}

Visualizer::~Visualizer()
{
	DiscardDeviceResources();
	SafeRelease(&m_pDirect2dFactory);
}

HRESULT Visualizer::CreateDeviceIndependentResources()
{
	HRESULT hr;
    // Create a Direct2D factory.
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pDirect2dFactory);

	return hr;
}

HRESULT Visualizer::CreateDeviceResources()
{
	HRESULT hr;
	RECT rc;
	int width, height;

	GetClientRect(m_hWnd, &rc);
	width = rc.right - rc.left;
	height = rc.bottom - rc.top;

	D2D1_SIZE_U size = D2D1::SizeU(width, height);

	// Create a Direct2D render target.
	hr = m_pDirect2dFactory->CreateHwndRenderTarget(
		D2D1::RenderTargetProperties(),
		D2D1::HwndRenderTargetProperties(m_hWnd, size),
		&m_pRenderTarget);
	if (FAILED(hr)) {
		return hr;
	}

	hr = m_pRenderTarget->CreateCompatibleRenderTarget(&m_pBuffer1);
	if (FAILED(hr)) {
		return hr;
	}

	hr = m_pRenderTarget->CreateCompatibleRenderTarget(&m_pBuffer2);
	if (FAILED(hr)) {
		return hr;
	}

	return hr;
}

VOID Visualizer::DiscardDeviceResources()
{
	SafeRelease(&m_pBuffer1);
	SafeRelease(&m_pBuffer2);
	SafeRelease(&m_pRenderTarget);
}
