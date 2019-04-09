 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Main program
  *
  * Copyright 1995 Ed Hanway
  * Copyright 1995, 1996, 1997 Bernd Schmidt
  */

#include "sysconfig.h"
#include "sysdeps.h"
#include <assert.h>

#include "config.h"
#include "options.h"
#include "threaddep/penguin.h"
#include "uae.h"
#include "gensound.h"
#include "sounddep/sound.h"
#include "events.h"
#include "memory.h"
#include "custom.h"
#include "serial.h"
#include "readcpu.h"
#include "newcpu.h"
#include "disk.h"
#include "debug.h"
#include "xwin.h"
#include "joystick.h"
#include "keybuf.h"
#include "gui.h"
#include "zfile.h"
#include "autoconf.h"
#include "osemu.h"
#include "osdep/exectasks.h"
#include "compiler.h"
#include "picasso96.h"
#include "uaeexe.h"

int version = 100*UAEMAJOR + 10*UAEMINOR + UAEURSAMINOR;

/* Note to porters: please don't change any of these options! UAE is supposed
 * to behave identically on all platforms if possible. */
struct uae_prefs currprefs;

static void default_currprefs (void)
{
    currprefs.framerate = 1;
    currprefs.illegal_mem = 0;
    currprefs.no_xhair = 0;
    currprefs.use_serial = 0;
    currprefs.serial_demand = 0;
    currprefs.parallel_demand = 0;
    currprefs.automount_uaedev = 1;

    currprefs.fake_joystick = 2 + (0<<8);
    currprefs.keyboard_lang = KBD_LANG_US;
    currprefs.emul_accuracy = 2;
    currprefs.test_drawing_speed = 0;

    currprefs.produce_sound = 0;
    currprefs.stereo = 0;
    currprefs.sound_bits = DEFAULT_SOUND_BITS;
    currprefs.sound_freq = DEFAULT_SOUND_FREQ;
    currprefs.sound_minbsiz = DEFAULT_SOUND_MINB;
    currprefs.sound_maxbsiz = DEFAULT_SOUND_MAXB;

    currprefs.gfx_width = 800;
    currprefs.gfx_height = 600;
    currprefs.gfx_lores = 0;
    currprefs.gfx_linedbl = 0;
    currprefs.gfx_correct_aspect = 0;
    currprefs.gfx_xcenter = 0;
    currprefs.gfx_ycenter = 0;
    currprefs.color_mode = 0;

    currprefs.use_low_bandwidth = 0;
    currprefs.use_mitshm = 0;

    currprefs.immediate_blits = 0;
    currprefs.blits_32bit_enabled = 0;

    strcpy (currprefs.df[0], "df0.adf");
    strcpy (currprefs.df[1], "df1.adf");
    strcpy (currprefs.df[2], "df2.adf");
    strcpy (currprefs.df[3], "df3.adf");

    currprefs.m68k_speed = 4;
    currprefs.cpu_level = 0;
    currprefs.cpu_compatible = 0;
    currprefs.address_space_24 = 1;
};
struct uae_prefs changed_prefs;

int no_gui = 0, use_debugger = 0, use_gfxlib = 0;

int joystickpresent = 0;

int cloanto_rom = 0;

char warning_buffer[256];

uae_u32 fastmem_size = 0x00000000;
uae_u32 a3000mem_size = 0x00000000;
uae_u32 z3fastmem_size = 0x00000000;
uae_u32 chipmem_size = 0x00200000;
uae_u32 bogomem_size = 0x00000000;
uae_u32 gfxmem_size = 0x00000000;

char romfile[256] = "kick.rom";
char keyfile[256] = "";
char prtname[256] = DEFPRTNAME;

char optionsfile[256];

/* If you want to pipe printer output to a file, put something like
 * "cat >>printerfile.tmp" above.
 * The printer support was only tested with the driver "PostScript" on
 * Amiga side, using apsfilter for linux to print ps-data.
 *
 * Under DOS it ought to be -p LPT1: or -p PRN: but you'll need a
 * PostScript printer or ghostscript -=SR=-
 */

/* People must provide their own name for this */
char sername[256] = "";

/* Slightly stupid place for this... */
/* ncurses.c might use quite a few of those. */
char *colormodes[] = { "256 colors", "32768 colors", "65536 colors",
    "256 colors dithered", "16 colors dithered", "16 million colors",
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", ""
};

