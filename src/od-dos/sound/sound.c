 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Support for DOS
  *
  * Copyright 1997 Gustavo Goedert
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "options.h"
#include "memory.h"
#include "custom.h"
#include "audio.h"
#include "gensound.h"
#include "sounddep/sound.h"
#include "events.h"

#include <go32.h>
#include <sys/farptr.h>
#include <time.h>

#include "sound/sb.h"
#include "sound/gus.h"

uae_u16 sndbuffer[44100];
uae_u16 *sndbufpt;
int sndbufsize;
extern int sound_table[256][64];
int dspbits;

int sound_curfreq;

void (*SND_Write)(void *buf, unsigned long size); // Pointer to function that plays data on card
void (*SND_DirectWrite)(unsigned int size, int freq); // New function that plays data directly
volatile int IsPlaying = 0;
int CurrentBuffer = 0;
unsigned int direct_buffers[2], direct_sndbufpt;

void direct_mono_sample16_handler(void);
void direct_mono_sample8_handler(void);
void direct_stereo_sample16_handler(void);
void direct_stereo_sample8_handler(void);
void normal_sample16_handler(void);
void normal_sample8_handler(void);
void interpol_freq(int new_freq);
void direct_check_sound_buffers(void);
void normal_check_sound_buffers(void);

#define INTERPOL_SIZE 8
int freq_buf[INTERPOL_SIZE];
int buf_tot, buf_pos=0;

inline void interpol_freq(int new_freq) {
    buf_tot = buf_tot - freq_buf[buf_pos] + new_freq;
    freq_buf[buf_pos] = new_freq;
    buf_pos++;
    if (buf_pos == INTERPOL_SIZE) buf_pos = 0;
    sound_curfreq = buf_tot/INTERPOL_SIZE;
    if (sound_curfreq<currprefs.sound_freq) {
	sound_curfreq |= 0xff;
	if (sound_curfreq<5512)
	    sound_curfreq = 5512;
    }
    if (sound_curfreq>currprefs.sound_freq)
	sound_curfreq = currprefs.sound_freq;
}

unsigned int sound_bytes = 0, last_clock = 0;

inline void direct_check_sound_buffers(void) {
    int played_size = direct_sndbufpt - direct_buffers[CurrentBuffer];

    sound_bytes++;
    if ((!IsPlaying) && played_size >= currprefs.sound_minbsiz) {
	SND_DirectWrite(played_size, sound_curfreq);
	if (currprefs.sound_adjust) {
	    unsigned int cur_clock, new_freq;

	    if (last_clock == 0)
		last_clock = uclock();
	    else {
		cur_clock = uclock();
		new_freq = (unsigned long long) sound_bytes * UCLOCKS_PER_SEC / (cur_clock-last_clock);
		interpol_freq(new_freq);
		last_clock = cur_clock;
	    }
	    sound_bytes = 0;
	}
	CurrentBuffer = !CurrentBuffer;
	direct_sndbufpt = direct_buffers[CurrentBuffer];
    } else if (played_size >= currprefs.sound_maxbsiz) {
	if (currprefs.sound_adjust)
	    interpol_freq(currprefs.sound_freq);
	while(IsPlaying);
	SND_DirectWrite(currprefs.sound_maxbsiz, sound_curfreq);
	CurrentBuffer = !CurrentBuffer;
	direct_sndbufpt = direct_buffers[CurrentBuffer];
	last_clock=0;
    }
}

inline void normal_check_sound_buffers(void) {
    if ((char *)sndbufpt - (char *)sndbuffer >= sndbufsize) {
	SND_Write(sndbuffer, sndbufsize);
	sndbufpt = sndbuffer;
    }
}

/* Gustavo et al.: *please* don't duplicate code like this, but use the functions
 * in audio.c */
