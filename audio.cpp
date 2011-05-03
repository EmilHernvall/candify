#include <windows.h>
#include <stdlib.h>
#include "candify.h"
#include "audio.h"

#define BUFFER_COUNT 8

/**
 * Pop the first package from the fifo.
 */
static audio_fifo_data_t *audio_fifo_pop(audio_fifo_t *fifo)
{
	audio_fifo_data_t *afd = NULL;

	WaitForSingleObject(fifo->hMutex, INFINITE);

	afd = fifo->fifo_head;
	if (afd != NULL) {
		fifo->fifo_head = afd->next;
		fifo->framecount -= afd->nframes;
		fifo->packetcount--;
	}

	ReleaseMutex(fifo->hMutex);
	return afd;
}

audio_fifo_data_t *audio_fifo_peek(audio_fifo_t *fifo)
{
	audio_fifo_data_t *afd = NULL;

	WaitForSingleObject(fifo->hMutex, INFINITE);
	afd = fifo->fifo_head;
	ReleaseMutex(fifo->hMutex);

	return afd;
}

/**
 * The playback thread.
 */
static void audio_main_thread(audio_fifo_t *fifo)
{
    HWAVEOUT hWaveOut; 
    WAVEHDR waveHdr[BUFFER_COUNT]; 
	audio_fifo_data_t *afd[BUFFER_COUNT];
    WAVEFORMATEX format;
	HANDLE hWaveEvent;

	// Create an event to use with the waveOut-api. Notifications
	// are sent here when playback of a buffer has finished.
	hWaveEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

	// We have to keep track of the packages until they have
	// finished playing. The number of packages we need to
	// keep at any given time is of course equal to the
	// number of buffers.
	memset(afd, 0, sizeof(audio_fifo_data_t*) * BUFFER_COUNT);

	while (true) {
		// Wait for an event indicating that new data is available
		// (for the most part)
		WaitForSingleObject(fifo->hEvent, INFINITE);

		// Double check that there really is data available,
		// and return to waiting if there isn't
		if (audio_available_frames(fifo) == 0) {
			continue;
		}

		// Retrieve a package in advance so that
		// we can set up the wave format.
		afd[0] = audio_fifo_pop(fifo);

		format.cbSize = sizeof(WAVEFORMATEX);
		format.nBlockAlign = 4;
		format.nChannels = afd[0]->channels;
		format.nSamplesPerSec = afd[0]->rate;
		format.wBitsPerSample = 16;
		format.wFormatTag = WAVE_FORMAT_PCM;
		format.nAvgBytesPerSec = format.nChannels * format.nSamplesPerSec * format.wBitsPerSample / 8;

		// Initialize the sound card
		if (waveOutOpen((LPHWAVEOUT)&hWaveOut, WAVE_MAPPER, (LPWAVEFORMATEX)&format, 
			(DWORD_PTR)hWaveEvent, 0L, CALLBACK_EVENT)) { 
			
			MessageBox(NULL, "Failed to initialize sound card.", "Candify", MB_ICONEXCLAMATION);
			return;
		}

		// Initialize all the buffers
		for (int i = 0; i < BUFFER_COUNT; i++) {

			// This is just here so that we don't loose the packet
			// that we've already retrieved.
			if (afd[i] == NULL) {
				afd[i] = audio_fifo_pop(fifo);
			}

			memset(&waveHdr[i], 0, sizeof(WAVEHDR));
			waveHdr[i].lpData = (LPSTR)afd[i]->frames;
			waveHdr[i].dwBufferLength = afd[i]->channels * afd[i]->nframes * 2;

			waveOutPrepareHeader(hWaveOut, &waveHdr[i], sizeof(WAVEHDR));
			waveOutWrite(hWaveOut, &waveHdr[i], sizeof(WAVEHDR));
		}

		// Inner playback loop. We only exit this loop when the wave format
		// has changed.
		do {
			// Wait for a notification that a buffer has finished playing.
			WaitForSingleObject(hWaveEvent, INFINITE);

			// Loop until we find an empty buffer.
			for (int i = 0; i < BUFFER_COUNT; i++) {
				if ((waveHdr[i].dwFlags & WHDR_DONE) == 0) {
					continue;
				}

				// Clean up from last run
				waveOutUnprepareHeader(hWaveOut, &waveHdr[i], sizeof(WAVEHDR));
				free(afd[i]);
				
				// Start all over again. First we make sure that there is
				// a package ready. If not, we wait for that too.
				while (audio_available_frames(fifo) == 0) {
					WaitForSingleObject(fifo->hEvent, INFINITE);
				}

				afd[i] = audio_fifo_pop(fifo);

				if (fifo->hNotifyWnd != NULL) {
					PostMessage(fifo->hNotifyWnd, WM_USER, (WPARAM)afd[i], 0);
				}

				memset(&waveHdr[i], 0, sizeof(WAVEHDR));
				waveHdr[i].lpData = (LPSTR)afd[i]->frames;
				waveHdr[i].dwBufferLength = afd[i]->channels * afd[i]->nframes * 2;

				waveOutPrepareHeader(hWaveOut, &waveHdr[i], sizeof(WAVEHDR));
				waveOutWrite(hWaveOut, &waveHdr[i], sizeof(WAVEHDR));
			}

			// printf("%d, %d\n", fifo->packagecount, fifo->framecount);

		} while (true);

		// Cleanup
		waveOutClose(hWaveOut);
	}
}