static void fix_options(void)
{
    int err = 0;

    if ((chipmem_size & (chipmem_size - 1)) != 0
	|| chipmem_size < 0x80000
	|| chipmem_size > 0x800000)
    {
	chipmem_size = 0x200000;
	fprintf (stderr, "Unsupported chipmem size!\n");
	err = 1;
    }
    if ((fastmem_size & (fastmem_size - 1)) != 0
	|| (fastmem_size != 0 && (fastmem_size < 0x100000 || fastmem_size > 0x800000)))
    {
	fastmem_size = 0;
	fprintf (stderr, "Unsupported fastmem size!\n");
	err = 1;
    }
    if ((gfxmem_size & (gfxmem_size - 1)) != 0
	|| (gfxmem_size != 0 && (gfxmem_size < 0x100000 || gfxmem_size > 0x800000)))
    {
	gfxmem_size = 0;
	fprintf (stderr, "Unsupported graphics card memory size!\n");
	err = 1;
    }
    if ((z3fastmem_size & (z3fastmem_size - 1)) != 0
	|| (z3fastmem_size != 0 && (z3fastmem_size < 0x100000 || z3fastmem_size > 0x4000000)))
    {
	z3fastmem_size = 0;
	fprintf (stderr, "Unsupported Zorro III fastmem size!\n");
	err = 1;
    }
    if (currprefs.address_space_24 && (gfxmem_size != 0 || z3fastmem_size != 0)) {
	z3fastmem_size = gfxmem_size = 0;
	fprintf (stderr, "Can't use a graphics card or Zorro III fastmem when using a 24 bit\n"
		 "address space - sorry.\n");
    }
    if ((bogomem_size & (bogomem_size - 1)) != 0
	|| (bogomem_size != 0 && (bogomem_size < 0x80000 || bogomem_size > 0x100000)))
    {
	bogomem_size = 0;
	fprintf (stderr, "Unsupported bogomem size!\n");
	err = 1;
    }

    if (chipmem_size > 0x200000 && fastmem_size != 0) {
	fprintf (stderr, "You can't use fastmem and more than 2MB chip at the same time!\n");
	fastmem_size = 0;
	err = 1;
    }
    if (currprefs.m68k_speed < 1 || currprefs.m68k_speed > 20) {
	fprintf (stderr, "Bad value for -w parameter: must be within 1..20.\n");
	currprefs.m68k_speed = 4;
	err = 1;
    }
    if (currprefs.produce_sound < 0 || currprefs.produce_sound > 3) {
	fprintf (stderr, "Bad value for -S parameter: enable value must be within 0..3\n");
	currprefs.produce_sound = 0;
	err = 1;
    }
    if (currprefs.cpu_level < 2 && z3fastmem_size > 0) {
	fprintf (stderr, "Z3 fast memory can't be used with a 68000/68010 emulation. It\n"
		 "requires a 68020 emulation. Turning off Z3 fast memory.\n");
	z3fastmem_size = 0;
	err = 1;
    }
    if (currprefs.cpu_level < 2 && gfxmem_size > 0) {
	fprintf (stderr, "Picasso96 can't be used with a 68000/68010 emulation. It\n"
		 "requires a 68020 emulation. Turning off Picasso96.\n");
	gfxmem_size = 0;
	err = 1;
    }

    if (err)
	fprintf (stderr, "Please use \"uae -h\" to get usage information.\n");
}

int quit_program = 0;

void uae_reset (void)
{
    if (quit_program != 1 && quit_program != -1)
	quit_program = -2;
}

void uae_quit (void)
{
    if (quit_program != -1)
	quit_program = -1;
}

/*
 * This function is dangerous, at least if you use different versions of UAE
 * from time to time (as I do). For example, running the SVGAlib version with
 * "-o" will discard the keyboard language information...
 */
