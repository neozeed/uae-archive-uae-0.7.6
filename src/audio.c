 /*
  * UAE - The Un*x Amiga Emulator
  *
  * OS specific functions
  *
  * Copyright 1995, 1996, 1997 Bernd Schmidt
  * Copyright 1996 Marcus Sundberg
  * Copyright 1996 Manfred Thole
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "options.h"
#include "memory.h"
#include "custom.h"
#include "gensound.h"
#include "sounddep/sound.h"
#include "events.h"
#include "audio.h"

#undef BENCHMARK_AUDIO

#ifdef BENCHMARK_AUDIO

#define BEGIN_BENCH frame_time_t audbench = read_processor_time ();
#define END_BENCH sh_time += read_processor_time () - audbench; sh_count++;
static frame_time_t sh_time = 0;
unsigned long sh_count = 0;

#else

#define BEGIN_BENCH
#define END_BENCH

#endif

int sound_available = 0;

struct audio_channel_data audio_channel[4];

int sound_table[64][256];
unsigned long int sample_evtime;

void init_sound_table16(void)
{
    int i,j;

    for (i = 0; i < 256; i++)
	for (j = 0; j < 64; j++)
	    sound_table[j][i] = j * (uae_s8)i * (currprefs.stereo ? 2 : 1);
}

void init_sound_table8 (void)
{
    int i,j;

    for (i = 0; i < 256; i++)
	for (j = 0; j < 64; j++)
	    sound_table[j][i] = (j * (uae_s8)i * (currprefs.stereo ? 2 : 1)) / 256;
}

void AUDxDAT(int nr, uae_u16 v)
{
    struct audio_channel_data *cdp = audio_channel + nr;
    cdp->dat = v;
    if (cdp->state == 0 && !(INTREQR() & (0x80 << nr))) {
	cdp->state = 2;
	INTREQ(0x8000 | (0x80 << nr));
	/* data_written = 2 ???? */
	eventtab[ev_aud0 + nr].evtime = cycles + cdp->per;
	eventtab[ev_aud0 + nr].oldcycles = cycles;
	eventtab[ev_aud0 + nr].active = 1;
	events_schedule();
    }
}

void AUDxLCH(int nr, uae_u16 v) { audio_channel[nr].lc = (audio_channel[nr].lc & 0xffff) | ((uae_u32)v << 16); }
void AUDxLCL(int nr, uae_u16 v) { audio_channel[nr].lc = (audio_channel[nr].lc & ~0xffff) | (v & 0xFFFE); }
void AUDxPER(int nr, uae_u16 v)
{
    if (v <= 0) {
#if 0 /* v == 0 is rather common, and harmless, and the value isn't signed anyway */
	static int warned = 0;
	if (!warned)
	    write_log ("Broken program accessing the sound hardware\n"), warned++;
#endif
	v = 65535;
    }
    if (v < maxhpos/2 && currprefs.produce_sound < 3)
	v = maxhpos/2;

    audio_channel[nr].per = v;
}

void AUDxLEN(int nr, uae_u16 v) { audio_channel[nr].len = v; }

#define MULTIPLICATION_PROFITABLE

#ifdef MULTIPLICATION_PROFITABLE
typedef uae_s8 sample8_t;
#define DO_CHANNEL_1(v, c) do { (v) *= audio_channel[c].vol; } while (0)
#define SBASEVAL8(logn) ((logn) == 1 ? SOUND8_BASE_VAL << 7 : SOUND8_BASE_VAL << 8)
#define SBASEVAL16(logn) ((logn) == 1 ? SOUND16_BASE_VAL >> 1 : SOUND16_BASE_VAL)
#define FINISH_DATA(b,logn) do { if (14 - (b) + (logn) > 0) data >>= 14 - (b) + (logn); else data <<= (b) - 14 - (logn); } while (0);
#else
typedef uae_u8 sample8_t;
#define DO_CHANNEL_1(v, c) do { (v) = audio_channel[c].voltbl[(v)]; } while (0)
#define SBASEVAL8(logn) SOUND8_BASE_VAL
#define SBASEVAL16(logn) SOUND16_BASE_VAL
#define FINISH_DATA(b,logn)
#endif

