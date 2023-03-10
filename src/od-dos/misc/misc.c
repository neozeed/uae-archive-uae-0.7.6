 /*
  * UAE - The Un*x Amiga Emulator
  *
  * DOS misc routines.
  *
  * (c) 1997 Gustavo Goedert
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include <dos.h>
#include <go32.h>
#include <dpmi.h>
#include <sys/farptr.h>
#include <crt0.h>
#include <signal.h>
#include <sys/fsext.h>
#include <stdlib.h>
#include <conio.h>
#include <signal.h>

#include "config.h"
#include "options.h"
#include "threaddep/penguin.h"
#include "memory.h"
#include "misc/misc.h"
#include "misc/handlers.h"
#include "xwin.h"
#include "video/vbe.h"
#include "video/dos-supp.h"
#include "disk.h"
#include "debug.h"
#include "picasso96.h"

#define MAXMESSAGES 100

char *Messages[MAXMESSAGES];
int CurrentMessage = 0;
int UseScreen = 0;
char *SaveMem = NULL, *OldBufMem;
UBYTE SavedLEDb;

void EnterText(void);
void LeaveText(void);
void DOS_init(void);
void DOS_leave(void);
void DOS_abort(void);
void DumpMessages(void);
static int ScreenWrite(__FSEXT_Fnumber func, int *retval, va_list rest_args);

extern int NeedDither, ModeNumber, UseLinear, ScreenIsPicasso, PicassoModeNumber, PicassoUseLinear;
extern char PicassoInvalidLines[];

void DOS_init(void) {
    int i;

    /* very used selector... */
    _farsetsel(_dos_ds);

    /* small check and setup */
    signal(SIGABRT, DOS_abort);
    atexit(DOS_leave);
    for (i=0; i<MAXMESSAGES; i++)
	Messages[i] = NULL;
    __FSEXT_set_function (fileno (stdout), ScreenWrite);
    __FSEXT_set_function (fileno (stderr), ScreenWrite);
    if(ersatzkickfile && disk_empty(0)) {
	printf ("No KickStart and no bootdisk, giving up.\n");
	exit(1);
    }
    signal(SIGINT, SIG_IGN);
    signal(SIGFPE, SIG_IGN);

    /* check video */
    if (!InitGfxLib()) {
	printf("Can't init GfxLib.\n");
	exit(1);
    }

    /* enable case on LFN systems (eg. win95) */
    if (_USE_LFN)
	_crt0_startup_flags = _CRT0_FLAG_PRESERVE_FILENAME_CASE;
}

void DOS_leave(void) {
    DumpMessages();
}

void DOS_abort(void) {
    printf ("abort!\n");
    exit(1);
}

void DumpMessages(void) {
    int i, StartMessage, MessagePosition;

    UseScreen = 1;
    if (CurrentMessage) {
	StartMessage = CurrentMessage - MAXMESSAGES;
	if (StartMessage < 0)
	    StartMessage = 0;
	for (i=StartMessage; i<CurrentMessage; i++) {
	    MessagePosition = i % MAXMESSAGES;
	    if (Messages[MessagePosition] != NULL)
		printf(Messages[MessagePosition]);
	}
	CurrentMessage = 0;
    }
}

static int ScreenWrite(__FSEXT_Fnumber func, int *retval, va_list rest_args) {
    char *buf, *mybuf;
    size_t buflen;
    int fd = va_arg (rest_args, int);
    int MessagePosition;

    if (func != __FSEXT_write || UseScreen)
	return 0;

    buf = va_arg(rest_args, char *);
    buflen = va_arg(rest_args, size_t);
    mybuf = malloc(buflen + 1);
    if (mybuf != NULL) {
	memcpy (mybuf, buf, buflen);
	mybuf[buflen] = '\0';
    }

    MessagePosition = CurrentMessage % MAXMESSAGES;
    if (Messages[ MessagePosition ] != NULL)
	free(Messages[ MessagePosition ]);
    Messages[ MessagePosition ] = mybuf;

    CurrentMessage++;

    *retval = buflen;
    return 1;
}

void EnterText(void) {
    _go32_dpmi_registers IntRegs;

    if (!ScreenIsPicasso) {
	if (ModeNumber != -1) {
	    SavedLEDb = InitLED();
	    UninstallKeyboardHandler();
	    SaveMem = malloc(gfxvidinfo.rowbytes*gfxvidinfo.height);
	    if (SaveMem != NULL) {
		memcpy(SaveMem, gfxvidinfo.bufmem, gfxvidinfo.rowbytes*gfxvidinfo.height);
		OldBufMem = gfxvidinfo.bufmem;
		gfxvidinfo.bufmem = SaveMem;
	    }
	}
    } else {
	if (PicassoModeNumber != -1) {
	    SavedLEDb = InitLED();
	    UninstallKeyboardHandler();
	}
    }
    /* Set text mode */
    IntRegs.x.ax = 0x1C;
    IntRegs.h.ah = 0x00;
    IntRegs.h.al = 0x03;
    IntRegs.x.ss = IntRegs.x.sp = IntRegs.x.flags = 0;
    _go32_dpmi_simulate_int(0x10, &IntRegs);
    _set_screen_lines(50);
    clrscr();
}