void direct_mono_sample16_handler(void)
{
    int nr, adk;
    uae_u32 data = SOUND16_BASE_VAL;

    eventtab[ev_sample].evtime = cycles + sample_evtime;
    eventtab[ev_sample].oldcycles = cycles;

    adk = adkcon;

    if (!(adk & 0x11))
	data += sound_table[audio_channel[0].current_sample][audio_channel[0].vol];
    if (!(adk & 0x22))
	data += sound_table[audio_channel[1].current_sample][audio_channel[1].vol];
    if (!(adk & 0x44))
	data += sound_table[audio_channel[2].current_sample][audio_channel[2].vol];
    if (!(adk & 0x88))
	data += sound_table[audio_channel[3].current_sample][audio_channel[3].vol];

    _farnspokew(direct_sndbufpt, data);
    direct_sndbufpt += 2;
    direct_check_sound_buffers();
}

void direct_mono_sample8_handler(void)
{
    int nr, adk, played_size;
    uae_u32 data = SOUND8_BASE_VAL;

    eventtab[ev_sample].evtime = cycles + sample_evtime;
    eventtab[ev_sample].oldcycles = cycles;

    adk = adkcon;

    if (!(adk & 0x11))
	data += sound_table[audio_channel[0].current_sample][audio_channel[0].vol];
    if (!(adk & 0x22))
	data += sound_table[audio_channel[1].current_sample][audio_channel[1].vol];
    if (!(adk & 0x44))
	data += sound_table[audio_channel[2].current_sample][audio_channel[2].vol];
    if (!(adk & 0x88))
	data += sound_table[audio_channel[3].current_sample][audio_channel[3].vol];

    _farnspokeb(direct_sndbufpt++, data);
    direct_check_sound_buffers();
}

void direct_stereo_sample16_handler(void)
{
    int nr, adk;
    uae_u32 ldata = SOUND16_BASE_VAL, rdata = SOUND16_BASE_VAL;

    eventtab[ev_sample].evtime = cycles + sample_evtime;
    eventtab[ev_sample].oldcycles = cycles;

    adk = adkcon;

    if (!(adk & 0x11))
	ldata += sound_table[audio_channel[0].current_sample][audio_channel[0].vol];
    if (!(adk & 0x22))
	rdata += sound_table[audio_channel[1].current_sample][audio_channel[1].vol];
    if (!(adk & 0x44))
	rdata += sound_table[audio_channel[2].current_sample][audio_channel[2].vol];
    if (!(adk & 0x88))
	ldata += sound_table[audio_channel[3].current_sample][audio_channel[3].vol];

    _farnspokew(direct_sndbufpt, ldata);
    direct_sndbufpt += 2;
    _farnspokew(direct_sndbufpt, rdata);
    direct_sndbufpt += 2;
    direct_check_sound_buffers();
}

void direct_stereo_sample8_handler(void)
{
    int nr, adk, played_size;
    uae_u32 ldata = SOUND8_BASE_VAL, rdata = SOUND8_BASE_VAL;

    eventtab[ev_sample].evtime = cycles + sample_evtime;
    eventtab[ev_sample].oldcycles = cycles;

    adk = adkcon;

    if (!(adk & 0x11))
	ldata += sound_table[audio_channel[0].current_sample][audio_channel[0].vol];
    if (!(adk & 0x22))
	rdata += sound_table[audio_channel[1].current_sample][audio_channel[1].vol];
    if (!(adk & 0x44))
	rdata += sound_table[audio_channel[2].current_sample][audio_channel[2].vol];
    if (!(adk & 0x88))
	ldata += sound_table[audio_channel[3].current_sample][audio_channel[3].vol];

    _farnspokeb(direct_sndbufpt++, ldata);
    _farnspokeb(direct_sndbufpt++, rdata);
    direct_check_sound_buffers();
}


void normal_sample16_handler(void)
{
    int nr, adk;
    uae_u32 data = SOUND16_BASE_VAL;

    eventtab[ev_sample].evtime = cycles + sample_evtime;
    eventtab[ev_sample].oldcycles = cycles;

    adk = adkcon;

    if (!(adk & 0x11))
	data += sound_table[audio_channel[0].current_sample][audio_channel[0].vol];
    if (!(adk & 0x22))
	data += sound_table[audio_channel[1].current_sample][audio_channel[1].vol];
    if (!(adk & 0x44))
	data += sound_table[audio_channel[2].current_sample][audio_channel[2].vol];
    if (!(adk & 0x88))
	data += sound_table[audio_channel[3].current_sample][audio_channel[3].vol];

    *(uae_u16 *)sndbufpt = data;
    sndbufpt = (uae_u16 *)(((uae_u8 *)sndbufpt) + 2);
    normal_check_sound_buffers();
}