void AUDxVOL(int nr, uae_u16 v)
{
    int v2 = v & 64 ? 63 : v & 63;
    audio_channel[nr].vol = v2;
#ifndef MULTIPLICATION_PROFITABLE
    audio_channel[nr].voltbl = sound_table[v2];
#endif
}

#define DO_CHANNEL(v, c) do { (v) &= audio_channel[c].adk_mask; data += v; } while (0);

/* Templates! I want templates! */
void sample16_handler(void)
{
    BEGIN_BENCH

    uae_u32 data0 = audio_channel[0].current_sample;
    uae_u32 data1 = audio_channel[1].current_sample;
    uae_u32 data2 = audio_channel[2].current_sample;
    uae_u32 data3 = audio_channel[3].current_sample;
    DO_CHANNEL_1 (data0, 0);
    DO_CHANNEL_1 (data1, 1);
    DO_CHANNEL_1 (data2, 2);
    DO_CHANNEL_1 (data3, 3);
    data0 &= audio_channel[0].adk_mask;
    data1 &= audio_channel[1].adk_mask;
    data2 &= audio_channel[2].adk_mask;
    data3 &= audio_channel[3].adk_mask;
    data0 += data1;
    data0 += data2;
    data0 += data3;
    {
	uae_u32 data = SBASEVAL16(2) + data0;
	FINISH_DATA(16, 2);
	PUT_SOUND_WORD (data);
    }
    END_BENCH

    check_sound_buffers ();

    eventtab[ev_sample].evtime = cycles + sample_evtime;
    eventtab[ev_sample].oldcycles = cycles;
}

void sample8_handler(void)
{
    BEGIN_BENCH
	
    uae_u32 data0 = audio_channel[0].current_sample;
    uae_u32 data1 = audio_channel[1].current_sample;
    uae_u32 data2 = audio_channel[2].current_sample;
    uae_u32 data3 = audio_channel[3].current_sample;
    DO_CHANNEL_1 (data0, 0);
    DO_CHANNEL_1 (data1, 1);
    DO_CHANNEL_1 (data2, 2);
    DO_CHANNEL_1 (data3, 3);
    data0 &= audio_channel[0].adk_mask;
    data1 &= audio_channel[1].adk_mask;
    data2 &= audio_channel[2].adk_mask;
    data3 &= audio_channel[3].adk_mask;
    data0 += data1;
    data0 += data2;
    data0 += data3;
    {
	uae_u32 data = SBASEVAL8(2) + data0;
	FINISH_DATA(8, 2);
	PUT_SOUND_BYTE (data);
    }
    END_BENCH

    check_sound_buffers ();

    eventtab[ev_sample].evtime = cycles + sample_evtime;
    eventtab[ev_sample].oldcycles = cycles;
}

#ifdef HAVE_STEREO_SUPPORT
void sample16s_handler(void)
{
    BEGIN_BENCH

    uae_u32 data0 = audio_channel[0].current_sample;
    uae_u32 data1 = audio_channel[1].current_sample;
    uae_u32 data2 = audio_channel[2].current_sample;
    uae_u32 data3 = audio_channel[3].current_sample;
    DO_CHANNEL_1 (data0, 0);
    DO_CHANNEL_1 (data1, 1);
    DO_CHANNEL_1 (data2, 2);
    DO_CHANNEL_1 (data3, 3);

    data0 &= audio_channel[0].adk_mask;
    data1 &= audio_channel[1].adk_mask;
    data2 &= audio_channel[2].adk_mask;
    data3 &= audio_channel[3].adk_mask;
    
    data0 += data3;
    {
	uae_u32 data = SBASEVAL16(1) + data0;
	FINISH_DATA (16, 1);
	PUT_SOUND_WORD_RIGHT (data);
    }

    data1 += data2;
    {
	uae_u32 data = SBASEVAL16(1) + data1;
	FINISH_DATA (16, 1);
	PUT_SOUND_WORD_LEFT (data);
    }
    
    END_BENCH
    
    check_sound_buffers ();

    eventtab[ev_sample].evtime = cycles + sample_evtime;
    eventtab[ev_sample].oldcycles = cycles;
}

