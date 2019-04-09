 /* 
  * UAE - The Un*x Amiga Emulator
  * 
  * Support for Amiga audio.device sound
  * 
  * Copyright 1996, 1997 Samuel Devulder
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

#include <hardware/custom.h>
#include <hardware/cia.h>

#define CIAAPRA 0xBFE001 
#define CUSTOM  0xDFF000

static struct Custom *custom= (struct Custom*) CUSTOM;
static struct CIA *cia = (struct CIA *) CIAAPRA;

/*
 * Compared to Linux, AF_SOUND, and mac above, the AMIGA sound processing
 * with OS routines is awfull. (sam). But with AHI DOSDriver it is far more
 * easier.
 */

char whichchannel[]={1,2,4,8};
struct IOAudio *AudioIO;
struct MsgPort *AudioMP;
struct Message *AudioMSG;

unsigned char *buffers[2];
uae_u16 *sndbuffer;
uae_u16 *sndbufpt;
int sndbufsize;
int bufidx, devopen;

int have_sound, clockval, oldledstate, period;

ULONG AUDIO_FILE;

static ULONG TST_AUDIO_FILE(char *buff, char *name, int rate, int bsize)
{
    struct Process *pr = (void*)FindTask(NULL);
    ULONG wd, fd;

    if(!name) return 0;
    wd = (ULONG)pr->pr_WindowPtr;
    pr->pr_WindowPtr = (APTR)-1;
    sprintf(buff,name,rate,bsize);
    fd = Open(buff, MODE_NEWFILE);
    pr->pr_WindowPtr = (APTR)wd;
    return fd;
}

int setup_sound(void)
{
    sound_available = 1;
    return 1;
}

int init_sound (void) 
{ /* too complex ? No it is only the allocation of a single channel ! */
  /* it would have been far less painfull if AmigaOS provided a */
  /* SOUND: device handler */
    int rate;
    char buff[256],*devname = NULL;

    atexit(close_sound); /* if only amiga os had resource tracking */
    
    /* determine the clock */
    { 
	struct GfxBase *GB;
	GB = (void*)OpenLibrary("graphics.library",0L);
	if(!GB) goto fail;
	if (GB->DisplayFlags & PAL)
	    clockval = 3546895;        /* PAL clock */
	else
	    clockval = 3579545;        /* NTSC clock */
	CloseLibrary((void*)GB);
    }

    /* check buffsize */
    if (currprefs.sound_maxbsiz < 2 || currprefs.sound_maxbsiz > (256*1024)) {
        fprintf(stderr, "Sound buffer size %d out of range.\n", currprefs.sound_maxbsiz);
        currprefs.sound_maxbsiz = 8192;
    } 
    sndbufsize = (currprefs.sound_maxbsiz + 1)&~1;

    /* check freq */
    if (!currprefs.sound_freq) currprefs.sound_freq = 1;
    if (clockval/currprefs.sound_freq < 124 || clockval/currprefs.sound_freq > 65535) {
	fprintf(stderr, "Can't use sound with desired frequency %d Hz\n", currprefs.sound_freq);
        currprefs.sound_freq = 22000;
    }
    rate   = currprefs.sound_freq;
    period = (uae_u16)(clockval/rate);

    /* check for $AUDIONAME or AUD: or AUDIO: device */
    devname = buff;
    AUDIO_FILE = TST_AUDIO_FILE(buff, getenv("AUDIONAME"),
                                rate, sndbufsize);
    if(!AUDIO_FILE) /* AHI */
    AUDIO_FILE = TST_AUDIO_FILE(buff, "AUDIO:FREQUENCY=%d/BUFFER=%d",
                                rate, sndbufsize);
    if(!AUDIO_FILE) /* AUD: */
    AUDIO_FILE = TST_AUDIO_FILE(buff, "AUDIO:FREQUENCY%d/BUFFER%d",
                                rate, sndbufsize);
    if(!AUDIO_FILE)
    AUDIO_FILE = TST_AUDIO_FILE(buff, "AUD:FREQUENCY%d/BUFFER%d",
                                rate, sndbufsize);

    /* else use audio.device */
    if(!AUDIO_FILE) {
    /* setup the stuff */
    AudioMP = CreatePort(0,0);
    if(!AudioMP) goto fail;
        AudioIO = (struct IOAudio *)CreateExtIO(AudioMP, 
                                                sizeof(struct IOAudio));
    if(!AudioIO) goto fail;

    AudioIO->ioa_Request.io_Message.mn_Node.ln_Pri /*pfew!!*/ = 85;
    AudioIO->ioa_Data = whichchannel;
    AudioIO->ioa_Length = sizeof(whichchannel);
    AudioIO->ioa_AllocKey = 0;
        if(OpenDevice(devname = AUDIONAME, 0, (void*)AudioIO, 0)) goto fail;
    devopen = 1;
        }

    /* get the buffers */
    if(AUDIO_FILE) {
        buffers[0] = (void*)AllocMem(sndbufsize,MEMF_ANY|MEMF_CLEAR);
        buffers[1] = NULL;
        if(!buffers[0]) goto fail;
    } else {
        buffers[0] = (void*)AllocMem(sndbufsize,MEMF_CHIP|MEMF_CLEAR);
        buffers[1] = (void*)AllocMem(sndbufsize,MEMF_CHIP|MEMF_CLEAR);
        if(!buffers[0] || !buffers[1]) goto fail;
    }
    bufidx = 0;
    sndbuffer = sndbufpt = (uae_u16*)buffers[bufidx];

    oldledstate = cia->ciapra & (1<<CIAB_LED);
    cia->ciapra |= (1<<CIAB_LED);

    sample_evtime = (long)maxhpos * maxvpos * 50 / rate;
    init_sound_table8 ();
    eventtab[ev_sample].handler = sample8_handler;

    fprintf(stderr, "Sound driver found and configured for %d bits "
                    "at %d Hz, buffer is %d bytes (%s)\n",
                    8, rate, sndbufsize,devname);

    sound_available = 1;
    return 1;
fail:
    sound_available = 0;
    return 0;
}

void close_sound(void)
{
    if(AUDIO_FILE) Close(AUDIO_FILE);
    if(devopen) {CloseDevice((void*)AudioIO);devopen = 0;}
    if(AudioIO) {DeleteExtIO((void*)AudioIO);AudioIO = NULL;}
    if(AudioMP) {DeletePort((void*)AudioMP);AudioMP = NULL;}
    if(buffers[0]) {FreeMem((APTR)buffers[0],sndbufsize);buffers[0] = 0;}
    if(buffers[1]) {FreeMem((APTR)buffers[1],sndbufsize);buffers[1] = 0;}
    if(sound_available) {
    	cia->ciapra = (cia->ciapra & ~(1<<CIAB_LED)) | oldledstate;
	sound_available = 0;
    }
}