void save_options(FILE *f)
{
    int i;
    if (use_debugger)
	fprintf (f, "-D\n");
    fprintf (f, "-r %s\n", romfile);
    if (strlen (keyfile) > 0)
	fprintf (f, "-K %s\n", keyfile);
    for (i = 0; i < 4; i++)
	if (strlen (currprefs.df[i]) > 0)
	    fprintf (f, "-%d %s\n", i, currprefs.df[i]);

#ifndef UNSUPPORTED_OPTION_p
    fprintf (f, "-p %s\n", prtname);
#endif
#ifndef UNSUPPORTED_OPTION_S
    fprintf (f, "-S %d:%c:%d:%d:%d:%d\n", currprefs.produce_sound,
	    currprefs.stereo ? 's' : 'm',
	    currprefs.sound_bits,
	    currprefs.sound_freq,
	    currprefs.sound_maxbsiz,
	    currprefs.sound_minbsiz);
#endif
    if (currprefs.fake_joystick)
	fprintf (f, "-J %c%c\n", "01MABC"[currprefs.fake_joystick & 255],
		"01MABC"[(currprefs.fake_joystick >> 8) & 255]);
    fprintf (f, "-f %d\n", currprefs.framerate);
    fprintf (f, "-O %d:%d:", currprefs.gfx_width, currprefs.gfx_height);
    if (currprefs.gfx_lores)
	fprintf (f, "l");
    switch (currprefs.gfx_xcenter) {
     case 1: fprintf (f, "x"); break;
     case 2: fprintf (f, "X"); break;
     default: break;
    }
    switch (currprefs.gfx_ycenter) {
     case 1: fprintf (f, "y"); break;
     case 2: fprintf (f, "Y"); break;
     default: break;
    }
    if (currprefs.gfx_linedbl)
	fprintf (f, "d");
    if (currprefs.gfx_correct_aspect)
	fprintf (f, "c");
    fprintf (f, "\n-H %d\n", currprefs.color_mode);
    if (fastmem_size > 0)
	fprintf (f, "-F %d\n", fastmem_size / 0x100000);
    if (z3fastmem_size > 0)
	fprintf (f, "-Z %d\n", z3fastmem_size / 0x100000);
    if (bogomem_size > 0)
	fprintf (f, "-s %d\n", bogomem_size / 0x40000);
    if (gfxmem_size > 0)
	fprintf (f, "-U %d\n", gfxmem_size / 0x100000);
    fprintf (f, "-c %d\n", chipmem_size / 0x80000);

    fprintf (f, "-C %d%s\n", currprefs.cpu_level,
	     (currprefs.cpu_compatible ? "c"
	      : currprefs.address_space_24 && currprefs.cpu_level > 1 ? "a"
	      : ""));

    if (!currprefs.automount_uaedev)
	fprintf (f, "-a\n");
    if (currprefs.use_low_bandwidth)
	fprintf (f, "-L\n");
    if (currprefs.use_mitshm)
	fprintf (f, "-T\n");
    fprintf (f, "-w %d\n", currprefs.m68k_speed);
    fprintf (f, "-A %d\n", currprefs.emul_accuracy);
    /* We don't write "-t" - I can hardly imagine a user who wants that in his
     * config file. */
    write_filesys_config(f);
#ifndef UNSUPPORTED_OPTION_l
    fprintf (f, "-l %s\n", (currprefs.keyboard_lang == KBD_LANG_DE ? "de"
			   : currprefs.keyboard_lang == KBD_LANG_ES ? "es"
			   : currprefs.keyboard_lang == KBD_LANG_US ? "us"
			   : currprefs.keyboard_lang == KBD_LANG_SE ? "se"
			   : currprefs.keyboard_lang == KBD_LANG_FR ? "fr"
			   : currprefs.keyboard_lang == KBD_LANG_IT ? "it"
			   : "FOO"));
#endif
}

struct optiondef {
    const char *getopt_str;
    const char *helpstr;
};