void normal_sample8_handler(void)
{
    int nr, adk;
    uae_u32 data = SOUND8_BASE_VAL;

    eventtab[ev_sample].evtime = cycles + sample_evtime;
    eventtab[ev_sample].oldcycles = cycles;

    adk = adkcon;

    if (!(adk & 0x11))
	data += sound_table[audio_channel[0].current_sample][audio_channel[0].vol];
    if (!(adk & 0x22))
	data += sound_table[audio_channel[1].current_sample][audio_channel[1].vol];
    if (!(adk & 0x44))
	data += sound_table[audio_channel[2].current_sample][audio_channel[2].vol];
    if (!(adk & 0x88))
	data += sound_table[audio_channel[3].current_sample][audio_channel[3].vol];

    *(uae_u8 *)sndbufpt = data;
    sndbufpt = (uae_u16 *)(((uae_u8 *)sndbufpt) + 1);
    normal_check_sound_buffers();
}

void close_sound(void) {
}

int setup_sound(void) {
    sound_available = 1;
    return 1;
}

int init_sound (void)
{
    int tmp;
    int rate;
    int i;

    if (currprefs.sound_minbsiz > currprefs.sound_maxbsiz) {
	fprintf(stderr, "Minimum sound buffer size bigger then maximum, exchanging.\n");
	tmp = currprefs.sound_minbsiz;
	currprefs.sound_minbsiz = currprefs.sound_maxbsiz;
	currprefs.sound_maxbsiz = tmp;
    }

    dspbits = currprefs.sound_bits;
    rate = currprefs.sound_freq;
    sndbufsize = currprefs.sound_maxbsiz;

    if (GUS_Init(&dspbits, &rate, &sndbufsize, direct_buffers, &currprefs.stereo));
    else if (SB_DetectInitSound(&dspbits, &rate, &sndbufsize, direct_buffers, &currprefs.stereo));
    else if (0/*OTHER_CARD_DETECT_ROUTINE*/);
    else
	return 0;

    currprefs.sound_freq = rate;
    currprefs.sound_minbsiz = (currprefs.sound_minbsiz>>2)<<2;
    currprefs.sound_maxbsiz = sndbufsize;

    if (direct_buffers[0] == 0)
	currprefs.sound_minbsiz = currprefs.sound_maxbsiz;

    sample_evtime = (long)maxhpos * maxvpos * 50 / rate;

    if (dspbits == 16) {
	init_sound_table16 ();
	if (direct_buffers[0] != 0) {
	    if (currprefs.stereo)
		eventtab[ev_sample].handler = direct_stereo_sample16_handler;
	    else
		eventtab[ev_sample].handler = direct_mono_sample16_handler;
	} else
	    eventtab[ev_sample].handler = normal_sample16_handler;
    } else {
	init_sound_table8 ();
	if (direct_buffers[0] != 0) {
	    if (currprefs.stereo)
		eventtab[ev_sample].handler = direct_stereo_sample8_handler;
	    else
		eventtab[ev_sample].handler = direct_mono_sample8_handler;
	} else
	    eventtab[ev_sample].handler = normal_sample8_handler;
    }
    sound_available = 1;
    printf ("Sound driver found and configured for %d bits at %d Hz, buffer is %d:%d bytes\n",
	    dspbits, rate, currprefs.sound_minbsiz, currprefs.sound_maxbsiz);
    sndbufpt = sndbuffer;
    direct_sndbufpt = direct_buffers[0];

    sound_curfreq = currprefs.sound_freq;
    for (i=0; i<INTERPOL_SIZE; i++)
	freq_buf[i] = sound_curfreq;
    buf_tot = sound_curfreq * INTERPOL_SIZE;

    return 1;
}
