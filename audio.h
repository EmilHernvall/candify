#pragma once

#include "util.h"

#pragma warning ( disable : 4200 )
typedef struct audio_fifo_data_t_ {
	audio_fifo_data_t_ *next;
	int nframes;
	int rate;
	int channels;
	int seqid;
	uint16_t frames[];
} audio_fifo_data_t;

typedef struct {
	HANDLE hMutex;
	HANDLE hEvent;
	HWND hNotifyWnd;
	int bInitialized;
	int bFormatSet;
	audio_fifo_data_t *fifo_head;
	audio_fifo_data_t *fifo_tail;
	int framecount;
	int packetcount;
	int seq;
} audio_fifo_t;

void audio_init(audio_fifo_t *fifo, HWND hNotifyWnd);
int audio_available_frames(audio_fifo_t *fifo);
void audio_fifo_push(audio_fifo_t *fifo, audio_fifo_data_t *afd);
audio_fifo_data_t *audio_fifo_peek(audio_fifo_t *fifo);
void audio_fifo_flush(audio_fifo_t *fifo);