void sample8s_handler(void)
{
    BEGIN_BENCH

    uae_u32 data0 = audio_channel[0].current_sample;
    uae_u32 data1 = audio_channel[1].current_sample;
    uae_u32 data2 = audio_channel[2].current_sample;
    uae_u32 data3 = audio_channel[3].current_sample;
    DO_CHANNEL_1 (data0, 0);
    DO_CHANNEL_1 (data1, 1);
    DO_CHANNEL_1 (data2, 2);
    DO_CHANNEL_1 (data3, 3);

    data0 &= audio_channel[0].adk_mask;
    data1 &= audio_channel[1].adk_mask;
    data2 &= audio_channel[2].adk_mask;
    data3 &= audio_channel[3].adk_mask;

    data0 += data3;
    {
	uae_u32 data = SBASEVAL8(1) + data0;
	FINISH_DATA (8, 1);
	PUT_SOUND_BYTE_RIGHT (data);
    }
    data1 += data2;
    {
	uae_u32 data = SBASEVAL8(1) + data1;
	FINISH_DATA (8, 1);
	PUT_SOUND_BYTE_LEFT (data);
    }

    END_BENCH
    
    check_sound_buffers ();

    eventtab[ev_sample].evtime = cycles + sample_evtime;
    eventtab[ev_sample].oldcycles = cycles;
}
#else
void sample8s_handler(void)
{
    sample8_handler();
}
void sample16s_handler(void)
{
    sample16_handler();
}
#endif

static uae_u8 int2ulaw(int ch)
{
    int mask;

    if (ch < 0) {
      ch = -ch;
      mask = 0x7f;
    }
    else {
      mask = 0xff;
    }

    if (ch < 32) {
	ch = 0xF0 | ( 15 - (ch/2) );
    } else if (ch < 96) {
	ch = 0xE0 | ( 15 - (ch-32)/4 );
    } else if (ch < 224) {
	ch = 0xD0 | ( 15 - (ch-96)/8 );
    } else if (ch < 480) {
	ch = 0xC0 | ( 15 - (ch-224)/16 );
    } else if (ch < 992 ) {
	ch = 0xB0 | ( 15 - (ch-480)/32 );
    } else if (ch < 2016) {
	ch = 0xA0 | ( 15 - (ch-992)/64 );
    } else if (ch < 4064) {
	ch = 0x90 | ( 15 - (ch-2016)/128 );
    } else if (ch < 8160) {
	ch = 0x80 | ( 15 - (ch-4064)/256 );
    } else {
	ch = 0x80;
    }
    return (uae_u8)(mask & ch);
}

void sample_ulaw_handler (void)
{
    int nr;
    uae_u32 data = 0;

    eventtab[ev_sample].evtime += cycles - eventtab[ev_sample].oldcycles;
    eventtab[ev_sample].oldcycles = cycles;

    for (nr = 0; nr < 4; nr++) {
	if (!(adkcon & (0x11 << nr))) {
	    uae_u32 d = audio_channel[nr].current_sample;
	    DO_CHANNEL_1 (d, nr);
	    data += d;
	}
    }
    PUT_SOUND_BYTE (int2ulaw (data));
    check_sound_buffers ();
}