/**
 * Initialize audio. Set up structures and create the playback thread.
 */
void audio_init(audio_fifo_t *fifo, HWND hNotifyWnd)
{
	HANDLE hThread;
	DWORD dwThreadId;

	fifo->bInitialized = 1;
	fifo->bFormatSet = 0;
	fifo->fifo_tail = NULL;
	fifo->fifo_head = NULL;
	fifo->framecount = 0;
	fifo->packetcount = 0;
	fifo->seq = 0;

	// This is used to synchronize the audio fifo object
	fifo->hMutex = CreateMutex(NULL, FALSE, NULL);

	// This is used to notify the playback thread when
	// there's new data available.
	fifo->hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

	// Used to notify an window whenever a new packet
	// is written to buffer.
	fifo->hNotifyWnd = hNotifyWnd;

	hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)audio_main_thread, 
		fifo, 0, &dwThreadId);
}

/**
 * The number of queued packages.
 */
int audio_available_frames(audio_fifo_t *fifo)
{
	DWORD dwAvailable;

	WaitForSingleObject(fifo->hMutex, INFINITE);
	dwAvailable = fifo->framecount;
	ReleaseMutex(fifo->hMutex);

	return dwAvailable;
}

/**
 * Insert a new frame. It has to be allocated and prepared
 * correctly before calling this function.
 */
void audio_fifo_push(audio_fifo_t *fifo, audio_fifo_data_t *afd)
{
	audio_fifo_data_t *tail;

	// Acquire lock
	WaitForSingleObject(fifo->hMutex, INFINITE);

	// Insert new data package into fifo
	tail = fifo->fifo_tail;
	if (tail != NULL) {
		tail->next = afd;
	} else {
		fifo->fifo_head = afd;
	}

	afd->next = NULL;
	fifo->fifo_tail = afd;
	if (fifo->fifo_head == NULL) {
		fifo->fifo_head = afd;
	}

	afd->seqid = fifo->seq++;

	// Update statistics
	fifo->framecount += afd->nframes;
	fifo->packetcount++;

	// Notify playback thread
	SetEvent(fifo->hEvent);

	ReleaseMutex(fifo->hMutex);
}

/**
 * Flush the fifo and remove all queued packages.
 */
void audio_fifo_flush(audio_fifo_t *fifo)
{
	audio_fifo_data_t *current, *next;

	// Acquire lock
	WaitForSingleObject(fifo->hMutex, INFINITE);

	current = fifo->fifo_head;
	while (current != NULL) {
		next = current->next;
		free(current);
		current = next;
	}

	fifo->fifo_head = NULL;
	fifo->fifo_tail = NULL;
	fifo->framecount = 0;
	fifo->packetcount = 0;

	// Notify playback thread
	SetEvent(fifo->hEvent);

	ReleaseMutex(fifo->hMutex);
}
