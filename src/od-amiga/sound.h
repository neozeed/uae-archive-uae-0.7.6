 /* 
  * UAE - The Un*x Amiga Emulator
  * 
  * Support for Amiga audio.device sound
  * 
  * Copyright 1996, 1997 Samuel Devulder
  */

#include <proto/exec.h>
#include <proto/dos.h>
#ifndef __SASC
#include <proto/alib.h>
#endif

#include <exec/memory.h>
#include <exec/devices.h>
#include <exec/io.h>

#include <graphics/gfxbase.h>
#include <devices/timer.h>
#include <devices/audio.h>

extern char whichchannel[];
extern struct IOAudio *AudioIO;
extern struct MsgPort *AudioMP;
extern struct Message *AudioMSG;

extern unsigned char *buffers[2];
extern uae_u16 *sndbuffer, *sndbufpt;
extern int bufidx, devopen;
extern int sndbufsize;

extern int have_sound, clockval, oldledstate, period;

extern ULONG AUDIO_FILE;

static __inline__ void flush_sound_buffer(void)
{
    if(AUDIO_FILE) {
        Write(AUDIO_FILE, buffers[bufidx], sndbufsize);
    } else {
	static char IOSent = 0;

	AudioIO->ioa_Request.io_Command = CMD_WRITE;
	AudioIO->ioa_Request.io_Flags   = ADIOF_PERVOL|IOF_QUICK;
	AudioIO->ioa_Data               = (uae_s8 *)buffers[bufidx];
	AudioIO->ioa_Length             = sndbufsize;
	AudioIO->ioa_Period             = period;
	AudioIO->ioa_Volume             = 64;
	AudioIO->ioa_Cycles             = 1;

	if(IOSent) WaitIO((void*)AudioIO); else IOSent=1;
	BeginIO((void*)AudioIO);

	/* double buffering */
	bufidx = 1 - bufidx;
	sndbuffer = sndbufpt = (uae_u16*)buffers[bufidx];
    }
}

static __inline__ void check_sound_buffers (void)
{
    if ((char *)sndbufpt - (char *)sndbuffer >= sndbufsize) {
	flush_sound_buffer();
    }
}

#define PUT_SOUND_BYTE(b) do { *(uae_u8 *)sndbufpt = b; sndbufpt = (uae_u16 *)(((uae_u8 *)sndbufpt) + 1); } while (0)
#define PUT_SOUND_WORD(b) do { *(uae_u16 *)sndbufpt = b; sndbufpt = (uae_u16 *)(((uae_u8 *)sndbufpt) + 2); } while (0)
#define SOUND16_BASE_VAL 0
#define SOUND8_BASE_VAL 0

#define DEFAULT_SOUND_MAXB 8192
#define DEFAULT_SOUND_MINB 8192
#define DEFAULT_SOUND_BITS 16
#define DEFAULT_SOUND_FREQ 11025

#define UNSUPPORTED_OPTION_b

#undef DONT_WANT_SOUND
