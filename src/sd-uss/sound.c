 /* 
  * UAE - The Un*x Amiga Emulator
  * 
  * Support for Linux/USS sound
  * 
  * Copyright 1997 Bernd Schmidt
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

#include <sys/ioctl.h>

#ifdef HAVE_SYS_SOUNDCARD_H
#include <sys/soundcard.h>
#elif defined HAVE_MACHINE_SOUNDCARD_H
#include <machine/soundcard.h>
#else
#error "Something went wrong during configuration."
#endif

int sound_fd;
static int have_sound;
static unsigned long formats;

uae_u16 sndbuffer[44100];
uae_u16 *sndbufpt;
int sndbufsize;

static int exact_log2(int v)
{
    int l = 0;
    while ((v >>= 1) != 0)
	l++;
    return l;
}

void close_sound(void)
{
    if (have_sound)
	close(sound_fd);
}

int setup_sound(void)
{
    sound_fd = open ("/dev/dsp", O_WRONLY);
    have_sound = !(sound_fd < 0);
    if (!have_sound)
	return 0;

    if (ioctl (sound_fd, SNDCTL_DSP_GETFMTS, &formats) == -1) {
	fprintf(stderr, "ioctl failed - can't use sound.\n");
	close(sound_fd);
	have_sound = 0;
	return 0;
    }

    sound_available = 1;
    return 1;
}

int init_sound (void)
{
    int tmp;
    int rate;
    int dspbits;

    if (currprefs.sound_maxbsiz < 128 || currprefs.sound_maxbsiz > 16384) {
	fprintf(stderr, "Sound buffer size %d out of range.\n", currprefs.sound_maxbsiz);
	currprefs.sound_maxbsiz = 8192;
    }

    tmp = 0x00040000 + exact_log2(currprefs.sound_maxbsiz);
    ioctl (sound_fd, SNDCTL_DSP_SETFRAGMENT, &tmp);
    ioctl (sound_fd, SNDCTL_DSP_GETBLKSIZE, &sndbufsize);

    dspbits = currprefs.sound_bits;
    ioctl (sound_fd, SNDCTL_DSP_SAMPLESIZE, &dspbits);
    ioctl (sound_fd, SOUND_PCM_READ_BITS, &dspbits);
    if (dspbits != currprefs.sound_bits) {
	fprintf(stderr, "Can't use sound with %d bits\n", currprefs.sound_bits);
	return 0;
    }

    tmp = currprefs.stereo;
    ioctl (sound_fd, SNDCTL_DSP_STEREO, &tmp);

    rate = currprefs.sound_freq;
    ioctl (sound_fd, SNDCTL_DSP_SPEED, &rate);
    ioctl (sound_fd, SOUND_PCM_READ_RATE, &rate);
    /* Some soundcards have a bit of tolerance here. */
    if (rate < currprefs.sound_freq * 90 / 100 || rate > currprefs.sound_freq * 110 / 100) {
	fprintf(stderr, "Can't use sound with desired frequency %d\n", currprefs.sound_freq);
	return 0;
    }

    sample_evtime = (long)maxhpos * maxvpos * 50 / rate;

    if (dspbits == 16) {
	/* Will this break horribly on bigendian machines? Possible... */
	if (!(formats & AFMT_S16_LE))
	    return 0;
	init_sound_table16 ();
	eventtab[ev_sample].handler = currprefs.stereo ? sample16s_handler : sample16_handler;
    } else {
	if (!(formats & AFMT_U8))
	    return 0;
	init_sound_table8 ();
	eventtab[ev_sample].handler = currprefs.stereo ? sample8s_handler : sample8_handler;
    }
    sound_available = 1;
    printf ("Sound driver found and configured for %d bits at %d Hz, buffer is %d bytes\n",
	    dspbits, rate, sndbufsize);
    sndbufpt = sndbuffer;
    
#ifdef FRAME_RATE_HACK
    vsynctime = vsynctime * 9 / 10;
#endif	

    return 1;
}