static struct optiondef uaeopts[] = {
    { "h",	  "  -h           : Print help\n" },
#ifndef UNSUPPORTED_OPTION_l
    { "l:",	  "  -l lang      : Set keyboard language to lang, where lang is\n"
		  "                 DE, SE, US, ES, FR or IT\n" },
#endif
    { "m:",	  "  -m VOL:dir   : Mount directory called <dir> as AmigaDOS volume VOL:\n" },
    { "M:",	  "  -M VOL:dir   : like -m, but mount read-only\n" },
    { "W:",	  "  -W specs     : Mount a hardfile. The \"specs\" parameter consists of\n"
		  "                 \"sectors per track:number of surfaces:filename\"\n" },
    { "s:",	  "  -s n         : Emulate n*256 KB slow memory at 0xC00000\n" },
    { "c:",	  "  -c n         : Emulate n*512 KB chip memory at 0x000000\n" },
    { "F:",	  "  -F n         : Emulate n MB fast memory at 0x200000\n" },
    { "Z:",	  "  -Z n         : Emulate n MB Zorro III fast memory\n" },
#if defined PICASSO96
    { "U:",	  "  -U n         : Emulate a Picasso96 compatible graphics card with n MB memory\n" },
#endif
    { "w:",	  "  -w n         : Set CPU speed to n (default 4)\n" },
    { "a",	  "  -a           : Add no expansion devices (disables fastmem and\n"
		  "                 harddisk support\n" },
    { "J:",	  "  -J xy        : Specify how to emulate joystick port 0 (x) and 1 (y)\n"
		  "                 Use 0 for joystick 0, 1 for joystick 1, M for mouse,\n"
		  "                 a/b/c for various keyboard replacements\n" },
    { "f:",	  "  -f n         : Set the frame rate to 1/n (draw only every nth frame)\n" },
#ifndef UNSUPPORTED_OPTION_D
    { "D",	  "  -D           : Start up the built-in debugger\n" },
#endif
    { "i",	  "  -i           : Print illegal memory accesses\n" },
    { "t",	  "  -t           : Test drawing speed (makes the emulator very slow)\n" },
#ifndef UNSUPPORTED_OPTION_G
    { "G",	  "  -G           : Disable user interface\n" },
#endif
    { "A:",	  "  -A n         : Set emulator accuracy to n (0, 1 or 2)\n" },
#ifdef USE_EXECLIB
    { "g",	  "  -g           : Turn on gfx-lib replacement (EXPERIMENTAL).\n" },
#endif
    { "C:",	  "  -C parms     : Set CPU parameters. '0' for 68000 emulation, '1' for\n"
		  "                 68010, '2' for 68020, '3' for 68020/68881. Additionally,\n"
	          "                 you can specify 'c' for a slow but compatible emulation\n"
	          "                 and 'a' for a 24 bit address space.\n" },
    { "n:",	  "  -n parms     : Set blitter parameters: 'i' enables immediate blits,\n"
		  "                 '3' enables 32 bit blits (may crash RISC machines)\n" },
    { "0:1:2:3:", "  -[0123] file : Use file instead of df[0123].adf as disk image\n" },
    { "r:",	  "  -r file      : Use file as ROM image instead of kick.rom\n" },
    { "K:",	  "  -K file      : Use file as ROM key file for encrypted ROMs.\n" },
#ifndef UNSUPPORTED_OPTION_S
    { "S:",	  "  -S spec      : Set parameters for sound emulation (see below)\n" },
#endif
#ifndef UNSUPPORTED_OPTION_p
    { "p:",	  "  -p prt       : Use <prt> for printer output (default " DEFPRTNAME ").\n" },
#endif
#ifndef UNSUPPORTED_OPTION_I
    { "I:",	  "  -I device    : Use <device> for serial output (e.g. " DEFSERNAME ").\n" },
    { "d:",       "  -d [s][p]    : Open serial/parallel port on demand only.\n" },
#endif
#ifdef TARGET_SPECIAL_OPTIONS
    TARGET_SPECIAL_OPTIONS
#endif
    { "O:",	  "  -O modespec  : Define graphics mode (see below)\n" },
    { "H:",	  "  -H mode      : Set the color mode (see below)\n" }
};

void usage(void)
{
    int i;

    printf ("UAE - The Un*x Amiga emulator\n");
    printf ("Summary of command-line options (please read the README for more details):\n");
    for (i = 0; i < sizeof uaeopts / sizeof *uaeopts; i++)
	printf ("%s", uaeopts[i].helpstr);

    printf ("\n");
    printf ("%s",
	   "The format for the \"-S\" sound spec parameter is as follows:\n"
	   "  -S enable:stereo:bits:freq:maxbuffer:minbuffer\n"
	   "  You do not need to give the full specs, you can terminate after every field.\n"
	   "  enable: Either 0 (no sound), 1 (emulated, but not output), 2 (emulated), or\n"
	   "    3 (completely accurate emulation). The suggested value is 2.\n"
	   "  stereo: Either 's' (stereo) or 'm' (mono).\n"
	   "  bits: Number of bits to use for sound output, usually 8 or 16\n"
	   "  freq: Frequency for sound output; usually 44100 or 22050\n"
	   "  maxbuffer: Maximum buffer size for sound output\n"
	   "  minbuffer: Minimum buffer size for sound output\n"
	   "Larger buffers need less computing power, but the sound will be delayed.\n"
	   "Some of these settings will have no effect on certain ports of UAE.\n");
    printf ("%s",
#ifndef COLOR_MODE_HELP_STRING
	   "Valid color modes: 0 (256 colors); 1 (32768 colors); 2 (65536 colors)\n"
	   "                   3 (256 colors, with dithering for better results)\n"
	   "                   4 (16 colors, dithered); 5 (16 million colors)\n"
#else
	   COLOR_MODE_HELP_STRING
#endif
	   );
    printf ("%s",
	   "The format for the modespec parameter of \"-O\" is as follows:\n"
	   "  -O width:height:modifiers\n"
	   "  \"width\" and \"height\" specify the dimensions of the picture.\n"
	   "  \"modifiers\" is a string that contains zero or more of the following\n"
	   "  characters:\n"
	   "   l:    Treat the display as lores, drawing only every second pixel\n"
	   "   x, y: Center the screen horizontally or vertically.\n"
	   "   d:    Doubles the height of the display for better interlace emulation\n"
	   "   c:    Correct aspect ratio\n"
	   "UAE may choose to ignore the color mode setting and/or adjust the\n"
	   "video mode setting to reasonable values.\n");
}

