 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Sound emulation stuff
  *
  * Copyright 1995, 1996, 1997 Bernd Schmidt
  */

extern struct audio_channel_data {
    uaecptr lc, pt;
    int state, wper, wlen;
    int current_sample;
    int vol;
    int *voltbl;
    unsigned long adk_mask;
    unsigned char dmaen, intreq2, data_written;
    uae_u16 dat, nextdat, per, len;    
} audio_channel[4];

#ifndef DONT_WANT_SOUND
extern void aud0_handler(void);
extern void aud1_handler(void);
extern void aud2_handler(void);
extern void aud3_handler(void);
#endif

extern void AUDxDAT(int nr, uae_u16 value);
extern void AUDxVOL(int nr, uae_u16 value);
extern void AUDxPER(int nr, uae_u16 value);
extern void AUDxLCH(int nr, uae_u16 value);
extern void AUDxLCL(int nr, uae_u16 value);
extern void AUDxLEN(int nr, uae_u16 value);

extern void audio_reset (void);
extern void dump_audio_bench (void);