static void audio_handler (int nr)
{
    struct audio_channel_data *cdp = audio_channel + nr;

    switch (cdp->state) {
     case 0:
	fprintf(stderr, "Bug in sound code\n");
	break;

     case 1:
	/* We come here at the first hsync after DMA was turned on. */
	eventtab[ev_aud0 + nr].evtime += maxhpos;
	eventtab[ev_aud0 + nr].oldcycles += maxhpos;

	cdp->state = 5;
	INTREQ(0x8000 | (0x80 << nr));
	if (cdp->wlen != 1)
	    cdp->wlen--;
	cdp->nextdat = chipmem_bank.wget(cdp->pt);

	cdp->pt += 2;
	break;

     case 5:
	/* We come here at the second hsync after DMA was turned on. */
	if (currprefs.produce_sound == 0)
	    cdp->per = 65535;

	eventtab[ev_aud0 + nr].evtime = cycles + cdp->per;
	eventtab[ev_aud0 + nr].oldcycles = cycles;
	cdp->dat = cdp->nextdat;
	cdp->current_sample = (sample8_t)(cdp->dat >> 8);

	cdp->state = 2;
	{
	    int audav = adkcon & (1 << nr);
	    int audap = adkcon & (16 << nr);
	    int napnav = (!audav && !audap) || audav;
	    if (napnav)
		cdp->data_written = 2;
	}
	break;

     case 2:
	/* We come here when a 2->3 transition occurs */
	if (currprefs.produce_sound == 0)
	    cdp->per = 65535;

	cdp->current_sample = (sample8_t)(cdp->dat & 0xFF);
	eventtab[ev_aud0 + nr].evtime = cycles + cdp->per;
	eventtab[ev_aud0 + nr].oldcycles = cycles;

	cdp->state = 3;

	/* Period attachment? */
	if (adkcon & (0x10 << nr)) {
	    if (cdp->intreq2 && cdp->dmaen)
		INTREQ(0x8000 | (0x80 << nr));
	    cdp->intreq2 = 0;

	    cdp->dat = cdp->nextdat;
	    if (cdp->dmaen)
		cdp->data_written = 2;
	    if (nr < 3) {
		if (cdp->dat == 0)
		    (cdp+1)->per = 65535;

		else if (cdp->dat < maxhpos/2 && currprefs.produce_sound < 3)
		    (cdp+1)->per = maxhpos/2;
		else
		    (cdp+1)->per = cdp->dat;
	    }
	}
	break;

     case 3:
	/* We come here when a 3->2 transition occurs */
	if (currprefs.produce_sound == 0)
	    cdp->per = 65535;

	eventtab[ev_aud0 + nr].evtime = cycles + cdp->per;
	eventtab[ev_aud0 + nr].oldcycles = cycles;

	if ((INTREQR() & (0x80 << nr)) && !cdp->dmaen) {
	    cdp->state = 0;
	    cdp->current_sample = 0;
	    eventtab[ev_aud0 + nr].active = 0;
	    break;
	} else {
	    int audav = adkcon & (1 << nr);
	    int audap = adkcon & (16 << nr);
	    int napnav = (!audav && !audap) || audav;
	    cdp->state = 2;

	    if ((cdp->intreq2 && cdp->dmaen && napnav)
		|| (napnav && !cdp->dmaen))
		INTREQ(0x8000 | (0x80 << nr));
	    cdp->intreq2 = 0;

	    cdp->dat = cdp->nextdat;
	    cdp->current_sample = (sample8_t)(cdp->dat >> 8);

	    if (cdp->dmaen && napnav)
		cdp->data_written = 2;

	    /* Volume attachment? */
	    if (audav) {
		if (nr < 3) {
		    (cdp+1)->vol = cdp->dat;
#ifndef MULTIPLICATION_PROFITABLE
		    (cdp+1)->voltbl = sound_table[cdp->dat];
#endif
		}
	    }
	}
	break;

     default:
	cdp->state = 0;
	eventtab[ev_aud0 + nr].active = 0;
	break;
    }
}

void aud0_handler (void)
{
    audio_handler (0);
}
void aud1_handler (void)
{
    audio_handler (1);
}
void aud2_handler (void)
{
    audio_handler (2);
}
void aud3_handler (void)
{
    audio_handler (3);
}

void audio_reset (void)
{
    memset (audio_channel, 0, sizeof audio_channel);
    audio_channel[0].per = 65535;
    audio_channel[1].per = 65535;
    audio_channel[2].per = 65535;
    audio_channel[3].per = 65535;
    audio_channel[0].voltbl = sound_table[0];
    audio_channel[1].voltbl = sound_table[0];
    audio_channel[2].voltbl = sound_table[0];
    audio_channel[3].voltbl = sound_table[0];
}

void dump_audio_bench (void)
{
#ifdef BENCHMARK_AUDIO
    printf ("Average cycles per sample handler: %f\n", ((double)sh_time / sh_count));
#endif
}
