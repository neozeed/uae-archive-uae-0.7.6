/* 
 * UAE - The Un*x Amiga Emulator
 *
 * Win32 interface
 *
 * Copyright 1997 Mathias Ortmann
 */

#ifdef __GNUC__
#define __int64 long long
#include "machdep/winstuff.h"
#else
#include <windows.h>
#include <ddraw.h>
#include <stdlib.h>
#include <stdarg.h>
#include <commctrl.h>
#include <commdlg.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <io.h>
#endif

#include "config.h"
#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "gensound.h"
#include "sounddep/sound.h"
#include "events.h"
#include "uae.h"
#include "include/memory.h"
#include "custom.h"
#include "osdep/win32gui.h"
#include "resource.h"
#include "osdep/win32.h"

/* Sound emulation, Win32 interface */
HGLOBAL hWaveHdr;
LPWAVEHDR lpWaveHdr;
HANDLE hData;
LPSTR lpData;
HANDLE hWaveOut;

static WAVEFORMATEX wavfmt;
char *sndptr, *sndptrmax, soundneutral;
signed int bytesinbuf;
static HGLOBAL hSndbuf;
MMTIME mmtime;
static DWORD basevsynctime;

int stereo_sound;

signed long bufsamples;
int samplecount;
DWORD soundbufsize;

void close_sound (void)
{
    if (hWaveOut) {
	waveOutReset (hWaveOut);
	waveOutUnprepareHeader (hWaveOut, lpWaveHdr, sizeof (WAVEHDR));

	GlobalUnlock (hWaveHdr);
	GlobalFree (lpWaveHdr);

	GlobalUnlock (hData);
	GlobalFree (hData);

	waveOutClose (hWaveOut);

	hWaveOut = NULL;
    }
}

void startsound (void)
{
    lpWaveHdr->lpData = lpData;
    lpWaveHdr->dwBufferLength = soundbufsize;
    lpWaveHdr->dwFlags = WHDR_BEGINLOOP | WHDR_ENDLOOP;
    lpWaveHdr->dwLoops = 0x7fffffffL;
    waveOutPrepareHeader (hWaveOut, lpWaveHdr, sizeof (WAVEHDR));

    if (waveOutWrite (hWaveOut, lpWaveHdr, sizeof (WAVEHDR))) {
	MyOutputDebugString ("Failed to write to sound card\n");
	return;
    }
}

void stopsound (void)
{
    sndptr = lpData;
    samplecount = 0;
    memset (lpData, soundneutral, soundbufsize);
    waveOutReset (hWaveOut);
}

static int init_sound_win32 (void)
{
    MMRESULT mmres;
    DWORD id;
    long i;
    __int64 freq;

    if (currprefs.produce_sound < 0) {
	currprefs.produce_sound = 0;
	MyOutputDebugString ("FrameSync disabled.\n");
    } else {
#ifdef FRAME_RATE_HACK
	if (!vsynctime) {
	    DWORD oldpri = GetPriorityClass (GetCurrentProcess ());

	    MyOutputDebugString ("Measuring clock rate...\n");

	    SetPriorityClass (GetCurrentProcess (), REALTIME_PRIORITY_CLASS);
	    i = read_processor_time ();
	    Sleep (1000);
	    i = read_processor_time () - i;
	    SetPriorityClass (GetCurrentProcess (), oldpri);

	    MyOutputDebugString ("Your machine is running at approx. %d Hz\n", i);
	    vsynctime = i / 50;
	} else if (vsynctime > 0)
	    vsynctime *= 1000 / 50;
	else {
	    MyOutputDebugString ("Using QueryPerformanceCounter()...\n");
	    QueryPerformanceFrequency (&freq);
	    vsynctime = freq / 50;
	    useqpc = 1;
	}

	if (!vsynctime)
	    MyOutputDebugString ("No performance counters available. Bad.\n");
	else
	    MyOutputDebugString ("FrameSync enabled. Ticks per frame: %d\n", vsynctime);
#else
	vsynctime = 0;
#endif
    }

    if (currprefs.produce_sound < 2) {
	MyOutputDebugString ("Sound output disabled.\n");
	return 1;
    }
    mmtime.wType = TIME_SAMPLES;

    wavfmt.wFormatTag = WAVE_FORMAT_PCM;
    wavfmt.nChannels = 1 + stereo_sound;
    wavfmt.nSamplesPerSec = currprefs.sound_freq;
    wavfmt.nBlockAlign = currprefs.sound_bits / 8 * wavfmt.nChannels;
    wavfmt.nAvgBytesPerSec = wavfmt.nBlockAlign * currprefs.sound_freq;
    wavfmt.wBitsPerSample = currprefs.sound_bits;

    soundneutral = currprefs.sound_bits == 8 ? 128 : 0;
    bufsamples = 4096 * currprefs.sound_freq / 22050;
    soundbufsize = bufsamples * wavfmt.nBlockAlign;

    if (!(hData = GlobalAlloc (GMEM_MOVEABLE | GMEM_SHARE, soundbufsize))) {
	MyOutputDebugString ("Failed to allocate sound buffer!\n");
	return 0;
    }
    if (!(lpData = GlobalLock (hData))) {
	MyOutputDebugString ("Failed to lock sound buffer!\n");
	return 0;
    }
    if (!(hWaveHdr = GlobalAlloc (GMEM_MOVEABLE | GMEM_SHARE, (DWORD) sizeof (WAVEHDR)))) {
	MyOutputDebugString ("Failed to allocate wave header!\n");
	return 0;
    }
    if (!(lpWaveHdr = (LPWAVEHDR) GlobalLock (hWaveHdr))) {
	MyOutputDebugString ("Failed to lock wave header!\n");
	return 0;
    }
    if (mmres = waveOutOpen (&hWaveOut, WAVE_MAPPER, &wavfmt, 0, 0, CALLBACK_NULL)) {
	MyOutputDebugString ("Sound device failed to open with error code %d.\n", mmres);
	return 0;
    }
    basevsynctime = vsynctime;
    sndptrmax = lpData + soundbufsize;

    stopsound ();
    startsound ();

    return currprefs.sound_freq;
}

void adjust_sound_timing (void)
{
    static DWORD last;
    signed long diff;

    waveOutGetPosition (hWaveOut, &mmtime, sizeof (mmtime));
    if (!mmtime.u.sample)
	mmtime.u.sample++;

    if (last == mmtime.u.sample)
	return;
    last = mmtime.u.sample;

    diff = samplecount - mmtime.u.sample;

    if (diff < -bufsamples || diff > bufsamples) {
	samplecount = mmtime.u.sample;
	sndptr = lpData + (samplecount % bufsamples) * wavfmt.nBlockAlign;
    }
    if (diff < 0)
	vsynctime = basevsynctime * 5 / 6;
    else if (diff > 0)
	vsynctime = basevsynctime * 7 / 6;
}

int setup_sound (void)
{
    sound_available = 1;
    return 1;
}

int init_sound (void)
{
    int rate;

    if ((rate = init_sound_win32 ()) < 2)
	return rate;

    sample_evtime = (long) maxhpos *maxvpos * 50 / rate;

    if (currprefs.sound_bits == 16) {
	init_sound_table16 ();
	eventtab[ev_sample].handler = sample16_handler;
    } else {
	init_sound_table8 ();
	eventtab[ev_sample].handler = sample8_handler;
    }

    MyOutputDebugString ("Sound driver found and configured for %d bits at %d Hz %s\n",
			 currprefs.sound_bits, rate, stereo_sound ? "stereo" : "");

    return 1;
}