static void parse_gfx_specs (char *spec)
{
    char *x0 = my_strdup (spec);
    char *x1, *x2;

    x1 = strchr (x0, ':');
    if (x1 == 0)
	goto argh;
    x2 = strchr (x1+1, ':');
    if (x2 == 0)
	goto argh;
    *x1++ = 0; *x2++ = 0;

    currprefs.gfx_width = atoi (x0);
    currprefs.gfx_height = atoi (x1);
    currprefs.gfx_lores = strchr (x2, 'l') != 0;
    currprefs.gfx_xcenter = strchr (x2, 'x') != 0 ? 1 : strchr (x2, 'X') != 0 ? 2 : 0;
    currprefs.gfx_ycenter = strchr (x2, 'y') != 0 ? 1 : strchr (x2, 'Y') != 0 ? 2 : 0;
    currprefs.gfx_linedbl = strchr (x2, 'd') != 0;
    currprefs.gfx_correct_aspect = strchr (x2, 'c') != 0;

    free (x0);
    return;

    argh:
    fprintf (stderr, "Bad display mode specification.\n");
    fprintf (stderr, "The format to use is: \"width:height:modifiers\"\n");
    fprintf (stderr, "Type \"uae -h\" for detailed help.\n");
    free (x0);
}

static void parse_sound_spec (char *spec)
{
    char *x0 = my_strdup (spec);
    char *x1, *x2 = NULL, *x3 = NULL, *x4 = NULL, *x5 = NULL;

    x1 = strchr (x0, ':');
    if (x1 != NULL) {
	*x1++ = '\0';
	x2 = strchr (x1 + 1, ':');
	if (x2 != NULL) {
	    *x2++ = '\0';
	    x3 = strchr (x2 + 1, ':');
	    if (x3 != NULL) {
		*x3++ = '\0';
		x4 = strchr (x3 + 1, ':');
		if (x4 != NULL) {
		    *x4++ = '\0';
		    x5 = strchr (x4 + 1, ':');
		}
	    }
	}
    }
    currprefs.produce_sound = atoi (x0);
    if (x1) {
	if (*x1 == 's')
	    currprefs.stereo = 1;
	else
	    currprefs.stereo = 0;
    }
    if (x2)
	currprefs.sound_bits = atoi (x2);
    if (x3)
	currprefs.sound_freq = atoi (x3);
    if (x4)
	currprefs.sound_maxbsiz = currprefs.sound_minbsiz = atoi (x4);
    if (x5)
	currprefs.sound_minbsiz = atoi (x5);
    free (x0);
    return;
}

const char *gameport_state (int nr)
{
    if (JSEM_ISJOY0 (nr, currprefs.fake_joystick) && nr_joysticks > 0)
	return "using joystick #0";
    else if (JSEM_ISJOY1 (nr, currprefs.fake_joystick) && nr_joysticks > 1)
	return "using joystick #1";
    else if (JSEM_ISMOUSE (nr, currprefs.fake_joystick))
	return "using mouse";
    else if (JSEM_ISNUMPAD (nr, currprefs.fake_joystick))
	return "using numeric pad as joystick";
    else if (JSEM_ISCURSOR (nr, currprefs.fake_joystick))
	return "using cursor keys as joystick";
    else if (JSEM_ISSOMEWHEREELSE (nr, currprefs.fake_joystick))
	return "using T/F/H/B/Alt as joystick";

    return "not connected";
}

static int parse_joy_spec (char *spec)
{
    int v0 = 2, v1 = 0;
    if (strlen(spec) != 2)
	goto bad;

    switch (spec[0]) {
     case '0': v0 = 0; break;
     case '1': v0 = 1; break;
     case 'M': case 'm': v0 = 2; break;
     case 'A': case 'a': v0 = 3; break;
     case 'B': case 'b': v0 = 4; break;
     case 'C': case 'c': v0 = 5; break;
     default: goto bad;
    }

    switch (spec[1]) {
     case '0': v1 = 0; break;
     case '1': v1 = 1; break;
     case 'M': case 'm': v1 = 2; break;
     case 'A': case 'a': v1 = 3; break;
     case 'B': case 'b': v1 = 4; break;
     case 'C': case 'c': v1 = 5; break;
     default: goto bad;
    }
    if (v0 == v1)
	goto bad;
    /* Let's scare Pascal programmers */
    if (0)
bad:
    fprintf (stderr, "Bad joystick mode specification. Use -J xy, where x and y\n"
	     "can be 0 for joystick 0, 1 for joystick 1, M for mouse, and\n"
	     "a, b or c for different keyboard settings.\n");

    return v0 + (v1 << 8);
}

