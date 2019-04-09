 /* 
  * UAE - The Un*x Amiga Emulator
  * 
  * Support for the Mute sound system.
  * 
  * Copyright 1997 Bernd Schmidt
  */

extern char *sndptr, *sndptrmax;
extern int sndbuf, currbuf;
extern char *lpData;
extern int samplecount;

static __inline__ void check_sound_buffers (void)
{
}

#if 0
#define PUT_SOUND_BYTE(b) \
	if (sndptr == sndptrmax) { \
		if (++currbuf >= NUMSNDBUFS) currbuf -= NUMSNDBUFS; \
		sndptr = wavehdr[currbuf].lpData; \
		sndptrmax = sndptr+sndbufsize; \
	} \
	*sndptr++ = b; \
	bytesinbuf++;


#define PUT_SOUND_WORD(b) \
	if (sndptr == sndptrmax) { \
		if (++currbuf >= NUMSNDBUFS) currbuf -= NUMSNDBUFS; \
		sndptr = wavehdr[currbuf].lpData; \
		sndptrmax = sndptr+sndbufsize; \
	} \
	*(short *)sndptr = b; \
	sndptr += 2; \
	bytesinbuf += 2;
#endif

#define PUT_SOUND_BYTE(b) \
	if (sndptr == sndptrmax) { \
		sndptr = lpData; \
	} \
	samplecount++; \
	*sndptr++ = b; 


#define PUT_SOUND_WORD(b) \
	if (sndptr == sndptrmax) { \
		sndptr = lpData; \
	} \
	*(short *)sndptr = b; \
	samplecount++; \
	sndptr += 2; 

#define PUT_SOUND_BYTE_RIGHT(b) PUT_SOUND_BYTE(b)
#define PUT_SOUND_BYTE_LEFT(b) PUT_SOUND_BYTE(b)
#define PUT_SOUND_WORD_RIGHT(b) PUT_SOUND_WORD(b)
#define PUT_SOUND_WORD_LEFT(b) PUT_SOUND_WORD(b)

#define SOUND16_BASE_VAL 0
#define SOUND8_BASE_VAL 128

#define DEFAULT_SOUND_MINB 1500
#define DEFAULT_SOUND_MAXB 1500
#define DEFAULT_SOUND_BITS 8
#define DEFAULT_SOUND_FREQ 22050

#define HAVE_STEREO_SUPPORT
