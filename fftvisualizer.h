#pragma once

#include "visualizer.h"

class FFTVisualizer : public Visualizer
{
public:
	FFTVisualizer();
	~FFTVisualizer();
	virtual HRESULT Render();

private:
	DWORD m_dwCurrentPos;
	DWORD m_dwSkipped;
	DWORD m_dwLastSeq;
};