static void parse_filesys_spec (int readonly, char *spec)
{
    char buf[256];
    char *s2;

    strncpy(buf, spec, 255); buf[255] = 0;
    s2 = strchr(buf, ':');
    if(s2) {
	*s2++ = '\0';
#ifdef __DOS__
	{
	    char *tmp;

	    while ((tmp = strchr(s2, '\\')))
		*tmp = '/';
	}
#endif
	s2 = add_filesys_unit(buf, s2, readonly, 0, 0, 0);
	if (s2)
	    fprintf (stderr, "%s\n", s2);
    } else {
	fprintf (stderr, "Usage: [-m | -M] VOLNAME:mount_point\n");
    }
}

static void parse_hardfile_spec (char *spec)
{
    char *x0 = my_strdup (spec);
    char *x1, *x2, *x3;

    x1 = strchr (x0, ':');
    if (x1 == NULL)
	goto argh;
    *x1++ = '\0';
    x2 = strchr (x1 + 1, ':');
    if (x2 == NULL)
	goto argh;
    *x2++ = '\0';
    x3 = strchr (x2 + 1, ':');
    if (x3 == NULL)
	goto argh;
    *x3++ = '\0';
    x3 = add_filesys_unit (0, x3, 0, atoi (x0), atoi (x1), atoi (x2));
    if (x3)
	fprintf (stderr, "%s\n", x3);

    free (x0);
    return;

    argh:
    free (x0);
    fprintf (stderr, "Bad hardfile parameter specified - type \"uae -h\" for help.\n");
    return;
}

static void parse_cpu_specs (char *spec)
{
    if (*spec < '0' || *spec > '3') {
	fprintf (stderr, "CPU parameter string must begin with '0', '1', '2' or '3'.\n");
	return;
    }
	
    currprefs.cpu_level = *spec++ - '0';
    currprefs.address_space_24 = currprefs.cpu_level < 2;
    currprefs.cpu_compatible = 0;
    while (*spec != '\0') {
	switch (*spec) {
	 case 'a':
	    if (currprefs.cpu_level < 2)
		fprintf (stderr, "In 68000/68010 emulation, the address space is always 24 bit.\n");
	    else
		currprefs.address_space_24 = 1;
	    break;
	 case 'c':
	    if (currprefs.cpu_level != 0)
		fprintf (stderr, "The more compatible CPU emulation is only available for 68000\n"
			 "emulation, not for 68010 upwards.\n");
	    else
		currprefs.cpu_compatible = 1;
	    break;
	 default:
	    fprintf (stderr, "Bad CPU parameter specified - type \"uae -h\" for help.\n");
	    break;
	}
	spec++;
    }
}

#ifndef DONT_PARSE_CMDLINE