void LeaveText(void) {
    int i;

    if (!ScreenIsPicasso) {
	if (ModeNumber != -1) {
	    InstallKeyboardHandler();
	    RestoreLEDStatus(SavedLEDb);
	    /* Set graphics mode */
	    SetMode(ModeNumber, UseLinear);
	    /* Restore screen */
	    if ((gfxvidinfo.pixbytes == 1) || NeedDither)
		LoadPalette();
	    if (SaveMem != NULL) {
		gfxvidinfo.bufmem = OldBufMem;
		memcpy(gfxvidinfo.bufmem, SaveMem, gfxvidinfo.rowbytes*gfxvidinfo.height);
		free(SaveMem);
	    }
	    for(i=0; i<gfxvidinfo.height; i++)
		flush_line(i);
	}
    } else {
#ifdef PICASSO96
	if (PicassoModeNumber != -1) {
	    int i;
	    char *addr = gfxmemory + (picasso96_state.Address - gfxmem_start);

	    InstallKeyboardHandler();
	    RestoreLEDStatus(SavedLEDb);
	    /* Set graphics mode */
	    SetMode(PicassoModeNumber, PicassoUseLinear);
	    /* Restore screen */
	    DX_SetPalette (0, 256);
	    for (i = 0; i < picasso_vidinfo.height; i++, addr += picasso96_state.BytesPerRow) {
		PicassoInvalidLines[i] = 0;
		CurrentMode.PutLine(addr, i);
	    }
	}
#endif
    }
}

void HandleDisk(int num) {
    char *ptr=NULL, buf[256];

    EnterText();

    tui_reset();
    switch(num) {
     case 0:
	gotoxy(5, 4);
	cprintf("DF0:");
	ptr = tuifilereq("*.adf", currprefs.df[0]);
	break;
     case 1:
	gotoxy(5, 4);
	cprintf("DF1:");
	ptr = tuifilereq("*.adf", currprefs.df[1]);
	break;
     case 2:
	gotoxy(5, 4);
	cprintf("DF2:");
	ptr = tuifilereq("*.adf", currprefs.df[2]);
	break;
     case 3:
	gotoxy(5, 4);
	cprintf("DF3:");
	ptr = tuifilereq("*.adf", currprefs.df[3]);
	break;
    }
    tui_shutdown();
    if (ptr != NULL) {
	strcpy(buf, ptr);
	disk_insert(num, buf);
    }

    LeaveText();
}

void EnterDebug(void) {
    EnterText();
    DumpMessages();
    signal(SIGINT, activate_debugger);
    activate_debugger();
}

void LeaveDebug(void) {
    signal(SIGINT, SIG_IGN);
    UseScreen = 0;
    use_debugger = 0;
    LeaveText();
}

void SaveScreen(void) {
    static int err=0, lastsave=0;
    char filename[13];
    FILE *imagefile;
    char targaheader[18], *ptr;
    int i, j, r, g, b, c;
    UBYTE DitherBuf[1000];

    if (err || ScreenIsPicasso)
	return;

    while(1) {
	sprintf(filename, "uae%05d.tga", lastsave);
	imagefile = fopen(filename, "rb");
	if (imagefile == NULL) {
	    imagefile = fopen(filename, "wb");
	    if (imagefile == NULL) {
		err = 1;
		return;
	    }
	    break;
	}
	fclose(imagefile);
	lastsave++;
	if (lastsave == 100000) {
	    err = 1;
	    return;
	}
    }
    memset(targaheader, 0, 18);
    if (CurrentMode.BitsPerPixel == 8) {
	targaheader[1] = 1;
	targaheader[2] = 1;
	*((unsigned short *)&(targaheader[5])) = 256;
	targaheader[7] = 24;
    } else
	targaheader[2] = 2;
    *((unsigned short *)&(targaheader[12])) = CurrentMode.ModeWidth;
    *((unsigned short *)&(targaheader[14])) = gfxvidinfo.height;
    targaheader[16] = CurrentMode.BitsPerPixel;
    targaheader[17] = 0x20;
    fwrite(targaheader, 18, 1, imagefile);
    if (CurrentMode.BitsPerPixel == 8) {
	for(i=0; i<256; i++) {
	   ViewPalette(i, &r, &g, &b);
	   fputc(b, imagefile);
	   fputc(g, imagefile);
	   fputc(r, imagefile);
	}
    }
    if (NeedDither) {
	for(i=0; i<gfxvidinfo.height; i++) {
	    DitherLine(DitherBuf, (UWORD *)(gfxvidinfo.bufmem + i*gfxvidinfo.rowbytes), 0, i, CurrentMode.ModeWidth, CurrentMode.BitsPerPixel);
	    fwrite(DitherBuf, CurrentMode.ModeWidth, 1, imagefile);
	}
    }  else {
	if (CurrentMode.BitsPerColor!=16) {
	    for (i=0; i<gfxvidinfo.height; i++)
		fwrite(gfxvidinfo.bufmem + gfxvidinfo.rowbytes * i, CurrentMode.LineLength, 1, imagefile);
	} else {
	    for (i=0; i<gfxvidinfo.height; i++)
		for (j=0; j<CurrentMode.LineLength; j=j+2) {
		     ptr = gfxvidinfo.bufmem + gfxvidinfo.rowbytes * i + j;
		     r = (*((int *)ptr) >> CurrentMode.RedPosition) & ((1<<CurrentMode.RedSize)-1);
		     g = (*((int *)ptr) >> CurrentMode.GreenPosition) & ((1<<CurrentMode.GreenSize)-1);
		     b = (*((int *)ptr) >> CurrentMode.BluePosition) & ((1<<CurrentMode.BlueSize)-1);
		     if (CurrentMode.RedSize == 6)
			 r = r>>1;
		     if (CurrentMode.GreenSize == 6)
			 g = g>>1;
		     if (CurrentMode.BlueSize == 6)
			 b = b>>1;
		     c = b | (g<<5) | (r<<10);
		     fwrite(&c, 2, 1, imagefile);
		}
	}
    }
    fclose(imagefile);
}