void parse_cmdline(int argc, char **argv)
{
    int c, i;
    char getopt_str[256];
    strcpy (getopt_str, "");

    for (i = 0; i < sizeof uaeopts / sizeof *uaeopts; i++)
	strcat (getopt_str, uaeopts[i].getopt_str);

    /* Help! We're running out of letters! */
    while(((c = getopt(argc, argv, getopt_str)) != EOF))
    switch(c) {
     case 'h': usage();	exit(0);

     case '0': strncpy (currprefs.df[0], optarg, 255); currprefs.df[0][255] = 0; break;
     case '1': strncpy (currprefs.df[1], optarg, 255); currprefs.df[1][255] = 0; break;
     case '2': strncpy (currprefs.df[2], optarg, 255); currprefs.df[2][255] = 0; break;
     case '3': strncpy (currprefs.df[3], optarg, 255); currprefs.df[3][255] = 0; break;
     case 'r': strncpy (romfile, optarg, 255); romfile[255] = 0; break;
     case 'K': strncpy (keyfile, optarg, 255); keyfile[255] = 0; break;
     case 'p': strncpy (prtname, optarg, 255); prtname[255] = 0; break;
     case 'I': strncpy (sername, optarg, 255); sername[255] = 0; currprefs.use_serial = 1; break;
     case 'm': case 'M': parse_filesys_spec (c == 'M', optarg); break;
     case 'W': parse_hardfile_spec (optarg); break;
     case 'S': parse_sound_spec (optarg); break;
     case 'f': currprefs.framerate = atoi (optarg); break;
     case 'A': currprefs.emul_accuracy = atoi (optarg); break;
     case 'x': currprefs.no_xhair = 1; break;
     case 'i': currprefs.illegal_mem = 1; break;
     case 'J': currprefs.fake_joystick = parse_joy_spec (optarg); break;
     case 'a': currprefs.automount_uaedev = 0; break;

     case 't': currprefs.test_drawing_speed = 1; break;
     case 'L': currprefs.use_low_bandwidth = 1; break;
     case 'T': currprefs.use_mitshm = 1; break;
     case 'w': currprefs.m68k_speed = atoi (optarg); break;

     case 'g': use_gfxlib = 1; break;
     case 'G': no_gui = 1; break;
     case 'D': use_debugger = 1; break;

     case 'n':
	if (strchr (optarg, '3') != 0)
	    currprefs.blits_32bit_enabled = 1;
	if (strchr (optarg, 'i') != 0)
	    currprefs.immediate_blits = 1;
	break;

     case 'C':
	parse_cpu_specs (optarg);
	break;

     case 'Z':
	z3fastmem_size = atoi (optarg) * 0x100000;
	break;

     case 'U':
	gfxmem_size = atoi (optarg) * 0x100000;
	break;

     case 'F':
	fastmem_size = atoi (optarg) * 0x100000;
	break;

     case 's':
	bogomem_size = atoi (optarg) * 0x40000;
	break;

     case 'c':
	chipmem_size = atoi (optarg) * 0x80000;
	break;

     case 'l':
	if (0 == strcasecmp(optarg, "de"))
	    currprefs.keyboard_lang = KBD_LANG_DE;
	else if (0 == strcasecmp(optarg, "us"))
	    currprefs.keyboard_lang = KBD_LANG_US;
	else if (0 == strcasecmp(optarg, "se"))
	    currprefs.keyboard_lang = KBD_LANG_SE;
	else if (0 == strcasecmp(optarg, "fr"))
	    currprefs.keyboard_lang = KBD_LANG_FR;
	else if (0 == strcasecmp(optarg, "it"))
	    currprefs.keyboard_lang = KBD_LANG_IT;
	else if (0 == strcasecmp(optarg, "es"))
	    currprefs.keyboard_lang = KBD_LANG_ES;
	break;

     case 'O': parse_gfx_specs (optarg); break;
     case 'd':
	if (strchr (optarg, 'S') != NULL || strchr (optarg, 's')) {
	    write_log ("  Serial on demand.\n");
	    currprefs.serial_demand = 1;
	}
	if (strchr (optarg, 'P') != NULL || strchr (optarg, 'p')) {
	    write_log ("  Parallel on demand.\n");
	    currprefs.parallel_demand = 1;
	}

	break;

     case 'H':
	currprefs.color_mode = atoi (optarg);
	if (currprefs.color_mode < 0) {
	    fprintf (stderr, "Bad color mode selected. Using default.\n");
	    currprefs.color_mode = 0;
	}
	break;
    }
}
#endif

static void parse_cmdline_and_init_file(int argc, char **argv)
{
    FILE *f;
    char *tmp;
    int t;
    char *buffer,*tmpbuf, *token;
    char smallbuf[256];
    int bufsiz, result;
    int n_args;
    char **new_argv;
    int new_argc;
    char *home;

    strcpy(optionsfile,"");

#ifdef OPTIONS_IN_HOME
    home = getenv("HOME");
    if (home != NULL && strlen(home) < 240)
    {
	strcpy(optionsfile, home);
	strcat(optionsfile, "/");
    }
#endif

    strcat(optionsfile, OPTIONSFILENAME);

    f = fopen(optionsfile, "rb");
#ifdef OPTIONS_IN_HOME
    /* sam: if not found in $HOME then look in current directory */
    if (f == NULL) {
	strcpy (optionsfile, OPTIONSFILENAME);
	f = fopen(optionsfile, "rb");
    }
#endif

    if (f == NULL) {
	parse_cmdline(argc, argv);
	return;
    }
    fseek(f, 0, SEEK_END);
    bufsiz = ftell(f);
    fseek(f, 0, SEEK_SET);

    buffer = (char *)malloc(bufsiz+1);
    buffer[bufsiz] = 0;
    if (fread (buffer, 1, bufsiz, f) < bufsiz) {
	fprintf (stderr, "Error reading configuration file\n");
	fclose (f);
	parse_cmdline (argc, argv);
	return;
    }
    fclose(f);

    /* Canonicalize the buffer */
    while ((tmp = strchr (buffer, '\r')))
	*tmp = ' ';
    for (;;) {
	tmp = strchr (buffer, '\n');
	if (tmp == 0)
	    break;
	if (tmp[1] == '#') {
	    char *tmp2 = strchr (tmp + 1, '\n');
	    if (tmp2 == 0)
		*tmp = '\0';
	    else
		memmove (tmp, tmp2, strlen (tmp2) + 1);
	} else {
	    *tmp = ' ';
	}
    }
    while ((tmp = strchr(buffer, '\t')))
	*tmp = ' ';

    tmp = buffer;
    while (*tmp == ' ')
	tmp++;
    t = strlen (tmp);
    memmove (buffer, tmp, t + 1);

    while (t > 0 && buffer[t-1] == ' ')
	buffer[--t] = '\0';
    for (;;) {
	tmp = strstr (buffer, "  ");
	if (tmp == 0)
	    break;
	memmove (tmp, tmp + 1, strlen (tmp));
    }

    tmpbuf = my_strdup (buffer);

    n_args = 0;
    if (strtok(tmpbuf, "\n ") != NULL) {
	do {
	    n_args++;
	} while (strtok(NULL, "\n ") != NULL);
    }
    free (tmpbuf);

    new_argv = (char **)malloc ((1 + n_args + argc) * sizeof (char **));
    new_argv[0] = argv[0];
    new_argc = 1;

    token = strtok(buffer, "\n ");
    while (token != NULL) {
	new_argv[new_argc] = my_strdup (token);
	new_argc++;
	token = strtok(NULL, "\n ");
    }
    for (n_args = 1; n_args < argc; n_args++)
	new_argv[new_argc++] = argv[n_args];
    new_argv[new_argc] = NULL;
    parse_cmdline(new_argc, new_argv);
}

/* Okay, this stuff looks strange, but it is here to encourage people who
 * port UAE to re-use as much of this code as possible. Functions that you
 * should be using are do_start_program() and do_leave_program(), as well
 * as real_main(). Some OSes don't call main() (which is braindamaged IMHO,
 * but unfortunately very common), so you need to call real_main() from
 * whatever entry point you have. You may want to write your own versions
 * of start_program() and leave_program() if you need to do anything special.
 * Add #ifdefs around these as appropriate.
 */

void do_start_program(void)
{
    /* Do a reset on startup. Whether this is elegant is debatable. */
    quit_program = 2;
    m68k_go(1);
}

void do_leave_program(void)
{
    graphics_leave();
    close_joystick();
    close_sound();
    dump_counts();
    serial_exit();
    zfile_exit();
    if (!no_gui)
	gui_exit();
}

#ifndef _WIN32
void start_program(void)
{
    do_start_program();
}
#endif

void leave_program(void)
{
    do_leave_program();
}

void real_main(int argc, char **argv)
{
    FILE *hf;

    default_currprefs ();
    
    if (!graphics_setup()) {
	exit(1);
    }

    rtarea_init ();
    hardfile_install ();

    parse_cmdline_and_init_file(argc, argv);

    machdep_init();

    if (!setup_sound()) {
	fprintf (stderr, "Sound driver unavailable: Sound output disabled\n");
	currprefs.produce_sound = 0;
    }
    init_joystick();

    if (!no_gui) {
	int err = gui_init();
	if (err == -1) {
	    fprintf (stderr, "Failed to initialize the GUI\n");
	} else if (err == -2) {
	    exit(0);
	}
    }
    if (sound_available && !init_sound()) {
	fprintf (stderr, "Sound driver unavailable: Sound output disabled\n");
	currprefs.produce_sound = 0;
    }

    fix_options();
    changed_prefs = currprefs;

    /* Install resident module to get 8MB chipmem, if requested */
    rtarea_setup();

    keybuf_init(); /* Must come after init_joystick */

    expansion_init ();
    memory_init();

    filesys_install();
    gfxlib_install();
    emulib_install();
    uaeexe_install();

    custom_init(); /* Must come after memory_init */
    serial_init();
    DISK_init();
    init_m68k();
    compiler_init();
    gui_update();

    if (graphics_init ()) {
	setup_brkhandler ();
	filesys_start_threads ();
	if (use_debugger && debuggable ())
	    activate_debugger ();

	start_program ();
    }
    leave_program ();
}

#ifndef NO_MAIN_IN_MAIN_C
int main(int argc, char **argv)
{
    real_main(argc, argv);
    return 0;
}
#endif
