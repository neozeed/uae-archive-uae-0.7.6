 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Custom chip emulation
  *
  * Copyright 1995, 1996, 1997 Bernd Schmidt
  * Copyright 1995 Alessandro Bissacco
  */

 /*
  * This file should be split into two parts. There are two concepts of a
  * "frame" here: a hardware frame which emulates the display hardware of
  * the A500, and a "drawing" frame, which renders data on the screen. You
  * can have multiple hardware frames for one drawing frame, or completely
  * inhibit drawing while hardware frames continue to be emulated. There
  * should be a separate file "drawing.c" or something.
  */
#include "sysconfig.h"
#include "sysdeps.h"

#include <ctype.h>
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
#include "readcpu.h"
#include "newcpu.h"
#include "cia.h"
#include "disk.h"
#include "blitter.h"
#include "xwin.h"
#include "joystick.h"
#include "audio.h"
#include "keybuf.h"
#include "serial.h"
#include "osemu.h"
#include "autoconf.h"
#include "gui.h"
#include "picasso96.h"
#include "p2c.h"

#ifdef X86_ASSEMBLY
#define LORES_HACK
#endif
#define SPRITE_COLLISIONS

/*
 * Several people have tried this define, with not much success. Turning on
 * AGA garbles the screen. A place you could start looking is the calcdiw()
 * function - the AGA timing parameters are different, and apparently I
 * haven't figured out the correct formula yet. Pity, the current one looks
 * logical.
 *
 * @@@ Probably won't compile in this version.
 */

/* #define EMULATE_AGA */

#ifndef EMULATE_AGA
#define AGA_CHIPSET 0
#else
#define AGA_CHIPSET 1
#endif

#define SMART_UPDATE 1

#ifdef SUPPORT_PENGUINS
#undef SMART_UPDATE
#define SMART_UPDATE 1
#endif

#if AGA_CHIPSET == 1
#define MAX_PLANES 8
#else
#define MAX_PLANES 6
#endif

/* Fetched data spends 9 lores pixels somewhere in the chips before it appears
 * on-screen. We don't emulate this. Instead, we cheat with the copper to
 * compensate (much easier that way). */
#define COPPER_MAGIC_FUDGE -9
/* We ignore that many lores pixels at the start of the display. These are
 * invisible anyway due to hardware DDF limits. */
#define DISPLAY_LEFT_SHIFT 0x38
static int lores_factor, lores_shift, sprite_width;

#define PIXEL_XPOS(HPOS) (((HPOS)*2 - DISPLAY_LEFT_SHIFT) << lores_shift)

/* @@@ Is maxhpos + 4 - 1 correct? (4 less isn't enough) */
#define max_diwlastword (PIXEL_XPOS(maxhpos + 4 -1))

/* Mouse and joystick emulation */

#define defstepx (1<<16)
#define defstepy (1<<16)
#define defxoffs 0
#define defyoffs 0

static const int docal = 60, xcaloff = 40, ycaloff = 20;
static const int calweight = 3;
static int lastsampledmx, lastsampledmy;

int buttonstate[3];
int lastmx, lastmy;
int newmousecounters;
int ievent_alive = 0;

static enum { normal_mouse, dont_care_mouse, follow_mouse } mousestate;

static int mouse_x, mouse_y;
int joy0button, joy1button;
unsigned int joy0dir, joy1dir;
static int lastspr0x,lastspr0y,lastdiffx,lastdiffy,spr0pos,spr0ctl;
static int mstepx,mstepy,xoffs=defxoffs,yoffs=defyoffs;
static int sprvbfl;

/* Video buffer description structure. Filled in by the graphics system
 * dependent code. */

struct vidbuf_description gfxvidinfo;

/* Events */

unsigned long int cycles, nextevent, is_lastline, did_reset;
struct ev eventtab[ev_max];

frame_time_t vsynctime, vsyncmintime;

int vpos;
uae_u16 lof;
static int lof_changed = 0, interlace_seen = 0;

static int copper_waiting_for_blitter, copper_active;
static const int dskdelay = 2; /* FIXME: ??? */

static int dblpf_ind1[256], dblpf_ind2[256], dblpf_2nd1[256], dblpf_2nd2[256];
static int dblpf_aga1[256], dblpf_aga2[256], linear_map_256[256], lots_of_twos[256];

static int dblpfofs[] = { 0, 2, 4, 8, 16, 32, 64, 128 };

/* Big lookup tables for planar to chunky conversion. */

uae_u32 hirestab_h[256][2];
uae_u32 lorestab_h[256][4];

uae_u32 hirestab_l[256][1];
uae_u32 lorestab_l[256][2];

static uae_u32 sprtaba[256],sprtabb[256];
static uae_u32 clxtab[256];

/* The color lookup table. */

xcolnr xcolors[4096];

/*
 * Hardware registers of all sorts.
 */

static int fmode;

static uae_u16 cregs[256];

uae_u16 intena,intreq;
uae_u16 dmacon;
uae_u16 adkcon; /* used by audio code */

static uae_u32 cop1lc,cop2lc,copcon;

/* This is but an educated guess. It seems to be correct, but this stuff
 * isn't documented well. */
enum sprstate { SPR_stop, SPR_restart, SPR_waiting_start, SPR_waiting_stop };
static enum sprstate sprst[8];
static int spron[8];
static uaecptr sprpt[8];
static int sprxpos[8], sprvstart[8], sprvstop[8];

static uae_u32 bpl1dat,bpl2dat,bpl3dat,bpl4dat,bpl5dat,bpl6dat,bpl7dat,bpl8dat;
static uae_s16 bpl1mod,bpl2mod;

static uaecptr bplpt[8];
#ifndef SMART_UPDATE
static char *real_bplpt[8];
#endif

/*static int blitcount[256];  blitter debug */

struct color_entry {
#if AGA_CHIPSET == 0
    /* X86.S expects this at the start of the structure. */
    xcolnr acolors[32];
    uae_u16 color_regs[32];
#else
    uae_u32 color_regs[256];
#endif
};

static struct color_entry current_colors;
struct color_entry colors_for_drawing;
static unsigned int bplcon0,bplcon1,bplcon2,bplcon3,bplcon4;
static int nr_planes_from_bplcon0, corrected_nr_planes_from_bplcon0;
static unsigned int diwstrt,diwstop,ddfstrt,ddfstop;
static unsigned int sprdata[8], sprdatb[8], sprctl[8], sprpos[8];
static int sprarmed[8], sprite_last_drawn_at[8];
static int last_sprite_point, nr_armed;
static uae_u16 clxdat, clxcon;
static int clx_sprmask;
static uae_u32 dskpt;
static uae_u16 dsklen,dsksync;

static uae_u32 coplc;
static unsigned int copi1,copi2;

static enum copper_states {
    COP_stop, COP_read, COP_do_read, COP_read_ignore, COP_do_read_ignore, COP_wait, COP_morewait, COP_move, COP_skip
} copstate;
/* The time the copper needs for one cycle depends on bitplane DMA. Whenever
 * possible, we calculate a fixed value and store it here. */
static int copper_cycle_time;

static int dsklength;

static int plffirstline,plflastline,plfstrt,plfstop,plflinelen;
static int diwfirstword,diwlastword;
static enum { DIW_waiting_start, DIW_waiting_stop } diwstate, hdiwstate;

int dskdmaen; /* used in cia.c */

/* 880 isn't a magic number, it's a safe number with some padding at the end.
 * This used to be 1000, but that's excessive. (840 is too low). I'm too lazy
 * to figure out the exact space needed. */
union {
    /* Let's try to align this thing. */
    double uupzuq;
    long int cruxmedo;
    unsigned char apixels[880];
} pixdata;

uae_u32 ham_linebuf[880];
uae_u32 aga_linebuf[880], *aga_lbufptr;

char *xlinebuffer;
int next_lineno;
static int nln_how;

static int *amiga2aspect_line_map, *native2amiga_line_map;
static char *row_map[2049];
static int max_drawn_amiga_line;

/*
 * Statistics
 */

/* Used also by bebox.cpp */
unsigned long int msecs = 0, frametime = 0, timeframes = 0;
static unsigned long int seconds_base;
int bogusframe;

/*
 * helper functions
 */

int picasso_requested_on;
int picasso_on;

uae_sem_t frame_sem, gui_sem, ihf_sem;
volatile int frame_do_semup;
volatile int inhibit_frame;

static int framecnt = 0;
static int frame_redraw_necessary;

static __inline__ void count_frame (void)
{
    framecnt++;
    if (framecnt >= currprefs.framerate)
	framecnt = 0;
}

void check_prefs_changed_custom (void)
{
    currprefs.framerate = changed_prefs.framerate;
    /* Not really the right place... */
    if (currprefs.fake_joystick != changed_prefs.fake_joystick) {	
	currprefs.fake_joystick = changed_prefs.fake_joystick;
	joystick_setting_changed ();
    }
    currprefs.immediate_blits = changed_prefs.immediate_blits;
    currprefs.blits_32bit_enabled = changed_prefs.blits_32bit_enabled;
	
}

static __inline__ void setclr (uae_u16 *p, uae_u16 val)
{
    if (val & 0x8000) {
	*p |= val & 0x7FFF;
    } else {
	*p &= ~val;
    }
}

__inline__ int current_hpos (void)
{
    return cycles - eventtab[ev_hsync].oldcycles;
}

static __inline__ uae_u8 *pfield_xlateptr (uaecptr plpt, int bytecount)
{
    if (!chipmem_bank.check(plpt,bytecount)) {
	static int count = 0;
	if (!count)
	    count++, write_log ("Warning: Bad playfield pointer\n");
	return NULL;
    }
    return chipmem_bank.xlateaddr(plpt);
}

static void calculate_copper_cycle_time (void)
{
    if (diwstate == DIW_waiting_start || !dmaen (DMA_BITPLANE)) {
	copper_cycle_time = 2;
	return;
    }
    copper_cycle_time = corrected_nr_planes_from_bplcon0 <= 4 ? 2 : -1;
}

/* line_draw_funcs: pfield_do_linetoscr, pfield_do_fill_line, decode_ham6 */
typedef void (*line_draw_func)(int, int);

#define LINE_UNDECIDED 1
#define LINE_DECIDED 2
#define LINE_DECIDED_DOUBLE 3
#define LINE_AS_PREVIOUS 4
#define LINE_BORDER_NEXT 5
#define LINE_BORDER_PREV 6
#define LINE_DONE 7
#define LINE_DONE_AS_PREVIOUS 8
#define LINE_REMEMBERED_AS_PREVIOUS 9

static char *line_drawn;
static char linestate[(maxvpos + 1)*2 + 1];

static int min_diwstart, max_diwstop, prev_x_adjust, linetoscr_x_adjust, linetoscr_right_x;
static int linetoscr_x_adjust_bytes;
static int thisframe_y_adjust, prev_y_adjust, thisframe_first_drawn_line, thisframe_last_drawn_line;
static int thisframe_y_adjust_real, max_ypos_thisframe, min_ypos_for_screen;
static int extra_y_adjust;

static uae_u8 line_data[(maxvpos+1) * 2][MAX_PLANES * MAX_WORDS_PER_LINE * 2];

/*
 * The idea behind this code is that at some point during each horizontal
 * line, we decide how to draw this line. There are many more-or-less
 * independent decisions, each of which can be taken at a different horizontal
 * position.
 * Sprites, color changes and bitplane delay changes are handled specially:
 * There isn't a single decision, but a list of structures containing
 * information on how to draw the line.
 */

struct color_change {
    int linepos;
    int regno;
    unsigned long value;
};

struct sprite_draw {
    int linepos;
    int num;
    int ctl;
    uae_u32 datab;
};

struct delay_change {
    int linepos;
    unsigned int value;
};

/* Way too much... */
#define MAX_REG_CHANGE ((maxvpos+1) * 2 * maxhpos)
static int current_change_set;

#ifdef OS_WITHOUT_MEMORY_MANAGEMENT
/* sam: Those arrays uses around 7Mb of BSS... That seems  */
/* too much for AmigaDOS (uae crashes as soon as one loads */
/* it. So I use a different strategy here (realloc the     */
/* arrays when needed. That strategy might be usefull for  */
/* computer with low memory.                               */
static struct sprite_draw  *sprite_positions[2];
static int max_sprite_draw    = 400;
static int delta_sprite_draw  = 0;
static struct color_change *color_changes[2];
static int max_color_change   = 400;
static int delta_color_change = 0;
static struct delay_change *delay_changes;
static int max_delay_change = 100;
static int delta_delay_change = 0;
#else
static struct sprite_draw sprite_positions[2][MAX_REG_CHANGE];
static struct color_change color_changes[2][MAX_REG_CHANGE];
/* We don't remember those across frames, that would be too much effort.
 * We simply redraw the line whenever we see one of these. */
static struct delay_change delay_changes[MAX_REG_CHANGE];
#endif
static struct sprite_draw *curr_sprite_positions, *prev_sprite_positions;
static struct color_change *curr_color_changes, *prev_color_changes;

static int next_color_change, next_sprite_draw, next_delay_change;

static struct color_entry color_tables[2][(maxvpos+1) * 2];
static struct color_entry *curr_color_tables, *prev_color_tables;
static int next_color_entry, remembered_color_entry, drawing_color_matches;
static enum { color_match_acolors, color_match_full } color_match_type;
static int color_src_match, color_dest_match, color_compare_result;

static int last_redraw_point;

static int first_drawn_line, last_drawn_line;
static int first_block_line, last_block_line;

static __inline__ void docols(struct color_entry *colentry)
{
#if AGA_CHIPSET == 0
    int i;
    for (i = 0; i < 32; i++) {
	int v = colentry->color_regs[i];
	if (v < 0 || v > 4095)
	  continue;
	colentry->acolors[i] = xcolors[v];
    }
#endif
}

void notice_new_xcolors (void)
{
    int i;

    docols(&current_colors);
    docols(&colors_for_drawing);
    for (i = 0; i < (maxvpos+1)*2; i++) {
	docols(color_tables[0]+i);
	docols(color_tables[1]+i);
    }
}

void notice_screen_contents_lost (void)
{
    frame_redraw_necessary = 2;
}

static void init_regchanges (void)
{
    size_t i;

    next_color_change = 0;
    next_sprite_draw = 0;
    current_change_set = 0;
    for (i = 0; i < sizeof linestate / sizeof *linestate; i++)
	linestate[i] = LINE_UNDECIDED;
}

/* struct decision contains things we save across drawing frames for
 * comparison (smart update stuff). */
struct decision {
    unsigned long color0;
    int which;
    int plfstrt, plflinelen;
    int diwfirstword, diwlastword;
    int ctable;

    uae_u16 bplcon0, bplcon1, bplcon2;
#if 0 /* We don't need this. */
    uae_u16 bplcon3;
#endif
#if AGA_CHIPSET == 1
    uae_u16 bplcon4;
#endif
};

/* Anything related to changes in hw registers during the DDF for one
 * line. */
struct draw_info {
    int first_sprite_draw, last_sprite_draw;
    int first_color_change, last_color_change;
    int first_delay_change, last_delay_change;
    int nr_color_changes, nr_sprites;
};

/* These few are only needed during/at the end of the scanline, and don't
 * have to be remembered. */
static int decided_bpl1mod, decided_bpl2mod, decided_nr_planes, decided_hires;

/* These are generated by the drawing code from the line_decisions array for
 * each line that needs to be drawn. */
static int bplehb, bplham, bpldualpf, bpldualpfpri, bplplanecnt, bplhires;
static int bpldelay1, bpldelay2;
static int plfpri[3];

static struct decision line_decisions[2 * (maxvpos+1) + 1];
static struct draw_info line_drawinfo[2][2 * (maxvpos+1) + 1];
static struct draw_info *curr_drawinfo, *prev_drawinfo;
static struct decision *dp_for_drawing;
static struct draw_info *dip_for_drawing;

static char line_changed[2 * (maxvpos+1)];

#ifdef SMART_UPDATE
#define MARK_LINE_CHANGED(l) do { line_changed[l] = 1; } while (0)
#else
#define MARK_LINE_CHANGED(l) do { } while (0)
#endif

static struct decision thisline_decision;
static int modulos_added, plane_decided, color_decided, very_broken_program;

static void do_sprites(int currvp, int currhp);

static void remember_ctable (void)
{
    if (remembered_color_entry == -1) {
	/* The colors changed since we last recorded a color map. Record a
	 * new one. */
	memcpy (curr_color_tables + next_color_entry, &current_colors, sizeof current_colors);
	remembered_color_entry = next_color_entry++;
    }
    thisline_decision.ctable = remembered_color_entry;
    if (color_src_match == -1 || color_dest_match != remembered_color_entry
	|| line_decisions[next_lineno].ctable != color_src_match)
    {
	/* The remembered comparison didn't help us - need to compare again. */
	int oldctable = line_decisions[next_lineno].ctable;
	int changed = 0;

	if (oldctable == -1) {
	    changed = 1;
	    color_src_match = color_dest_match = -1;
	} else {
	    color_compare_result = memcmp (&prev_color_tables[oldctable].color_regs,
					   &current_colors.color_regs,
					   sizeof current_colors.color_regs) != 0;
	    if (color_compare_result)
		changed = 1;
	    color_src_match = oldctable;
	    color_dest_match = remembered_color_entry;
	}
	if (changed) {
	    line_changed[next_lineno] = 1;
	}
    } else {
	/* We know the result of the comparison */
	if (color_compare_result)
	    line_changed[next_lineno] = 1;
    }
}

static void remember_ctable_for_border (void)
{
    remember_ctable ();
#if 0
    if (remembered_color_entry == -1) {
	/* The colors changed since we last recorded a color map. Record a
	 * new one. */
	memcpy (curr_color_tables + next_color_entry, &current_colors, sizeof current_colors);
	remembered_color_entry = next_color_entry++;
    }
    thisline_decision.ctable = remembered_color_entry;
    if (color_src_match == -1 || color_dest_match != remembered_color_entry
	|| line_decisions[next_lineno].ctable != color_src_match)
    {
	/* The remembered comparison didn't help us - need to compare again. */
	int oldctable = line_decisions[next_lineno].ctable;
	int changed = 0;

	if (oldctable == -1) {
	    changed = 1;
	    color_src_match = color_dest_match = -1;
	} else {
	    color_compare_result = memcmp (&prev_color_tables[oldctable].color_regs,
					   &current_colors.color_regs,
					   sizeof current_colors.color_regs) != 0;
	    if (color_compare_result)
		changed = 1;
	    color_src_match = oldctable;
	    color_dest_match = remembered_color_entry;
	}
	if (changed) {
	    line_changed[next_lineno] = 1;
	}
    } else {
	/* We know the result of the comparison */
	if (color_compare_result)
	    line_changed[next_lineno] = 1;
    }
#endif
}

/* Called to determine the state of the horizontal display window state
 * machine at the current position. It might have changed since we last
 * checked.  */
static void decide_diw (void)
{
    if (hdiwstate == DIW_waiting_start && thisline_decision.diwfirstword == -1
	&& PIXEL_XPOS (current_hpos ()) >= diwfirstword)
    {
	thisline_decision.diwfirstword = diwfirstword;
	hdiwstate = DIW_waiting_stop;
	/* Decide playfield delays only at DIW start, because they don't matter before and
	 * some programs change them after DDF start but before DIW start. */
	thisline_decision.bplcon1 = bplcon1;
	if (thisline_decision.diwfirstword != line_decisions[next_lineno].diwfirstword)
	    MARK_LINE_CHANGED (next_lineno);
	thisline_decision.diwlastword = -1;
    }
    if (hdiwstate == DIW_waiting_stop && thisline_decision.diwlastword == -1
	&& PIXEL_XPOS (current_hpos ()) >= diwlastword)
    {
	thisline_decision.diwlastword = diwlastword;
	hdiwstate = DIW_waiting_start;
	if (thisline_decision.diwlastword != line_decisions[next_lineno].diwlastword)
	    MARK_LINE_CHANGED (next_lineno);
    }
}

/* Called when we know that the line is not in the border and we want to it.
 * The data fetch starting position and length are passed as parameters. */
static __inline__ void decide_as_playfield (int startpos, int len)
{
    thisline_decision.which = 1;

    /* The latter condition might be able to happen in interlaced frames. */
    if (vpos >= minfirstline && (thisframe_first_drawn_line == -1 || vpos < thisframe_first_drawn_line))
	thisframe_first_drawn_line = vpos;
    thisframe_last_drawn_line = vpos;

    thisline_decision.plfstrt = startpos;
    thisline_decision.plflinelen = len;

    /* These are for comparison. */
    thisline_decision.bplcon0 = bplcon0;
    thisline_decision.bplcon2 = bplcon2;
#if AGA_CHIPSET == 1
    thisline_decision.bplcon4 = bplcon4;
#endif

#ifdef SMART_UPDATE
    if (line_decisions[next_lineno].plfstrt != thisline_decision.plfstrt
	|| line_decisions[next_lineno].plflinelen != thisline_decision.plflinelen
	|| line_decisions[next_lineno].bplcon0 != thisline_decision.bplcon0
	|| line_decisions[next_lineno].bplcon2 != thisline_decision.bplcon2
#if AGA_CHIPSET == 1
	|| line_decisions[next_lineno].bplcon4 != thisline_decision.bplcon4
#endif
	)
#endif /* SMART_UPDATE */
	line_changed[next_lineno] = 1;
}

/* Called when we already decided whether the line is playfield or border,
 * but are no longer sure that this was such a good idea. This can make a
 * difference only for very badly written software. */
static __inline__ void post_decide_line (void)
{
    static int warned = 0;
    int tmp;

    if (thisline_decision.which == 1 && current_hpos () < thisline_decision.plfstrt + thisline_decision.plflinelen
	&& ((bplcon0 & 0x7000) == 0 || !dmaen (DMA_BITPLANE)))
    {
	/* This is getting gross... */
	thisline_decision.plflinelen = current_hpos() - thisline_decision.plfstrt;
	thisline_decision.plflinelen &= 7;
	if (thisline_decision.plflinelen == 0)
	    thisline_decision.which = -1;
	/* ... especially THIS! */
	modulos_added = 1;
	return;
    }

    if (thisline_decision.which != -1 || diwstate == DIW_waiting_start || (bplcon0 & 0x7000) == 0
	|| !dmaen (DMA_BITPLANE) || current_hpos () >= thisline_decision.plfstrt + thisline_decision.plflinelen)
	return;
#if 0 /* Can't warn because Kickstart 1.3 does it at reset time ;-) */
    if (!warned) {
	write_log ("That program you are running is _really_ broken.\n");
	warned = 1;
    }
#endif

    decided_hires = (bplcon0 & 0x8000) == 0x8000;
    decided_nr_planes = nr_planes_from_bplcon0;

    /* Magic 12, Ray of Hope 2 demo does ugly things. Looks great from the
     * outside, rotten inside. */
    if (decided_hires) {
	tmp = current_hpos () & ~3;
	very_broken_program = current_hpos () & 3;
    } else {
	tmp = current_hpos () & ~7;
	very_broken_program = current_hpos () & 7;
    }
    MARK_LINE_CHANGED (next_lineno); /* Play safe. */
    decide_as_playfield (tmp, plflinelen + plfstrt - tmp);
}

static void decide_line_1 (void)
{
    do_sprites(vpos, current_hpos ());

    /* Surprisingly enough, this seems to be correct here - putting this into
     * decide_diw() results in garbage. */
    if (diwstate == DIW_waiting_start && vpos == plffirstline) {
	diwstate = DIW_waiting_stop;
	calculate_copper_cycle_time ();
    }
    if (diwstate == DIW_waiting_stop && vpos == plflastline) {
	diwstate = DIW_waiting_start;
	calculate_copper_cycle_time ();
    }

    if (framecnt != 0) {
/*	thisline_decision.which = -2; This doesn't do anything but hurt, I think. */
	return;
    }

    if (!dmaen(DMA_BITPLANE) || diwstate == DIW_waiting_start || nr_planes_from_bplcon0 == 0) {
	/* We don't want to draw this one. */
	thisline_decision.which = -1;
	thisline_decision.plfstrt = plfstrt;
	thisline_decision.plflinelen = plflinelen;
	return;
    }

    decided_hires = (bplcon0 & 0x8000) == 0x8000;
    decided_nr_planes = nr_planes_from_bplcon0;
#if 0
    /* The blitter gets slower if there's high bitplane activity.
     * @@@ but the values must be different. FIXME */
    if (bltstate != BLT_done
	&& ((decided_hires && decided_nr_planes > 2)
	    || (!decided_hires && decided_nr_planes > 4)))
    {
	int pl = decided_nr_planes;
	unsigned int n = (eventtab[ev_blitter].evtime-cycles);
	if (n > plflinelen)
	    n = plflinelen;
	n >>= 1;
	if (decided_hires)
	    pl <<= 1;
	if (pl == 8)
	    eventtab[ev_blitter].evtime += plflinelen;
	else if (pl == 6)
	    eventtab[ev_blitter].evtime += n*2;
	else if (pl == 5)
	    eventtab[ev_blitter].evtime += n*3/2;
	events_schedule();
    }
#endif
    decide_as_playfield (plfstrt, plflinelen);
}

/* Main entry point for deciding how to draw a line. May either do
 * nothing, decide the line as border, or decide the line as playfield. */
static __inline__ void decide_line (void)
{
    if (thisline_decision.which == 0 && current_hpos() >= plfstrt)
	decide_line_1 ();
}

/* Called when a color is about to be changed (write to a color register),
 * but the new color has not been entered into the table yet. */
static void record_color_change (int regno, unsigned long value)
{
    /* Early positions don't appear on-screen. */
    if (framecnt != 0 || vpos < minfirstline || current_hpos () < 0x18
	/*|| currprefs.emul_accuracy == 0*/)
	return;

    decide_diw ();
    decide_line ();

    /* See if we can record the color table, but have not done so yet.
     * @@@ There might be a minimal performance loss in case someone changes
     * a color exactly at the start of the DIW. I don't think it can actually
     * fail to work even in this case, but I'm not 100% sure.
     * @@@ There might be a slightly larger performance loss if we're drawing
     * this line as border... especially if there are changes in colors != 0
     * we might end up setting line_changed for no reason. FIXME */
    if (thisline_decision.diwfirstword >= 0
	&& thisline_decision.which != 0)
    {
	if (thisline_decision.ctable == -1)
	    remember_ctable ();
    }

    /* Changes outside the DIW can be ignored if the modified color is not the
     * background color, or if the accuracy is < 2. */
    if ((regno != 0 || currprefs.emul_accuracy < 2)
	&& (diwstate == DIW_waiting_start || thisline_decision.diwfirstword < 0
	    || thisline_decision.diwlastword >= 0
	    || thisline_decision.which == 0
	    || (thisline_decision.which == 1 && current_hpos() >= thisline_decision.plfstrt + thisline_decision.plflinelen)))
	return;

    /* If the border is changed the first time before the DIW, record the
     * original starting border value. */
    if (regno == 0 && thisline_decision.color0 == 0xFFFFFFFFul && thisline_decision.diwfirstword < 0) {
	thisline_decision.color0 = current_colors.color_regs[0];
	if (line_decisions[next_lineno].color0 != value)
	    line_changed[next_lineno] = 1;
    }
    /* Anything else gets recorded in the color_changes table. */
#ifdef OS_WITHOUT_MEMORY_MANAGEMENT
    if(next_color_change >= max_color_change) {
	++delta_color_change;
	return;
    }
#endif
    curr_color_changes[next_color_change].linepos = current_hpos ();
    curr_color_changes[next_color_change].regno = regno;
    curr_color_changes[next_color_change++].value = value;
}

/* Stupid hack to get some Sanity demos working. */
static void decide_delay (void)
{
    static int warned;

    /* Don't do anything if we're outside the DIW. */
    if (thisline_decision.diwfirstword == -1 || thisline_decision.diwlastword > 0)
	return;
    decide_line ();
    /* Half-hearted attempt to get things right even when post_decide_line() changes
     * the decision afterwards. */
    if (thisline_decision.which != 1) {
	thisline_decision.bplcon1 = bplcon1;
	return;
    }
    /* @@@ Could check here for DDF stopping earlier than DIW */

#ifdef OS_WITHOUT_MEMORY_MANAGEMENT
    if(next_delay_change >= max_delay_change) {
	++delta_delay_change;
	return;
    }
#endif
    delay_changes[next_delay_change].linepos = current_hpos ();
    delay_changes[next_delay_change++].value = bplcon1;
    if (!warned) {
	warned = 1;
	write_log ("Program is torturing BPLCON1.\n");
    }
}

/*
 * The decision which bitplane pointers to use is not taken at plfstrt, since
 * data fetch does not start for all planes at this point. Therefore, we wait
 * for the end of the ddf area or the first write to any of the bitplane
 * pointer registers, whichever comes last, before we decide which plane pointers
 * to use.
 * Call decide_line() before this function.
 */
static void decide_plane (void)
{
    int i, bytecount;

    if (framecnt != 0 || plane_decided)
	return;

    if (decided_nr_planes == -1 /* Still undecided */
	|| current_hpos () < thisline_decision.plfstrt + thisline_decision.plflinelen)
	return;

    plane_decided = 1;

    bytecount = plflinelen / (decided_hires ? 4 : 8) * 2;

    if (bytecount > MAX_WORDS_PER_LINE * 2) {
	/* Can't happen. */
	static int warned = 0;
	if (!warned)
	    write_log ("Mysterious bug in decide_plane(). Please report.\n");
	bytecount = 0;
    }
    if (!very_broken_program) {
	uae_u8 *dataptr = line_data[next_lineno];
	for (i = 0; i < decided_nr_planes; i++, dataptr += MAX_WORDS_PER_LINE*2) {
	    uaecptr pt = bplpt[i];
	    uae_u8 *real_ptr = pfield_xlateptr(pt, bytecount);
	    if (real_ptr == NULL)
		real_ptr = pfield_xlateptr(0, 0);
#ifdef SMART_UPDATE
#if 1
	    if (line_changed[next_lineno])
		memcpy (dataptr, real_ptr, bytecount);
	    else
#endif
		line_changed[next_lineno] |= memcmpy (dataptr, real_ptr, bytecount);
#else
	    real_bplpt[i] = real_ptr;
#endif
	}
    } else {
	uae_u8 *dataptr = line_data[next_lineno];
	for (i = 0; i < decided_nr_planes; i++, dataptr += MAX_WORDS_PER_LINE*2) {
	    uaecptr pt = bplpt[i];
	    uae_u8 *real_ptr;

	    if (decided_hires) {
		switch (i) {
		 case 3: pt -= 2;
		 case 2: pt -= (very_broken_program >= 3 ? 2 : 0); break;
		 case 1: pt -= (very_broken_program >= 2 ? 2 : 0); break;
		 case 0: break;
		}
	    } else {
		switch (i) {
		 case 5: pt -= (very_broken_program >= 3 ? 2 : 0); break;
		 case 4: pt -= (very_broken_program >= 7 ? 2 : 0); break;
		 case 3: pt -= (very_broken_program >= 2 ? 2 : 0); break;
		 case 2: pt -= (very_broken_program >= 6 ? 2 : 0); break;
		 case 1: pt -= (very_broken_program >= 4 ? 2 : 0); break;
		 case 0: break;
		}
	    }
	    real_ptr = pfield_xlateptr(pt, bytecount);
	    if (real_ptr == NULL)
		real_ptr = pfield_xlateptr(0, 0);
#ifdef SMART_UPDATE
	    if (!line_changed[next_lineno])
		line_changed[next_lineno] |= memcmpy (dataptr, real_ptr, bytecount);
	    else
		memcpy (dataptr, real_ptr, bytecount);
#else
	    real_bplpt[i] = real_ptr;
#endif
	}
    }
}

/*
 * Called from the BPLxMOD routines, after a new value has been written.
 * This routine decides whether the new value is already relevant for the
 * current line.
 */
static void decide_modulos (void)
{
    /* All this effort just for the Sanity WOC demo... */
    decide_line ();
    if (decided_nr_planes != -1 && current_hpos() >= thisline_decision.plfstrt + thisline_decision.plflinelen)
	return;
    decided_bpl1mod = bpl1mod;
    decided_bpl2mod = bpl2mod;
}

/*
 * Add the modulos to the bitplane pointers if data fetch is already
 * finished for the current line.
 * Call decide_plane() before calling this, so that we won't use the
 * new values of the plane pointers for the current line.
 */
static void do_modulos (void)
{
    /* decided_nr_planes is != -1 if this line should be drawn by the
     * display hardware, regardless of whether it fits on the emulated screen.
     */
    if (decided_nr_planes != -1 && plane_decided && !modulos_added
	&& current_hpos() >= thisline_decision.plfstrt + thisline_decision.plflinelen)
    {
	int bytecount = thisline_decision.plflinelen / (decided_hires ? 4 : 8) * 2;
	int add1 = bytecount + decided_bpl1mod;
	int add2 = bytecount + decided_bpl2mod;

	if (!very_broken_program) {
	    switch (decided_nr_planes) {
	     case 8: bplpt[7] += add2;
	     case 7: bplpt[6] += add1;
	     case 6: bplpt[5] += add2;
	     case 5: bplpt[4] += add1;
	     case 4: bplpt[3] += add2;
	     case 3: bplpt[2] += add1;
	     case 2: bplpt[1] += add2;
	     case 1: bplpt[0] += add1;
	    }
	} else if (decided_hires) { /* @@@ used to be bplhires which is clearly wrong */
	    switch (decided_nr_planes) {
	     case 8: bplpt[7] += add2;
	     case 7: bplpt[6] += add1;
	     case 6: bplpt[5] += add2;
	     case 5: bplpt[4] += add1;
	     case 4: bplpt[3] += add2 - 2;
	     case 3: bplpt[2] += add1 - (very_broken_program >= 3 ? 2 : 0);
	     case 2: bplpt[1] += add2 - (very_broken_program >= 2 ? 2 : 0);
	     case 1: bplpt[0] += add1;
	    }
	} else {
	    switch (decided_nr_planes) {
	     case 8: bplpt[7] += add2;
	     case 7: bplpt[6] += add1;
	     case 6: bplpt[5] += add2 - (very_broken_program >= 3 ? 2 : 0);
	     case 5: bplpt[4] += add1 - (very_broken_program >= 7 ? 2 : 0);
	     case 4: bplpt[3] += add2 - (very_broken_program >= 2 ? 2 : 0);
	     case 3: bplpt[2] += add1 - (very_broken_program >= 6 ? 2 : 0);
	     case 2: bplpt[1] += add2 - (very_broken_program >= 4 ? 2 : 0);
	     case 1: bplpt[0] += add1;
	    }
	}

	modulos_added = 1;
    }
}

static __inline__ void record_sprite (int spr, int sprxp)
{
    int pos = next_sprite_draw;
    unsigned int data, datb;

#ifdef OS_WITHOUT_MEMORY_MANAGEMENT
    if(pos >= max_sprite_draw) {
	++delta_sprite_draw;
	return;
    }
#endif
    data = sprdata[spr];
    datb = sprdatb[spr];

    /* XXX FIXME, this isn't very clever, but it might do */
    for (;;) {
	if (pos == curr_drawinfo[next_lineno].first_sprite_draw)
	    break;
	if (curr_sprite_positions[pos-1].linepos < sprxp)
	    break;
	if (curr_sprite_positions[pos-1].linepos == sprxp
	    && curr_sprite_positions[pos-1].num > spr)
	    break;
	printf("Foo\n");
	pos--;
    }
    if (pos != next_sprite_draw) {
	int pos2 = next_sprite_draw;
	while (pos2 != pos) {
	    curr_sprite_positions[pos2] = curr_sprite_positions[pos2-1];
	    pos2--;
	}
    }
    curr_sprite_positions[pos].linepos = sprxp;
    curr_sprite_positions[pos].num = spr;
    curr_sprite_positions[pos].ctl = sprctl[spr];
    curr_sprite_positions[pos].datab = ((sprtaba[data & 0xFF] << 16) | sprtaba[data >> 8]
					| (sprtabb[datb & 0xFF] << 16) | sprtabb[datb >> 8]);
    next_sprite_draw++;
}

static void decide_sprites (void)
{
    int nrs[8], posns[8], count, i;
    int point = PIXEL_XPOS (current_hpos ());

    if (framecnt != 0 || current_hpos() < 0x14 || nr_armed == 0 || point == last_sprite_point)
	return;

    decide_diw ();
    decide_line ();

    if (thisline_decision.which != 1)
	return;

    count = 0;
    for (i = 0; i < 8; i++) {
	int sprxp = sprxpos[i];
	int j, bestp;

	if (!sprarmed[i] || sprxp < 0 || sprxp > point || last_sprite_point >= sprxp)
	    continue;
	if ((thisline_decision.diwfirstword >= 0 && sprxp + sprite_width < thisline_decision.diwfirstword)
	    || (thisline_decision.diwlastword >= 0 && sprxp > thisline_decision.diwlastword))
	    continue;

	for (bestp = 0; bestp < count; bestp++) {
	    if (posns[bestp] > sprxp)
		break;
	    if (posns[bestp] == sprxp && nrs[bestp] < i)
		break;
	}
	for (j = count; j > bestp; j--) {
	    posns[j] = posns[j-1];
	    nrs[j] = nrs[j-1];
	}
	posns[j] = sprxp;
	nrs[j] = i;
	count++;
    }
    for (i = 0; i < count; i++)
	record_sprite(nrs[i], posns[i]);
    last_sprite_point = point;
}

/* End of a horizontal scan line. Finish off all decisions that were not
 * made yet. */
static void finish_decisions (void)
{
    struct draw_info *dip;
    struct draw_info *dip_old;
    struct decision *dp;
    int changed;

    if (framecnt != 0)
	return;

    decide_diw ();
    if (thisline_decision.which == 0)
	decide_line_1 ();

    /* Large DIWSTOP values can cause the stop position never to be
     * reached, so the state machine always stays in the same state and
     * there's a more-or-less full-screen DIW. */
    if (hdiwstate == DIW_waiting_stop) {
	thisline_decision.diwlastword = max_diwlastword;
	if (thisline_decision.diwlastword != line_decisions[next_lineno].diwlastword)
	    MARK_LINE_CHANGED (next_lineno);
    }

    if (line_decisions[next_lineno].which != thisline_decision.which)
	line_changed[next_lineno] = 1;
    decide_plane ();

    dip = curr_drawinfo + next_lineno;
    dip_old = prev_drawinfo + next_lineno;
    dp = line_decisions + next_lineno;
    changed = line_changed[next_lineno];

    if (thisline_decision.which == 1) {
	if (diwlastword > max_diwstop)
	    max_diwstop = diwlastword;
	if (diwfirstword < min_diwstart)
	    min_diwstart = diwfirstword;

	decide_sprites();

	if (thisline_decision.bplcon1 != line_decisions[next_lineno].bplcon1)
	    changed = 1;
    }

    dip->last_color_change = next_color_change;
    dip->last_delay_change = next_delay_change;
    dip->last_sprite_draw = next_sprite_draw;

    if (thisline_decision.ctable == -1) {
	if (thisline_decision.which == 1)
	    remember_ctable ();
	else
	    remember_ctable_for_border ();
    }
    if (thisline_decision.which == -1 && thisline_decision.color0 == 0xFFFFFFFFul)
	thisline_decision.color0 = current_colors.color_regs[0];

    dip->nr_color_changes = next_color_change - dip->first_color_change;
    dip->nr_sprites = next_sprite_draw - dip->first_sprite_draw;

    if (dip->first_delay_change != dip->last_delay_change)
	changed = 1;
    if (!changed
	&& (dip->nr_color_changes != dip_old->nr_color_changes
	    || (dip->nr_color_changes > 0
		&& memcmp (curr_color_changes + dip->first_color_change,
			   prev_color_changes + dip_old->first_color_change,
			   dip->nr_color_changes * sizeof *curr_color_changes) != 0)))
	changed = 1;
    if (!changed && thisline_decision.which == 1
	&& (dip->nr_sprites != dip_old->nr_sprites
	    || (dip->nr_sprites > 0
		&& memcmp (curr_sprite_positions + dip->first_sprite_draw,
			   prev_sprite_positions + dip_old->first_sprite_draw,
			   dip->nr_sprites * sizeof *curr_sprite_positions) != 0)))
	changed = 1;

    if (changed) {
	line_changed[next_lineno] = 1;
	*dp = thisline_decision;
    } else
	/* The only one that may differ: */
	dp->ctable = thisline_decision.ctable;
}

/* Set the state of all decisions to "undecided" for a new scanline. */
static void reset_decisions (void)
{
    if (framecnt != 0)
	return;
    thisline_decision.which = 0;
    decided_bpl1mod = bpl1mod;
    decided_bpl2mod = bpl2mod;
    decided_nr_planes = -1;

    /* decided_hires shouldn't be touched before it's initialized by decide_line(). */
    thisline_decision.diwfirstword = -1;
    thisline_decision.diwlastword = -2;
    if (hdiwstate == DIW_waiting_stop) {
	thisline_decision.diwfirstword = PIXEL_XPOS (DISPLAY_LEFT_SHIFT/2);
	if (thisline_decision.diwfirstword != line_decisions[next_lineno].diwfirstword)
	    MARK_LINE_CHANGED (next_lineno);
    }
    thisline_decision.ctable = -1;
    thisline_decision.color0 = 0xFFFFFFFFul;

    line_changed[next_lineno] = 0;
    curr_drawinfo[next_lineno].first_color_change = next_color_change;
    curr_drawinfo[next_lineno].first_delay_change = next_delay_change;
    curr_drawinfo[next_lineno].first_sprite_draw = next_sprite_draw;

    /* memset(sprite_last_drawn_at, 0, sizeof sprite_last_drawn_at); */
    last_sprite_point = 0;
    modulos_added = 0;
    plane_decided = 0;
    color_decided = 0;
    very_broken_program = 0;
}

/* Initialize the decision array, once before the emulator really starts. */
static void init_decisions (void)
{
    size_t i;
    for (i = 0; i < sizeof line_decisions / sizeof *line_decisions; i++) {
	line_decisions[i].which = -2;
    }
}

/* Calculate display window and data fetch values from the corresponding
 * hardware registers. */
static void calcdiw (void)
{
    diwfirstword = ((diwstrt & 0xFF) - DISPLAY_LEFT_SHIFT - 1) << lores_shift;
    diwlastword  = ((diwstop & 0xFF) + 0x100 - DISPLAY_LEFT_SHIFT - 1) << lores_shift;

    if (diwlastword > max_diwlastword)
	diwlastword = max_diwlastword;
    if (diwfirstword < 0)
	diwfirstword = 0;
    if (diwlastword < 0)
	diwlastword = 0;

    plffirstline = diwstrt >> 8;
    plflastline = diwstop >> 8;
#if 0
    /* This happens far too often. */
    if (plffirstline < minfirstline) {
	fprintf(stderr, "Warning: Playfield begins before line %d!\n", minfirstline);
	plffirstline = minfirstline;
    }
#endif
    if ((plflastline & 0x80) == 0) plflastline |= 0x100;
#if 0 /* Turrican does this */
    if (plflastline > 313) {
	fprintf(stderr, "Warning: Playfield out of range!\n");
	plflastline = 313;
    }
#endif
    plfstrt = ddfstrt;
    plfstop = ddfstop;
    if (plfstrt < 0x18) plfstrt = 0x18;
    if (plfstop < 0x18) plfstop = 0x18;
    if (plfstop > 0xD8) plfstop = 0xD8;
    if (plfstrt > plfstop) plfstrt = plfstop;

    /* ! If the masking operation is changed, the pfield_doline code could break
     * on some systems (alignment) */
    /* This actually seems to be correct now, at least the non-AGA stuff... */
    plfstrt &= ~3;
    plfstop &= ~3;
    /* @@@ Start looking for AGA bugs here... (or maybe even in the above masking ops) */
    if ((fmode & 3) == 0)
	plflinelen = (plfstop-plfstrt+15) & ~7;
    else if ((fmode & 3) == 3)
	plflinelen = (plfstop-plfstrt+63) & ~31;
    else
	plflinelen = (plfstop-plfstrt+31) & ~15;
}

/*
 * Screen update macros/functions
 */

static unsigned int ham_lastcolor;

static void init_ham_decoding(int first)
{
    int pix = dp_for_drawing->diwfirstword;
    ham_lastcolor = colors_for_drawing.color_regs[0];
    while (pix < first) {
	int pv = pixdata.apixels[pix];
	switch(pv & 0x30) {
	 case 0x00: ham_lastcolor = colors_for_drawing.color_regs[pv]; break;
	 case 0x10: ham_lastcolor &= 0xFF0; ham_lastcolor |= (pv & 0xF); break;
	 case 0x20: ham_lastcolor &= 0x0FF; ham_lastcolor |= (pv & 0xF) << 8; break;
	 case 0x30: ham_lastcolor &= 0xF0F; ham_lastcolor |= (pv & 0xF) << 4; break;
	}
	pix++;
    }
}

static void decode_ham6 (int pix, int stoppos)
{
    uae_u32 *buf = ham_linebuf;

    if (!bplham || bplplanecnt != 6)
	return;

    if (stoppos > dp_for_drawing->diwlastword)
	stoppos = dp_for_drawing->diwlastword;
    if (pix < dp_for_drawing->diwfirstword) {
	ham_lastcolor = colors_for_drawing.color_regs[0];
	pix = dp_for_drawing->diwfirstword;
    }
#ifdef LORES_HACK
    if (currprefs.gfx_lores == 2)
	pix <<= 1, stoppos <<= 1;
#endif
    while (pix < stoppos) {
	int pv = pixdata.apixels[pix];
	switch(pv & 0x30) {
	 case 0x00: ham_lastcolor = colors_for_drawing.color_regs[pv]; break;
	 case 0x10: ham_lastcolor &= 0xFF0; ham_lastcolor |= (pv & 0xF); break;
	 case 0x20: ham_lastcolor &= 0x0FF; ham_lastcolor |= (pv & 0xF) << 8; break;
	 case 0x30: ham_lastcolor &= 0xF0F; ham_lastcolor |= (pv & 0xF) << 4; break;
	}

	buf[pix++] = ham_lastcolor;
    }
}
#if 0
static void decode_ham_aga (int pix, int stoppos)
{
    static uae_u32 lastcolor;
    uae_u32 *buf = ham_linebuf;

    if (!bplham || (bplplanecnt != 6 && bplplanecnt != 8))
	return;

    if (pix <= dp_for_drawing->diwfirstword) {
	pix = dp_for_drawing->diwfirstword;
	lastcolor = colors_for_drawing.color_regs[0];
    }

    if (dp_for_drawing->bplplanecnt == 6) {
	/* HAM 6 */
	while (pix < dp_for_drawing->diwlastword && pix < stoppos) {
	    int pv = pixdata.apixels[pix];
	    switch(pv & 0x30) {
	     case 0x00: lastcolor = colors_for_drawing.color_regs[pv]; break;
	     case 0x10: lastcolor &= 0xFFFF00; lastcolor |= (pv & 0xF)*0x11; break;
	     case 0x20: lastcolor &= 0x00FFFF; lastcolor |= (pv & 0xF)*0x11 << 16; break;
	     case 0x30: lastcolor &= 0xFF00FF; lastcolor |= (pv & 0xF)*0x11 << 8; break;
	    }
	    buf[pix++] = lastcolor;
	}
    } else if (dp_for_drawing->bplplanecnt == 8) {
	/* HAM 8 */
	while (pix < dp_for_drawing->diwlastword && pix < stoppos) {
	    int pv = pixdata.apixels[pix];
	    switch(pv & 0x3) {
	     case 0x0: lastcolor = colors_for_drawing.color_regs[pv >> 2]; break;
	     case 0x1: lastcolor &= 0xFFFF03; lastcolor |= (pv & 0xFC); break;
	     case 0x2: lastcolor &= 0x03FFFF; lastcolor |= (pv & 0xFC) << 16; break;
	     case 0x3: lastcolor &= 0xFF03FF; lastcolor |= (pv & 0xFC) << 8; break;
	    }
	    buf[pix++] = lastcolor;
	}
    }
}
#endif

#if AGA_CHIPSET != 0
/* WARNING: Not too much of this will work correctly yet. */

static void pfield_linetoscr_aga(int pix, int stoppos)
{
    uae_u32 *buf = aga_lbufptr;
    int i;
    int xor = (uae_u8)(bplcon4 >> 8);
    int oldpix = pix; \

    buf -= pix; \

    for (i = 0; i < stoppos; i++)
	pixdata.apixels[i] ^= xor;

    while (pix < diwfirstword && pix < stoppos) {
	buf[pix++] = colors_for_drawing.color_regs[0];
    }
    if (bplham) {
	while (pix < diwlastword && pix < stoppos) {
	    uae_u32 d = ham_linebuf[pix];
	    buf[pix] = d;
	    pix++;
	}
    } else if (bpldualpf) {
	/* Dual playfield */
	int *lookup = bpldualpfpri ? dblpf_aga2 : dblpf_aga1;
	int *lookup_no = bpldualpfpri ? dblpf_2nd2 : dblpf_2nd1;
	while (pix < diwlastword && pix < stoppos) {
	    int pixcol = pixdata.apixels[pix];
	    int pfno = lookup_no[pixcol];

	    if (spixstate[pix]) {
		buf[pix] = colors_for_drawing.color_regs[pixcol];
	    } else {
		int val = lookup[pixdata.apixels[pix]];
		if (pfno == 2)
		    val += dblpfofs[(bplcon2 >> 10) & 7];
		buf[pix] = colors_for_drawing.color_regs[val];
	    }
	    pix++;
	}
    } else if (bplehb) {
	while (pix < diwlastword && pix < stoppos) {
	    int pixcol = pixdata.apixels[pix];
	    uae_u32 d = colors_for_drawing.color_regs[pixcol];
	    /* What about sprites? */
	    if (pixcol & 0x20)
		d = (d & 0x777777) >> 1;
	    buf[pix] = d;
	    pix++;
	}
    } else {
	while (pix < diwlastword && pix < stoppos) {
	    int pixcol = pixdata.apixels[pix];
	    buf[pix] = colors_for_drawing.color_regs[pixcol];
	    pix++;
	}
    }
    while (pix < stoppos) {
	buf[pix++] = colors_for_drawing.color_regs[0];
    }
    aga_lbufptr += pix - oldpix;
}

static void aga_translate32(int start, int stop)
{
    memcpy (((uae_u32 *)xlinebuffer) + start, aga_linebuf + start, 4*(stop-start));
}

static void aga_translate16(int start, int stop)
{
    int i;
    for (i = start; i < stop; i++) {
	uae_u32 d = aga_linebuf[i];
	uae_u16 v = ((d & 0xF0) >> 4) | ((d & 0xF000) >> 8) | ((d & 0xF00000) >> 12);
	((uae_u16 *)xlinebuffer)[i] = xcolors[v];
    }
}

static void aga_translate8(int start, int stop)
{
    int i;
    for (i = start; i < stop; i++) {
	uae_u32 d = aga_linebuf[i];
	uae_u16 v = ((d & 0xF0) >> 4) | ((d & 0xF000) >> 8) | ((d & 0xF00000) >> 12);
	((uae_u8 *)xlinebuffer)[i] = xcolors[v];
    }
}
#endif

static int linetoscr_double_offset;

#define LINE_TO_SCR(NAME, TYPE, DO_DOUBLE, CONVERT) \
static void NAME(int pix, int lframe_end, int diw_end, int stoppos) \
{ \
    TYPE *buf = ((TYPE *)xlinebuffer); \
    int oldpix = pix; \
    /* These are different for register-allocation purposes. */ \
    TYPE d1, d2; \
    int offset; \
\
    if (DO_DOUBLE) offset = linetoscr_double_offset / sizeof(TYPE); \
\
    d1 = CONVERT(colors_for_drawing.acolors[0]); \
    while (pix < lframe_end) { \
	buf[pix] = d1; if (DO_DOUBLE) buf[pix+offset] = d1; \
	pix++; \
    } \
    if (bplham && bplplanecnt == 6) { \
	/* HAM 6 */ \
	while (pix < diw_end) { \
	    TYPE d = CONVERT(xcolors[ham_linebuf[pix]]); \
	    buf[pix] = d; if (DO_DOUBLE) buf[pix+offset] = d; \
	    pix++; \
	} \
    } else if (bpldualpf) { \
	/* Dual playfield */ \
	int *lookup = bpldualpfpri ? dblpf_ind2 : dblpf_ind1; \
	while (pix < diw_end) { \
	    int pixcol = pixdata.apixels[pix]; \
	    TYPE d; \
	    d = CONVERT(colors_for_drawing.acolors[lookup[pixcol]]); \
	    buf[pix] = d; if (DO_DOUBLE) buf[pix+offset] = d; \
	    pix++; \
	} \
    } else if (bplehb) { \
	while (pix < diw_end) { \
	    int p = pixdata.apixels[pix]; \
	    TYPE d = CONVERT (colors_for_drawing.acolors[p]); \
	    if (p >= 32) d = CONVERT(xcolors[(colors_for_drawing.color_regs[p-32] >> 1) & 0x777]); \
	    buf[pix] = d; if (DO_DOUBLE) buf[pix+offset] = d; \
	    pix++; \
	} \
    } else { \
	while (pix < diw_end) { \
	    TYPE d = CONVERT (colors_for_drawing.acolors[pixdata.apixels[pix]]); \
	    buf[pix] = d; if (DO_DOUBLE) buf[pix+offset] = d; \
	    pix++; \
	} \
    } \
    d2 = CONVERT (colors_for_drawing.acolors[0]); \
    while (pix < stoppos) { \
	buf[pix] = d2; if (DO_DOUBLE) buf[pix+offset] = d2; \
	pix++; \
    } \
}

#define FILL_LINE(NAME, TYPE, CONVERT) \
static void NAME(char *buf, int start, int stop) \
{ \
    TYPE *b = (TYPE *)buf; \
    int i;\
    xcolnr col = colors_for_drawing.acolors[0]; \
    for (i = start; i < stop; i++) \
	b[i] = CONVERT (col); \
}

LINE_TO_SCR(pfield_linetoscr_8, uae_u8, 0,)
LINE_TO_SCR(pfield_linetoscr_16, uae_u16, 0,)
LINE_TO_SCR(pfield_linetoscr_32, uae_u32, 0,)
LINE_TO_SCR(pfield_linetoscr_8_double_slow, uae_u8, 1,)
LINE_TO_SCR(pfield_linetoscr_16_double_slow, uae_u16, 1,)
LINE_TO_SCR(pfield_linetoscr_32_double_slow, uae_u32, 1,)

FILL_LINE(fill_line_8, uae_u8,)
FILL_LINE(fill_line_16, uae_u16,)
FILL_LINE(fill_line_32, uae_u32,)

#ifdef HAVE_UAE_U24
LINE_TO_SCR(pfield_linetoscr_24, uae_u24, 0, uae24_convert)
LINE_TO_SCR(pfield_linetoscr_24_double_slow, uae_u24, 1, uae24_convert)
FILL_LINE(fill_line_24, uae_u24, uae24_convert)
#else
/* Dummy versions */
static void pfield_linetoscr_24 (int pix, int lframe_end, int diw_end, int stoppos)
{
}
static void pfield_linetoscr_24_double_slow (int pix, int lframe_end, int diw_end, int stoppos)
{
}
static void fill_line_24 (char *buf, int start, int stop)
{
}
#endif

static __inline__ void fill_line_full (void)
{
    switch (gfxvidinfo.pixbytes) {
     case 1: fill_line_8(xlinebuffer, linetoscr_x_adjust, linetoscr_x_adjust + gfxvidinfo.width); break;
     case 2: fill_line_16(xlinebuffer, linetoscr_x_adjust, linetoscr_x_adjust + gfxvidinfo.width); break;
     case 3: fill_line_24(xlinebuffer, linetoscr_x_adjust, linetoscr_x_adjust + gfxvidinfo.width); break;
     case 4: fill_line_32(xlinebuffer, linetoscr_x_adjust, linetoscr_x_adjust + gfxvidinfo.width); break;
    }
}

#define fill_line fill_line_full

#define pfield_linetoscr_full8 pfield_linetoscr_8
#define pfield_linetoscr_full16 pfield_linetoscr_16
#define pfield_linetoscr_full24 pfield_linetoscr_24
#define pfield_linetoscr_full32 pfield_linetoscr_32

#define pfield_linetoscr_full8_double pfield_linetoscr_8_double_slow
#define pfield_linetoscr_full16_double pfield_linetoscr_16_double_slow
#define pfield_linetoscr_full24_double pfield_linetoscr_24_double_slow
#define pfield_linetoscr_full32_double pfield_linetoscr_32_double_slow

#if 1 && defined(X86_ASSEMBLY)
/* This version of fill_line is probably a win on all machines, but for
 * now I'm cautious */
#undef fill_line
static __inline__ void fill_line (void)
{
    int shift;
    int nints, nrem;
    int *start;
    xcolnr val;

#ifdef HAVE_UAE_U24
    if (gfxvidinfo.pixbytes == 3) {
	fill_line_24 (xlinebuffer, linetoscr_x_adjust, linetoscr_x_adjust + gfxvidinfo.width);
	return;
    }
#endif
    
    shift = 0;
    if (gfxvidinfo.pixbytes == 2)
	shift = 1;
    if (gfxvidinfo.pixbytes == 4)
	shift = 2;

    nints = gfxvidinfo.width >> (2-shift);
    nrem = nints & 7;
    nints &= ~7;
    start = (int *)(((char *)xlinebuffer) + (linetoscr_x_adjust << shift));
    val = colors_for_drawing.acolors[0];
    for (; nints > 0; nints -= 8, start+=8) {
	*start = val;
	*(start+1) = val;
	*(start+2) = val;
	*(start+3) = val;
	*(start+4) = val;
	*(start+5) = val;
	*(start+6) = val;
	*(start+7) = val;
    }

    switch (nrem) {
     case 7:
	*start++ = val;
     case 6:
	*start++ = val;
     case 5:
	*start++ = val;
     case 4:
	*start++ = val;
     case 3:
	*start++ = val;
     case 2:
	*start++ = val;
     case 1:
	*start = val;
    }
}

#undef pfield_linetoscr_full8
/* The types are lies, of course. */
extern void pfield_linetoscr_normal_asm8(void) __asm__("pfield_linetoscr_normal_asm8");
extern void pfield_linetoscr_ehb_asm8(void) __asm__("pfield_linetoscr_ehb_asm8");
extern void pfield_linetoscr_ham6_asm8(void) __asm__("pfield_linetoscr_ham6_asm8");
extern void pfield_linetoscr_dualpf_asm8(void) __asm__("pfield_linetoscr_dualpf_asm8");
extern void pfield_linetoscr_hdouble_asm8(void) __asm__("pfield_linetoscr_hdouble_asm8");
extern void pfield_linetoscr_hdouble_dpf_asm8(void) __asm__("pfield_linetoscr_hdouble_dpf_asm8");
extern void pfield_linetoscr_hdouble_ehb_asm8(void) __asm__("pfield_linetoscr_hdouble_ehb_asm8");
extern void pfield_linetoscr_asm8(void (*)(void), int, int, int, int, ...) __asm__("pfield_linetoscr_asm8");

static void pfield_linetoscr_full8(int pix, int lframe_end, int diw_end, int stoppos)
{
    int lframe_end_1, diw_end_1;

    lframe_end_1 = pix + ((lframe_end - pix) & ~3);
    diw_end_1 = stoppos - ((stoppos - diw_end) & ~3);

    if (bplham && bplplanecnt == 6) {
	pfield_linetoscr_asm8(pfield_linetoscr_ham6_asm8, pix, lframe_end_1, diw_end_1, stoppos);
    } else if (bpldualpf) {
#ifdef LORES_HACK
	if (currprefs.gfx_lores == 2)
	    pfield_linetoscr_asm8(pfield_linetoscr_hdouble_dpf_asm8, pix, lframe_end_1, diw_end_1, stoppos,
				   bpldualpfpri ? dblpf_ind2 : dblpf_ind1);
	else
#endif
	    pfield_linetoscr_asm8(pfield_linetoscr_dualpf_asm8, pix, lframe_end_1, diw_end_1, stoppos,
				  bpldualpfpri ? dblpf_ind2 : dblpf_ind1);
    } else if (bplehb) {
#ifdef LORES_HACK
	if (currprefs.gfx_lores == 2)
	    pfield_linetoscr_asm8(pfield_linetoscr_hdouble_ehb_asm8, pix, lframe_end_1, diw_end_1, stoppos);
	else
#endif
	    pfield_linetoscr_asm8(pfield_linetoscr_ehb_asm8, pix, lframe_end_1, diw_end_1, stoppos);
    } else {
#ifdef LORES_HACK
	if (currprefs.gfx_lores == 2)
	    pfield_linetoscr_asm8(pfield_linetoscr_hdouble_asm8, pix, lframe_end_1, diw_end_1, stoppos);
	else
#endif
	    pfield_linetoscr_asm8(pfield_linetoscr_normal_asm8, pix, lframe_end_1, diw_end_1, stoppos);
    }

    /* The assembly functions work on aligned data, so we may have to do some
     * additional work at the edges. */
    if (lframe_end != lframe_end_1) {
	int i, c = colors_for_drawing.acolors[0];
	for (i = lframe_end_1; i < lframe_end; i++)
	    xlinebuffer[i] = c;
    }
    if (diw_end != diw_end_1) {
	int i, c = colors_for_drawing.acolors[0];
	for (i = diw_end; i < diw_end_1; i++)
	    xlinebuffer[i] = c;
    }
}

#undef pfield_linetoscr_full16
extern void pfield_linetoscr_normal_asm16(void) __asm__("pfield_linetoscr_normal_asm16");
extern void pfield_linetoscr_ehb_asm16(void) __asm__("pfield_linetoscr_ehb_asm16");
extern void pfield_linetoscr_ham6_asm16(void) __asm__("pfield_linetoscr_ham6_asm16");
extern void pfield_linetoscr_dualpf_asm16(void) __asm__("pfield_linetoscr_dualpf_asm16");
extern void pfield_linetoscr_hdouble_asm16(void) __asm__("pfield_linetoscr_hdouble_asm16");
extern void pfield_linetoscr_hdouble_dpf_asm16(void) __asm__("pfield_linetoscr_hdouble_dpf_asm16");
extern void pfield_linetoscr_hdouble_ehb_asm16(void) __asm__("pfield_linetoscr_hdouble_ehb_asm16");
extern void pfield_linetoscr_asm16(void (*)(void), int, int, int, int, ...) __asm__("pfield_linetoscr_asm16");

static void pfield_linetoscr_full16(int pix, int lframe_end, int diw_end, int stoppos)
{
    int lframe_end_1, diw_end_1;

    lframe_end_1 = pix + ((lframe_end - pix) & ~3);
    diw_end_1 = stoppos - ((stoppos - diw_end) & ~3);

    if (bplham && bplplanecnt == 6) {
	pfield_linetoscr_asm16(pfield_linetoscr_ham6_asm16, pix, lframe_end_1, diw_end_1, stoppos);
    } else if (bpldualpf) {
#ifdef LORES_HACK
	if (currprefs.gfx_lores == 2)
	    pfield_linetoscr_asm16(pfield_linetoscr_hdouble_dpf_asm16, pix, lframe_end_1, diw_end_1, stoppos,
				   bpldualpfpri ? dblpf_ind2 : dblpf_ind1);
	else
#endif
	    pfield_linetoscr_asm16(pfield_linetoscr_dualpf_asm16, pix, lframe_end_1, diw_end_1, stoppos,
				   bpldualpfpri ? dblpf_ind2 : dblpf_ind1);
    } else if (bplehb) {
#ifdef LORES_HACK
	if (currprefs.gfx_lores == 2)
	    pfield_linetoscr_asm16(pfield_linetoscr_hdouble_ehb_asm16, pix, lframe_end_1, diw_end_1, stoppos);
	else
#endif
	    pfield_linetoscr_asm16(pfield_linetoscr_ehb_asm16, pix, lframe_end_1, diw_end_1, stoppos);
    } else {
#ifdef LORES_HACK
	if (currprefs.gfx_lores == 2)
	    pfield_linetoscr_asm16(pfield_linetoscr_hdouble_asm16, pix, lframe_end_1, diw_end_1, stoppos);
	else
#endif
	    pfield_linetoscr_asm16(pfield_linetoscr_normal_asm16, pix, lframe_end_1, diw_end_1, stoppos);
    }

    /* The assembly functions work on aligned data, so we may have to do some
     * additional work at the edges. */
    if (lframe_end != lframe_end_1) {
	int i, c = colors_for_drawing.acolors[0];
	for (i = lframe_end_1; i < lframe_end; i++)
	    ((uae_u16 *)xlinebuffer)[i] = c;
    }
    if (diw_end != diw_end_1) {
	int i, c = colors_for_drawing.acolors[0];
	for (i = diw_end; i < diw_end_1; i++)
	    ((uae_u16 *)xlinebuffer)[i] = c;
    }
}

#ifndef NO_DOUBLING_LINETOSCR
#define NO_DOUBLING_LINETOSCR
#endif

#endif

#ifdef NO_DOUBLING_LINETOSCR
#undef pfield_linetoscr_full8_double
#undef pfield_linetoscr_full16_double
static void pfield_linetoscr_full8_double(int start, int lframe_end, int diw_end, int stop)
{
    char *oldxlb = (char *)xlinebuffer;
    pfield_linetoscr_full8(start, lframe_end, diw_end, stop);
    xlinebuffer = oldxlb + linetoscr_double_offset;
    pfield_linetoscr_full8(start, lframe_end, diw_end, stop);
}
static void pfield_linetoscr_full16_double(int start, int lframe_end, int diw_end, int stop)
{
    char *oldxlb = (char *)xlinebuffer;
    pfield_linetoscr_full16(start, lframe_end, diw_end, stop);
    xlinebuffer = oldxlb + linetoscr_double_offset;
    pfield_linetoscr_full16(start, lframe_end, diw_end, stop);
}
#endif

static int linetoscr_diw_end, linetoscr_diw_start;

/* Initialize the variables necessary for drawing a line.
 * This involves setting up start/stop positions and display window
 * borders. Also, since the drawing code may not fully overwrite all
 * releveant areas in the pixdata.apixels array for speed reasons, parts of
 * it must possibly be cleared here. */
static void pfield_init_linetoscr (void)
{
    int ddf_left, ddf_right;
    int mindelay = bpldelay1, maxdelay = bpldelay2;
    if (bpldelay1 > bpldelay2)
	maxdelay = bpldelay1, mindelay = bpldelay2;

    linetoscr_diw_start = dp_for_drawing->diwfirstword;
    linetoscr_diw_end = dp_for_drawing->diwlastword;

    /* We should really look at DDF also when calculating max_diwstop/min_diwstrt,
     * so that centering works better, but I'm afraid that might cost too many
     * cycles. Plus it's dangerous, see the code below that handles the case
     * with sprites. */
    if (dip_for_drawing->nr_sprites == 0) {
	int hiresadjust = bplhires ? 4 : 8;
	ddf_left = ((dp_for_drawing->plfstrt + hiresadjust)*2 + mindelay - DISPLAY_LEFT_SHIFT) << lores_shift;
	ddf_right = ((dp_for_drawing->plfstrt + dp_for_drawing->plflinelen + hiresadjust)*2 + maxdelay - DISPLAY_LEFT_SHIFT) << lores_shift;

	if (linetoscr_diw_start < ddf_left)
	    linetoscr_diw_start = ddf_left;
	if (linetoscr_diw_end > ddf_right)
	    linetoscr_diw_end = ddf_right;

	if (mindelay != maxdelay) {
	    /* Raahh...
	     * We just clear the maximum amount of space that may need to be
	     * cleared. We could do this exactly, but it would come out slower
	     * because of the overhead. */
	    int strt = ddf_left;
	    if (currprefs.gfx_lores == 2) {
		fuzzy_memset_le32 (pixdata.apixels, 0, (ddf_left>>1), 15);
		fuzzy_memset_le32 (pixdata.apixels, 0, (ddf_right>>1) - 15, 15);
	    } else if (currprefs.gfx_lores) {
		fuzzy_memset_le32 (pixdata.apixels, 0, ddf_left, 15);
		fuzzy_memset_le32 (pixdata.apixels, 0, ddf_right - 15, 15);
	    } else {
		fuzzy_memset_le32 (pixdata.apixels, 0, ddf_left, 30);
		fuzzy_memset_le32 (pixdata.apixels, 0, ddf_right - 30, 30);
	    }
	}
    } else {
	int hiresadjust = bplhires ? 4 : 8;
	/* We swap mindelay and maxdelay here to get rid of the mindelay != maxdelay check.
	 * Since we have to do a memset anyway (because there may be sprites),
	 * we might as well clear all at once. */
	ddf_left = ((dp_for_drawing->plfstrt + hiresadjust)*2 + maxdelay - DISPLAY_LEFT_SHIFT) << lores_shift;
	ddf_right = ((dp_for_drawing->plfstrt + dp_for_drawing->plflinelen + hiresadjust)*2 + mindelay - DISPLAY_LEFT_SHIFT) << lores_shift;
	if (linetoscr_diw_start < ddf_left) {
	    int strt = linetoscr_diw_start;
	    int stop = ddf_left;
#ifdef LORES_HACK
	    if (currprefs.gfx_lores == 2)
		strt >>= 1, stop >>= 1;
#endif
	    if (strt < stop)
		fuzzy_memset (pixdata.apixels, 0, strt, stop - strt);
	}

	if (linetoscr_diw_end > ddf_right) {
	    int strt = ddf_right;
	    int stop = linetoscr_diw_end;
#ifdef LORES_HACK
	    if (currprefs.gfx_lores == 2)
		strt >>= 1, stop >>= 1;
#endif
	    if (strt < stop)
		fuzzy_memset (pixdata.apixels, 0, strt, stop - strt);
	}
    }
    /* Perverse cases happen. */
    if (linetoscr_diw_end < linetoscr_diw_start)
	linetoscr_diw_end = linetoscr_diw_start;
}

static void pfield_do_linetoscr(int start, int stop)
{
    int lframe_end = linetoscr_diw_start, diw_end = linetoscr_diw_end;

    if (stop > linetoscr_right_x)
	stop = linetoscr_right_x;
    if (start < linetoscr_x_adjust)
	start = linetoscr_x_adjust;

    if (lframe_end < start)
	lframe_end = start;
    if (diw_end > stop)
	diw_end = stop;

    if (start >= stop)
	return;

#if AGA_CHIPSET == 0
    if (start == linetoscr_x_adjust && stop == linetoscr_right_x) {
	switch (gfxvidinfo.pixbytes) {
	 case 1: pfield_linetoscr_full8 (start, lframe_end, diw_end, stop); break;
	 case 2: pfield_linetoscr_full16 (start, lframe_end, diw_end, stop); break;
	 case 3: pfield_linetoscr_full24 (start, lframe_end, diw_end, stop); break;
	 case 4: pfield_linetoscr_full32 (start, lframe_end, diw_end, stop); break;
	}
    } else {
	switch (gfxvidinfo.pixbytes) {
	 case 1: pfield_linetoscr_8 (start, lframe_end, diw_end, stop); break;
	 case 2: pfield_linetoscr_16 (start, lframe_end, diw_end, stop); break;
	 case 3: pfield_linetoscr_24 (start, lframe_end, diw_end, stop); break;
	 case 4: pfield_linetoscr_32 (start, lframe_end, diw_end, stop); break;
	}
    }
#else
    pfield_linetoscr_aga(start, lframe_end, diw_end, stop);
#endif
}

static void pfield_do_fill_line(int start, int stop)
{
    if (stop > linetoscr_right_x)
	stop = linetoscr_right_x;
    if (start < linetoscr_x_adjust)
	start = linetoscr_x_adjust;

    if (start >= stop)
	return;

    switch (gfxvidinfo.pixbytes) {
     case 1: fill_line_8 (xlinebuffer, start, stop); break;
     case 2: fill_line_16 (xlinebuffer, start, stop); break;
     case 3: fill_line_24 (xlinebuffer, start, stop); break;
     case 4: fill_line_32 (xlinebuffer, start, stop); break;
    }
}

static void pfield_do_linetoscr_full(int double_line)
{
    int start = linetoscr_x_adjust, stop = start + gfxvidinfo.width;
    int lframe_end = linetoscr_diw_start, diw_end = linetoscr_diw_end;
    if (lframe_end < start)
	lframe_end = start;
    if (diw_end > stop)
	diw_end = stop;

#if AGA_CHIPSET == 0
    if (double_line) {
	switch (gfxvidinfo.pixbytes) {
	 case 1: pfield_linetoscr_full8_double (start, lframe_end, diw_end, stop); break;
	 case 2: pfield_linetoscr_full16_double (start, lframe_end, diw_end, stop); break;
	 case 3: pfield_linetoscr_full24_double (start, lframe_end, diw_end, stop); break;
	 case 4: pfield_linetoscr_full32_double (start, lframe_end, diw_end, stop); break;
	}
    } else
	switch (gfxvidinfo.pixbytes) {
	 case 1: pfield_linetoscr_full8 (start, lframe_end, diw_end, stop); break;
	 case 2: pfield_linetoscr_full16 (start, lframe_end, diw_end, stop); break;
	 case 3: pfield_linetoscr_full24 (start, lframe_end, diw_end, stop); break;
	 case 4: pfield_linetoscr_full32 (start, lframe_end, diw_end, stop); break;
	}
#else
    pfield_linetoscr_aga(start, lframe_end, diw_end, stop);
#endif
}

/* Mouse stuff */
static void setdontcare(void)
{
    write_log ("Don't care mouse mode set\n");
    mousestate = dont_care_mouse;
    lastspr0x = lastmx; lastspr0y = lastmy;
    mstepx = defstepx; mstepy = defstepy;
}

static void setfollow(void)
{
    write_log ("Follow sprite mode set\n");
    mousestate = follow_mouse;
    lastdiffx = lastdiffy = 0;
    sprvbfl = 0;
    spr0ctl=spr0pos = 0;
    mstepx = defstepx; mstepy = defstepy;
}

uae_u32 mousehack_helper (void)
{
    int mousexpos, mouseypos;

#ifdef PICASSO96
    if (picasso_on) {
	mousexpos = lastmx + picasso96_state.XOffset;
	mouseypos = lastmy + picasso96_state.YOffset;
    } else
#endif
    {
	mousexpos = lastmx + linetoscr_x_adjust;
	if (lastmy >= gfxvidinfo.height)
	    lastmy = gfxvidinfo.height-1;
	mouseypos = native2amiga_line_map[lastmy] + thisframe_y_adjust - minfirstline;
	mouseypos <<= 1;
	mousexpos <<= (1-lores_shift);
	mousexpos += 2*DISPLAY_LEFT_SHIFT;
    }

    switch (m68k_dreg (regs, 0)) {
     case 0:
	return ievent_alive ? -1 : needmousehack ();
     case 1:
	ievent_alive = 10;
	return mousexpos;
     case 2:
	return mouseypos;
    }
    return 0;
}

void togglemouse(void)
{
    switch(mousestate) {
     case dont_care_mouse: setfollow(); break;
     case follow_mouse: setdontcare(); break;
     default: break; /* Nnnnnghh! */
    }
}

static __inline__ int adjust(int val)
{
    if (val>127)
	return 127;
    else if (val<-127)
	return -127;
    return val;
}

static void do_mouse_hack(void)
{
    int spr0x = ((spr0pos & 0xff) << 2) | ((spr0ctl & 1) << 1);
    int spr0y = ((spr0pos >> 8) | ((spr0ctl & 4) << 6)) << 1;
    int diffx, diffy;

    if (ievent_alive > 0) {
	mouse_x = mouse_y = 0;
	return;
    }
    switch (mousestate) {
     case normal_mouse:
	diffx = lastmx - lastsampledmx;
	diffy = lastmy - lastsampledmy;
	if (!newmousecounters) {
	    if (diffx > 127) diffx = 127;
	    if (diffx < -127) diffx = -127;
	    mouse_x += diffx;
	    if (diffy > 127) diffy = 127;
	    if (diffy < -127) diffy = -127;
	    mouse_y += diffy;
	}
	lastsampledmx += diffx; lastsampledmy += diffy;
	break;

     case dont_care_mouse:
	diffx = adjust (((lastmx-lastspr0x) * mstepx) >> 16);
	diffy = adjust (((lastmy-lastspr0y) * mstepy) >> 16);
	lastspr0x=lastmx; lastspr0y=lastmy;
	mouse_x+=diffx; mouse_y+=diffy;
	break;

     case follow_mouse:
	if (sprvbfl && sprvbfl-- >1) {
	    int mousexpos, mouseypos;

	    if ((lastdiffx > docal || lastdiffx < -docal) && lastspr0x != spr0x
		&& spr0x > plfstrt*4 + 34 + xcaloff && spr0x < plfstop*4 - xcaloff)
	    {
		int val = (lastdiffx << 16) / (spr0x - lastspr0x);
		if (val>=0x8000) mstepx=(mstepx*(calweight-1)+val)/calweight;
	    }
	    if ((lastdiffy > docal || lastdiffy < -docal) && lastspr0y != spr0y
		&& spr0y>plffirstline+ycaloff && spr0y<plflastline-ycaloff)
	    {
		int val = (lastdiffy<<16) / (spr0y-lastspr0y);
		if (val>=0x8000) mstepy=(mstepy*(calweight-1)+val)/calweight;
	    }
	    mousexpos = lastmx + linetoscr_x_adjust;
	    if (lastmy >= gfxvidinfo.height)
		lastmy = gfxvidinfo.height-1;
	    mouseypos = native2amiga_line_map[lastmy] + thisframe_y_adjust - minfirstline;
	    mouseypos <<= 1;
	    mousexpos <<= (1-lores_shift);
	    mousexpos += 2*DISPLAY_LEFT_SHIFT;
	    diffx = adjust ((((mousexpos + xoffs - spr0x) & ~1) * mstepx) >> 16);
	    diffy = adjust ((((mouseypos + yoffs - spr0y) & ~1) * mstepy) >> 16);
	    lastspr0x=spr0x; lastspr0y=spr0y;
	    lastdiffx=diffx; lastdiffy=diffy;
	    mouse_x+=diffx; mouse_y+=diffy;
	}
	break;
    }
}

 /*
  * register functions
  */

__inline__ uae_u16 DMACONR(void)
{
    return (dmacon | (bltstate==BLT_done ? 0 : 0x4000)
	    | (blt_info.blitzero ? 0x2000 : 0));
}
static __inline__ uae_u16 INTENAR(void) { return intena; }
uae_u16 INTREQR(void)
{
    return intreq | (currprefs.use_serial ? 0x0001 : 0);
}
static __inline__ uae_u16 ADKCONR(void) { return adkcon; }
static __inline__ uae_u16 VPOSR(void)
{
#if AGA_CHIPSET == 1
    return (vpos >> 8) | lof | 0x2300;
#elif defined (ECS_AGNUS)
    return (vpos >> 8) | lof | 0x2000;
#else
    return (vpos >> 8) | lof;
#endif
}
static void  VPOSW(uae_u16 v)
{
    if (lof != (v & 0x8000))
	lof_changed = 1;
    lof = v & 0x8000;
    /*
     * This register is much more fun on a real Amiga. You can program
     * refresh rates with it ;) But I won't emulate this...
     */
}
static __inline__ uae_u16 VHPOSR(void) { return (vpos << 8) | current_hpos(); }

static __inline__ void COP1LCH(uae_u16 v) { cop1lc = (cop1lc & 0xffff) | ((uae_u32)v << 16); }
static __inline__ void COP1LCL(uae_u16 v) { cop1lc = (cop1lc & ~0xffff) | (v & 0xfffe); }
static __inline__ void COP2LCH(uae_u16 v) { cop2lc = (cop2lc & 0xffff) | ((uae_u32)v << 16); }
static __inline__ void COP2LCL(uae_u16 v) { cop2lc = (cop2lc & ~0xffff) | (v & 0xfffe); }

static void  COPJMP1(uae_u16 a)
{
    coplc = cop1lc; copstate = COP_read;
    eventtab[ev_copper].active = 1; eventtab[ev_copper].oldcycles = cycles;
    eventtab[ev_copper].evtime = 4 + cycles; events_schedule();
    copper_active = 1;
    copper_waiting_for_blitter = 0;
}
static void  COPJMP2(uae_u16 a)
{
    coplc = cop2lc; copstate = COP_read;
    eventtab[ev_copper].active = 1; eventtab[ev_copper].oldcycles = cycles;
    eventtab[ev_copper].evtime = 4 + cycles; events_schedule();
    copper_active = 1;
    copper_waiting_for_blitter = 0;
}
static __inline__ void COPCON(uae_u16 a) { copcon = a; }
static void  DMACON(uae_u16 v)
{
    int i, need_resched = 0;

    uae_u16 oldcon = dmacon;

    decide_line();
    setclr(&dmacon,v); dmacon &= 0x1FFF;
    /* ??? post_decide_line (); */

    /* FIXME? Maybe we need to think a bit more about the master DMA enable
     * bit in these cases. */
    if ((dmacon & DMA_COPPER) > (oldcon & DMA_COPPER)) {
	COPJMP1(0);
    }
    if ((dmacon & DMA_BLITPRI) > (oldcon & DMA_BLITPRI) && bltstate != BLT_done) {
	static int count = 0;
	if (!count) {
	    count = 1;
	    write_log ("warning: program is doing blitpri hacks.\n");
	}
	regs.spcflags |= SPCFLAG_BLTNASTY;
    }
#ifndef DONT_WANT_SOUND
    for (i = 0; i < 4; i++) {
	struct audio_channel_data *cdp = audio_channel + i;

	cdp->dmaen = (dmacon & 0x200) && (dmacon & (1<<i));
	if (cdp->dmaen) {
	    if (cdp->state == 0) {
		cdp->state = 1;
		cdp->pt = cdp->lc;
		cdp->wper = cdp->per;
		cdp->wlen = cdp->len;
		cdp->data_written = 2;
		eventtab[ev_aud0 + i].oldcycles = eventtab[ev_hsync].oldcycles;
		eventtab[ev_aud0 + i].evtime = eventtab[ev_hsync].evtime;
		eventtab[ev_aud0 + i].active = 1;
		need_resched = 1; /* not _really_ necessary here, but... */
	    }
	} else {
	    if (cdp->state == 1 || cdp->state == 5) {
		cdp->state = 0;
		cdp->current_sample = 0;
		eventtab[ev_aud0 + i].active = 0;
		need_resched = 1;
	    }
	}
    }
#endif
    calculate_copper_cycle_time ();
    if (copper_active && !eventtab[ev_copper].active) {
	eventtab[ev_copper].active = 1;
	eventtab[ev_copper].oldcycles = cycles;
	eventtab[ev_copper].evtime = 1 + cycles;
	need_resched = 1;
    }
    if (need_resched)
	events_schedule();
}

/*static int trace_intena = 0;*/

static __inline__ void INTENA (uae_u16 v)
{
/*    if (trace_intena)
	fprintf(stderr, "INTENA: %04x\n", v);*/
    setclr(&intena,v); regs.spcflags |= SPCFLAG_INT;
}
void INTREQ (uae_u16 v)
{
    setclr(&intreq,v);
    regs.spcflags |= SPCFLAG_INT;
    if ((v&0x8800)==0x0800) serdat&=0xbfff;
}

static void ADKCON (uae_u16 v)
{
    unsigned long t;
    setclr (&adkcon,v);
    t = adkcon | (adkcon >> 4);
    audio_channel[0].adk_mask = (((t >> 0) & 1) - 1);
    audio_channel[1].adk_mask = (((t >> 1) & 1) - 1);
    audio_channel[2].adk_mask = (((t >> 2) & 1) - 1);
    audio_channel[3].adk_mask = (((t >> 3) & 1) - 1);
}

static void BPLPTH (uae_u16 v, int num)
{
  decide_line ();
  decide_plane();
  do_modulos();
  bplpt[num] = (bplpt[num] & 0xffff) | ((uae_u32)v << 16);
}
static void BPLPTL (uae_u16 v, int num)
{
  decide_line ();
  decide_plane();
  do_modulos();
  bplpt[num] = (bplpt[num] & ~0xffff) | (v & 0xfffe);
}

static void BPLCON0 (uae_u16 v)
{
#if AGA_CHIPSET == 0
    v &= 0xFF0E;
    /* The Sanity WOC demo needs this at one place (at the end of the "Party Effect")
     * Disable bitplane DMA if someone tries to do more than 4 Hires bitplanes. */
    if ((v & 0xF000) > 0xC000)
	v &= 0xFFF;
    /* Don't want 7 lores planes either. */
    if ((v & 0x8000) == 0 && (v & 0x7000) == 0x7000)
	v &= 0xEFFF;
#endif
    if (bplcon0 == v)
	return;
    decide_line ();
    bplcon0 = v;
    nr_planes_from_bplcon0 = (bplcon0 >> 12) & 7;
    corrected_nr_planes_from_bplcon0 = ((bplcon0 >> 12) & 7) << (bplcon0 & 0x8000 ? 1 : 0);
    post_decide_line ();
    calculate_copper_cycle_time ();
#if 0
    calcdiw(); /* This should go away. */
#endif
}
static __inline__ void BPLCON1 (uae_u16 v)
{
    if (bplcon1 == v)
	return;
    decide_diw ();
    bplcon1 = v;
    decide_delay ();
}
static __inline__ void BPLCON2 (uae_u16 v)
{
    if (bplcon2 != v)
	decide_line ();
    bplcon2 = v;
}
static __inline__ void BPLCON3 (uae_u16 v)
{
    if (bplcon3 != v)
	decide_line ();
    bplcon3 = v;
}
static __inline__ void BPLCON4 (uae_u16 v)
{
    if (bplcon4 != v)
	decide_line ();
    bplcon4 = v;
}

static void BPL1MOD (uae_u16 v)
{
    v &= ~1;
    if ((uae_s16)bpl1mod == (uae_s16)v)
	return;
    bpl1mod = v;
    decide_modulos ();
}

static void BPL2MOD (uae_u16 v)
{
    v &= ~1;
    if ((uae_s16)bpl2mod == (uae_s16)v)
	return;
    bpl2mod = v;
    decide_modulos();
}

/* We could do as well without those... */
static __inline__ void BPL1DAT (uae_u16 v) { bpl1dat = v; }
static __inline__ void BPL2DAT (uae_u16 v) { bpl2dat = v; }
static __inline__ void BPL3DAT (uae_u16 v) { bpl3dat = v; }
static __inline__ void BPL4DAT (uae_u16 v) { bpl4dat = v; }
static __inline__ void BPL5DAT (uae_u16 v) { bpl5dat = v; }
static __inline__ void BPL6DAT (uae_u16 v) { bpl6dat = v; }

static void DIWSTRT (uae_u16 v)
{
    if (diwstrt == v)
	return;
    decide_line ();
    diwstrt = v;
    calcdiw();
}
static void DIWSTOP (uae_u16 v)
{
    if (diwstop == v)
	return;
    diwstop = v;
    calcdiw();
}
static void DDFSTRT (uae_u16 v)
{
    v &= 0xFF;
    if (ddfstrt == v)
	return;
    decide_line ();
    ddfstrt = v;
    calcdiw();
}
static void DDFSTOP (uae_u16 v)
{
    v &= 0xFF;
    if (ddfstop == v)
	return;
    decide_line ();
    ddfstop = v;
    calcdiw();
}

static void  BLTADAT(uae_u16 v)
{
    maybe_blit();

    blt_info.bltadat = v;
}
/*
 * "Loading data shifts it immediately" says the HRM. Well, that may
 * be true for BLTBDAT, but not for BLTADAT - it appears the A data must be
 * loaded for every word so that AFWM and ALWM can be applied.
 */
static void  BLTBDAT(uae_u16 v)
{
    maybe_blit();

    if (bltcon1 & 2)
	blt_info.bltbhold = v << (bltcon1 >> 12);
    else
	blt_info.bltbhold = v >> (bltcon1 >> 12);
    blt_info.bltbdat = v;
}
static void BLTCDAT(uae_u16 v) { maybe_blit(); blt_info.bltcdat = v; }

static void BLTAMOD(uae_u16 v) { maybe_blit(); blt_info.bltamod = (uae_s16)(v & 0xFFFE); }
static void BLTBMOD(uae_u16 v) { maybe_blit(); blt_info.bltbmod = (uae_s16)(v & 0xFFFE); }
static void BLTCMOD(uae_u16 v) { maybe_blit(); blt_info.bltcmod = (uae_s16)(v & 0xFFFE); }
static void BLTDMOD(uae_u16 v) { maybe_blit(); blt_info.bltdmod = (uae_s16)(v & 0xFFFE); }

static void BLTCON0(uae_u16 v) { maybe_blit(); bltcon0 = v; blinea_shift = v >> 12; }
/* The next category is "Most useless hardware register".
 * And the winner is... */
static void BLTCON0L(uae_u16 v) { maybe_blit(); bltcon0 = (bltcon0 & 0xFF00) | (v & 0xFF); }
static void BLTCON1(uae_u16 v) { maybe_blit(); bltcon1 = v; }

static void BLTAFWM(uae_u16 v) { maybe_blit(); blt_info.bltafwm = v; }
static void BLTALWM(uae_u16 v) { maybe_blit(); blt_info.bltalwm = v; }

static void BLTAPTH(uae_u16 v) { maybe_blit(); bltapt = (bltapt & 0xffff) | ((uae_u32)v << 16); }
static void BLTAPTL(uae_u16 v) { maybe_blit(); bltapt = (bltapt & ~0xffff) | (v & 0xFFFE); }
static void BLTBPTH(uae_u16 v) { maybe_blit(); bltbpt = (bltbpt & 0xffff) | ((uae_u32)v << 16); }
static void BLTBPTL(uae_u16 v) { maybe_blit(); bltbpt = (bltbpt & ~0xffff) | (v & 0xFFFE); }
static void BLTCPTH(uae_u16 v) { maybe_blit(); bltcpt = (bltcpt & 0xffff) | ((uae_u32)v << 16); }
static void BLTCPTL(uae_u16 v) { maybe_blit(); bltcpt = (bltcpt & ~0xffff) | (v & 0xFFFE); }
static void BLTDPTH(uae_u16 v) { maybe_blit(); bltdpt = (bltdpt & 0xffff) | ((uae_u32)v << 16); }
static void BLTDPTL(uae_u16 v) { maybe_blit(); bltdpt = (bltdpt & ~0xffff) | (v & 0xFFFE); }
static void BLTSIZE(uae_u16 v)
{
    bltsize = v;

    maybe_blit();

    blt_info.vblitsize = bltsize >> 6;
    blt_info.hblitsize = bltsize & 0x3F;
    if (!blt_info.vblitsize) blt_info.vblitsize = 1024;
    if (!blt_info.hblitsize) blt_info.hblitsize = 64;

    bltstate = BLT_init;
    do_blitter();
}
static void BLTSIZV(uae_u16 v)
{
    maybe_blit();
    oldvblts = v & 0x7FFF;
}
static void BLTSIZH(uae_u16 v)
{
    maybe_blit();
    blt_info.hblitsize = v & 0x7FF;
    blt_info.vblitsize = oldvblts;
    if (!blt_info.vblitsize) blt_info.vblitsize = 32768;
    if (!blt_info.hblitsize) blt_info.hblitsize = 0x800;
    bltstate = BLT_init;
    do_blitter();
}
static __inline__ void SPRxCTL_1(uae_u16 v, int num)
{
    int sprxp;
    sprctl[num] = v;
    nr_armed -= sprarmed[num];
    sprarmed[num] = 0;
    if (sprpos[num] == 0 && v == 0) {
	sprst[num] = SPR_stop;
	spron[num] = 0;
    } else
	sprst[num] = SPR_waiting_start;

    sprxp = ((sprpos[num] & 0xFF) * 2) + (v & 1) - DISPLAY_LEFT_SHIFT;
    if (!currprefs.gfx_lores)
	sprxp *= 2;
    sprxpos[num] = sprxp;
    sprvstart[num] = (sprpos[num] >> 8) | ((sprctl[num] << 6) & 0x100);
    sprvstop[num] = (sprctl[num] >> 8) | ((sprctl[num] << 7) & 0x100);
}
static __inline__ void SPRxPOS_1(uae_u16 v, int num)
{
    int sprxp;
    sprpos[num] = v;
    sprxp = ((v & 0xFF) * 2) + (sprctl[num] & 1) - DISPLAY_LEFT_SHIFT;
    if (!currprefs.gfx_lores)
	sprxp *= 2;
    sprxpos[num] = sprxp;
    sprvstart[num] = (sprpos[num] >> 8) | ((sprctl[num] << 6) & 0x100);
}
static __inline__ void SPRxDATA_1(uae_u16 v, int num)
{
    sprdata[num] = v;
    nr_armed += 1 - sprarmed[num];
    sprarmed[num] = 1;
}
static __inline__ void SPRxDATB_1(uae_u16 v, int num)
{
    sprdatb[num] = v;
}
static void SPRxDATA(uae_u16 v, int num) { decide_sprites (); SPRxDATA_1 (v, num); }
static void SPRxDATB(uae_u16 v, int num) { decide_sprites (); SPRxDATB_1 (v, num); }
static void SPRxCTL(uae_u16 v, int num) { decide_sprites (); SPRxCTL_1 (v, num); }
static void SPRxPOS(uae_u16 v, int num) { decide_sprites (); SPRxPOS_1 (v, num); }
static void SPRxPTH(uae_u16 v, int num)
{
    decide_line ();
    sprpt[num] &= 0xffff;
    sprpt[num] |= (uae_u32)v << 16;

    if (sprst[num] == SPR_stop || vpos < vblank_endline)
	sprst[num] = SPR_restart;
    spron[num] = 1;
}
static void SPRxPTL(uae_u16 v, int num)
{
    decide_line ();
    sprpt[num] &= ~0xffff;
    sprpt[num] |= v;

    if (sprst[num] == SPR_stop || vpos < vblank_endline)
	sprst[num] = SPR_restart;
    spron[num] = 1;
}
static void CLXCON(uae_u16 v)
{
    clxcon = v;
    clx_sprmask = (((v >> 15) << 7) | ((v >> 14) << 5) | ((v >> 13) << 3) | ((v >> 12) << 1) | 0x55);
}
static uae_u16 CLXDAT(void)
{
    uae_u16 v = clxdat;
    clxdat = 0;
    return v;
}
static void COLOR(uae_u16 v, int num)
{
    int r,g,b;
    int cr,cg,cb;
    int colreg;

    v &= 0xFFF;
#if AGA_CHIPSET == 1
    {
	/* XXX Broken */
	uae_u32 cval;
	colreg = ((bplcon3 >> 13) & 7) * 32 + num;
	r = (v & 0xF00) >> 8;
	g = (v & 0xF0) >> 4;
	b = (v & 0xF) >> 0;
	cr = color_regs[colreg] >> 16;
	cg = (color_regs[colreg] >> 8) & 0xFF;
	cb = color_regs[colreg] & 0xFF;

	if (bplcon3 & 0x200) {
	    cr &= 0xF0; cr |= r;
	    cg &= 0xF0; cg |= g;
	    cb &= 0xF0; cb |= b;
	} else {
	    cr = r + (r << 4);
	    cg = g + (g << 4);
	    cb = b + (b << 4);
	}
	cval = (cr << 16) | (cg << 8) | cb;
	if (cval == color_regs[colreg])
	    return;
	color_regs[colreg] = cval;
	pfield_may_need_update(1);
    }
#else
    {
	if (current_colors.color_regs[num] == v)
	    return;
	/* Call this with the old table still intact. */
	record_color_change (num, v);
	remembered_color_entry = -1;
	current_colors.color_regs[num] = v;
	current_colors.acolors[num] = xcolors[v];
    }
#endif
}

static void  DSKSYNC(uae_u16 v) { dsksync = v; }
static void  DSKDAT(uae_u16 v) { write_log ("DSKDAT written. Not good.\n"); }
static void  DSKPTH(uae_u16 v) { dskpt = (dskpt & 0xffff) | ((uae_u32)v << 16); }
static void  DSKPTL(uae_u16 v) { dskpt = (dskpt & ~0xffff) | (v); }

static void  DSKLEN(uae_u16 v)
{
    if (v & 0x8000) {
	dskdmaen = dskdmaen == 1 ? 2 : 1;
    } else {
	dskdmaen = 0;
	if (eventtab[ev_diskblk].active)
	    write_log ("warning: Disk DMA aborted!\n");
	eventtab[ev_diskblk].active = 0;
	events_schedule();

    }
    dsklen = dsklength = v; dsklength &= 0x3fff;
    if (dskdmaen == 2 && dsksync != 0x4489 && (adkcon & 0x400)) {
	sprintf (warning_buffer, "Non-standard sync: %04x len: %x\n", dsksync, dsklength);
	write_log (warning_buffer);
    }
    if (dskdmaen > 1) {
	if (dsklen & 0x4000) {
	    eventtab[ev_diskblk].active = 1;
	    eventtab[ev_diskblk].oldcycles = cycles;
	    eventtab[ev_diskblk].evtime = 40 + cycles; /* ??? */
	    events_schedule();
	} else {
	    int result = DISK_PrepareReadMFM(dsklength, dsksync, adkcon & 0x400);
	    if (result) {
		eventtab[ev_diskblk].active = 1;
		eventtab[ev_diskblk].oldcycles = cycles;
		eventtab[ev_diskblk].evtime = result + cycles;
		events_schedule();
	    }
	}
    }
}

static uae_u16 DSKBYTR(void)
{
    uae_u16 v = (dsklen >> 1) & 0x6000;
    uae_u16 mfm, byte;
    if (DISK_GetData(&mfm, &byte))
	v |= 0x8000;
    v |= byte;
    if (dsksync == mfm) v |= 0x1000;
    return v;
}

static uae_u16 DSKDATR(void)
{
    uae_u16 mfm, byte;
    DISK_GetData(&mfm, &byte);
    return mfm;
}

static uae_u16 potgo_value;

static void POTGO(uae_u16 v)
{
    potgo_value = v;
}

static uae_u16 POTGOR(void)
{
    uae_u16 v = (potgo_value | (potgo_value << 1)) & 0xAA00;
    v |= v >> 1;

    if (JSEM_ISMOUSE (0, currprefs.fake_joystick)) {
	if (buttonstate[2])
	    v &= 0xFBFF;

	if (buttonstate[1])
	    v &= 0xFEFF;
    }
    else if (JSEM_ISJOY0(0, currprefs.fake_joystick) || JSEM_ISJOY1(0, currprefs.fake_joystick)) {
	if (joy0button & 2) v &= 0xfbff;
	if (joy0button & 4) v &= 0xfeff;
    }

    if (JSEM_ISJOY0(1, currprefs.fake_joystick) || JSEM_ISJOY1(1, currprefs.fake_joystick)) {
	if (joy1button & 2) v &= 0xbfff;
	if (joy1button & 4) v &= 0xefff;
    }

    return v;
}
static uae_u16 POT0DAT(void)
{
    static uae_u16 cnt = 0;
    if (JSEM_ISMOUSE (0, currprefs.fake_joystick)) {
	if (buttonstate[2])
	    cnt = ((cnt + 1) & 0xFF) | (cnt & 0xFF00);
	if (buttonstate[1])
	    cnt += 0x100;
    }

    return cnt;
}
static uae_u16 JOY0DAT(void)
{
    if (JSEM_ISMOUSE (0, currprefs.fake_joystick)) {
	do_mouse_hack();
	return ((uae_u8)mouse_x) + ((uae_u16)mouse_y << 8);
    }
    return joy0dir;
}
static uae_u16 JOY1DAT(void)
{
    if (JSEM_ISMOUSE (1, currprefs.fake_joystick)) {
	do_mouse_hack();
	return ((uae_u8)mouse_x) + ((uae_u16)mouse_y << 8);
    }
    return joy1dir;
}
static void JOYTEST(uae_u16 v)
{
    if (JSEM_ISMOUSE (0, currprefs.fake_joystick)) {
	mouse_x = v & 0xFC;
	mouse_y = (v >> 8) & 0xFC;
    }
}

/*
 * Here starts the copper code. It should be rewritten.
 */

#define COPPER_ALTERNATIVE 1
/*
 * Calculate the minimum number of cycles after which the
 * copper comparison becomes true. This is quite tricky. I hope it works.
 */
static int calc_copcomp_true(int currvpos, int currhpos)
{
    uae_u16 vp = currvpos & (((copi2 >> 8) & 0x7F) | 0x80);
    uae_u16 hp = currhpos & (copi2 & 0xFE);
    uae_u16 vcmp = ((copi1 & (copi2 | 0x8000)) >> 8);
    uae_u16 hcmp = (copi1 & copi2 & 0xFE);
    int copper_time_hpos;
    int cycleadd = maxhpos - currhpos;
    int coptime = 0;
    int maxwait = 32767;

#if COPPER_ALTERNATIVE == 0
    /* This is a kludge... the problem is that there are programs that wait for
     * FFDDFFFE and then for a line in the second display half, and this doesn't
     * work without this. I _think_ the reason why it works on the Amiga is that
     * the last cycle in the line isn't available for the copper, but I'm not sure.
     * OTOH, I'm pretty convinced that the copper timings are correct otherwise, so
     * I added this rather than changing something else. */
    if (hcmp == 0xDC)
	hcmp += 2;
#endif
    decide_line();
    /* Copper DMA is turned off in Hires 4 bitplane mode */
    if (vp == vcmp
	&& decided_nr_planes == 4 /* -1 if before plfstrt */
	&& decided_hires
	&& currhpos >= thisline_decision.plfstrt
	&& currhpos < thisline_decision.plfstrt + thisline_decision.plflinelen)
	coptime += thisline_decision.plfstrt + thisline_decision.plflinelen - currhpos;

    if ((vp > vcmp || (vp == vcmp && hp >= hcmp)) && ((copi2 & 0x8000) || !(DMACONR() & 0x4000)))
	return coptime;

    try_again:

    while (vp < vcmp) {
	currvpos++;
	if (currvpos > maxvpos + 1)
	    return -1;
	currhpos = 0;
	coptime += cycleadd;
	cycleadd = maxhpos;
	vp = currvpos & (((copi2 >> 8) & 0x7F) | 0x80);
    }
    if ((bplcon0 & 0xF000) == 0xC000) {
	if (coptime > 0)
	    return coptime;
	maxwait = plfstrt - current_hpos();
	if (maxwait < 0)
	    maxwait = 32767;
    }
    copper_time_hpos = currhpos;
    hp = copper_time_hpos & (copi2 & 0xFE);
    if (!(vp > vcmp)) {
	while ((int)hp < ((int)hcmp)) {
	    currhpos++;
	    /* Copper DMA is turned off in Hires 4 bitplane mode */
	    if (decided_nr_planes != 4 /* -1 if before plfstrt */
		|| !decided_hires
		|| current_hpos () >= (thisline_decision.plfstrt + thisline_decision.plflinelen))
		copper_time_hpos++;

	    if (currhpos > maxhpos) {
		/* Now, what? There might be a good position on the
		 * next line. But it can also be the FFFF FFFE
		 * case.
		 */
		currhpos = 0;
		currvpos++;
		vp = currvpos & (((copi2 >> 8) & 0x7F) | 0x80);
		goto try_again;
	    }
	    coptime++;
	    if (coptime >= maxwait)
		return coptime;
	    hp = copper_time_hpos & (copi2 & 0xFE);
	}
    }
    if (coptime == 0) /* waiting for the blitter */
	return 1;

    return coptime;
}

/*
 * Simple version of the above which only tries to get vpos correct.
 */
static int calc_copcomp_true_vpos(int currvpos, int currhpos)
{
    uae_u16 vp = currvpos & (((copi2 >> 8) & 0x7F) | 0x80);
    uae_u16 hp = currhpos & (copi2 & 0xFE);
    uae_u16 vcmp = ((copi1 & (copi2 | 0x8000)) >> 8);
    uae_u16 hcmp = (copi1 & copi2 & 0xFE);
    int copper_time_hpos;
    int cycleadd = maxhpos - currhpos;
    int coptime = 0;
#if COPPER_ALTERNATIVE == 0
    /* see above */
    if (hcmp == 0xDC)
	hcmp += 2;
#endif
    if ((vp > vcmp || (vp == vcmp && hp >= hcmp)) && ((copi2 & 0x8000) || !(DMACONR() & 0x4000)))
	return 0;

    while (vp < vcmp) {
	currvpos++;
	if (currvpos > maxvpos + 1)
	    return -1;
	currhpos = 0;
	coptime += cycleadd;
	cycleadd = maxhpos;
	vp = currvpos & (((copi2 >> 8) & 0x7F) | 0x80);
    }
    return coptime;
}

static enum copper_states cop_cmds[4] = { COP_move, COP_wait, COP_move, COP_skip };

/* This function is not always correct, but it's the best we can do. */
static int copper_memory_cycles (int n)
{
    int current_cycle = current_hpos ();
    int n_needed = 0;
    int planes = corrected_nr_planes_from_bplcon0;

    while (n) {
	/* This sucks. The DIW may end vertically at exactly this point. Therefore,
	 * we must call decide_line(). If we're vertically outside the DIW,
	 * copper_cycle_time will be != -1 after this call [Sanity Interference] */
	decide_line ();
#if COPPER_ALTERNATIVE == 1
	if (current_cycle >= (maxhpos-3)) {
	    n_needed += maxhpos - current_cycle;
	    current_cycle = 0;
	    continue;
	}
#endif
	if (copper_cycle_time == -1 && current_cycle >= 0x14
	    && current_cycle >= plfstrt && current_cycle < plfstrt + plflinelen
	    && (planes == 6 || (planes == 5 && (current_cycle % 8) < 4)))
	{
	    n_needed += 4;
	    current_cycle += 4;
	} else {
	    n_needed += 2;
	    current_cycle += 2;
	}
	if (current_cycle >= maxhpos)
	    current_cycle -= maxhpos;
	n--;
    }
    return n_needed;
}

static __inline__ int calc_copper_cycles (int n_cycles)
{
    return /*copper_cycle_time != -1 ? copper_cycle_time * n_cycles : */copper_memory_cycles (n_cycles);
}

static __inline__ int copper_init_read (int n_cycles)
{
    if (dmaen(DMA_COPPER)){
	int t = calc_copper_cycles (n_cycles);

	eventtab[ev_copper].evtime = t + cycles;
	return 1;
    } else {
	copstate = COP_read;
	eventtab[ev_copper].active = 0;
	return 0;
    }
}

static __inline__ void copper_read (void)
{
    int cmd;

    copi1 = chipmem_bank.wget(coplc);
    copi2 = chipmem_bank.wget(coplc+2);
    coplc += 4;
    eventtab[ev_copper].oldcycles = cycles;

    cmd = (copi1 & 1) | ((copi2 & 1) << 1);
    copstate = cop_cmds[cmd];
    eventtab[ev_copper].oldcycles = cycles;
}

static __inline__ void handle_bltfinish_wait (void)
{
    if (bltstate == BLT_done) {
	copstate = COP_read;
	/* Don't need to wait. Experimental: No copper wakeup time in this case. */
    } else {
	eventtab[ev_copper].active = 0;
	copstate = COP_morewait;
	copper_waiting_for_blitter = 1;
    }
}

void blitter_done_notify (void)
{
    if (copper_waiting_for_blitter) {
	copper_waiting_for_blitter = 0;
	eventtab[ev_copper].active = 1;
	eventtab[ev_copper].oldcycles = cycles;
	eventtab[ev_copper].evtime = 1 + cycles;
	events_schedule ();
    }
}

#if 0
static void do_copper_cheat(void)
{
    int coptime, t;
    int nmoves = 100; /*??*/
    int nextcycles = eventtab[ev_hsync].evtime + currprefs.copper_pos;

    eventtab[ev_copper].evtime = nextcycles;
    eventtab[ev_copper].oldcycles = cycles;
    nextcycles -= cycles;

    if (!dmaen(DMA_COPPER)) {
	eventtab[ev_copper].active = 0;
	return;
    }
    for (; nmoves; nmoves--)
	switch(copstate){
	 case COP_read:
	    copper_read ();
	    break;

	 case COP_read_ignore:
	    copper_read ();
	    copstate = COP_read;
	    break;

	 case COP_move:
	    if (copi1 >= (copcon & 2 ? 0x40u : 0x80u)) {
		custom_bank.wput(copi1,copi2);
		copstate = COP_read;
	    } else {
		copstate = COP_stop;
		eventtab[ev_copper].active = 0;
		copper_active = 0;
		return;
	    }
	    break;

	 case COP_skip:
	    copstate = COP_read;
	    if (calc_copcomp_true(vpos, current_hpos()) == 0)
		copstate = COP_read_ignore;
	    break;

	 case COP_wait:
	    coptime = calc_copcomp_true(vpos, current_hpos());
	    if (coptime < 0) {
		copstate = COP_stop;
		eventtab[ev_copper].active = 0;
		copper_active = 0;
		return;
	    }
	    if (coptime == 0) {
		copper_read();
		if (vpos != 255)
		    break;
		eventtab[ev_copper].evtime = cycles + 6;
		return;
	    }
	    if (coptime >= nextcycles)
		return;
	    if (vpos == 255)
		eventtab[ev_copper].evtime = cycles + coptime;
	    /*else
		copstate = COP_read;*/
	    return;

	 case COP_stop:
	    eventtab[ev_copper].active = 0;
	    copper_active = 0;
	    return;

	 case COP_morewait:
	 case COP_do_read:
	 case COP_do_read_ignore:
	    abort();
	}
}
#endif

static void do_copper(void)
{
    int coptime, t;
    for (;;)
	switch(copstate){
	 case COP_read:
	    eventtab[ev_copper].oldcycles = cycles;
	    if (!copper_init_read (2))
		return;
	    copstate = COP_do_read;
	    return;

	 case COP_do_read:
	    copper_read ();
	    break;

	 case COP_read_ignore:
	    eventtab[ev_copper].oldcycles = cycles;
	    if (!copper_init_read (2))
		return;
	    copstate = COP_do_read_ignore;
	    return;

	 case COP_do_read_ignore:
	    copper_read ();
	    copstate = COP_read;
	    break;

	 case COP_move:
	    if (copi1 >= (copcon & 2 ? 0x40u : 0x80u)) {
		custom_bank.wput(copi1,copi2);
		copstate = COP_read;
		break;
	    } else {
		copstate = COP_stop;
		eventtab[ev_copper].active = 0;
		copper_active = 0;
	    }
	    return;

	 case COP_skip:
	    copstate = COP_read;
	    if (calc_copcomp_true(vpos, current_hpos()) == 0)
		copstate = COP_read_ignore;
	    break;

	 case COP_wait:
	    /* Recognize blitter wait statements. This is a speed optimization
	     * only.*/
	    if (copi1 == 1 && copi2 == 0) {
		handle_bltfinish_wait ();
		if (!eventtab[ev_copper].active)
		    return;
		break;
	    }
	    coptime = calc_copcomp_true_vpos(vpos, current_hpos());
	    if (coptime > 0) {
		copstate = COP_morewait;
		eventtab[ev_copper].oldcycles = cycles;
		eventtab[ev_copper].evtime = coptime + cycles;
		return;
	    }
	    coptime = calc_copcomp_true(vpos, current_hpos());
	    if (coptime < 0) {
		copstate = COP_stop;
		eventtab[ev_copper].active = 0;
		copper_active = 0;
		return;
	    }
	    if (coptime) {
		copstate = COP_morewait;
		eventtab[ev_copper].evtime = coptime + cycles;
		return;
	    }
	    copstate = COP_read;
	    /* Experimental: no copper wakeup time in this case. The HRM says
	     * nothing about this. */
	    break;

	 case COP_morewait:
	    coptime = calc_copcomp_true(vpos, current_hpos());
	    if (coptime < 0) {
		copstate = COP_stop;
		eventtab[ev_copper].active = 0;
		copper_active = 0;
		return;
	    }
	    if (coptime) {
		eventtab[ev_copper].evtime = coptime + cycles;
		return;
	    }
	    /* Copper wakeup time: 1 memory cycle, plus 2 for the next read */
	    eventtab[ev_copper].oldcycles = cycles;
	    if (!copper_init_read (3))
		return;
	    copstate = COP_do_read;
	    return;

	 case COP_stop:
	    eventtab[ev_copper].active = 0;
	    copper_active = 0;
	    return;
	}
}

static void diskblk_handler(void)
{
    regs.spcflags |= SPCFLAG_DISK;
    eventtab[ev_diskblk].active = 0;
}

void do_disk(void)
{
    if (dskdmaen != 2 && (regs.spcflags & SPCFLAG_DISK)) {
	static int warned = 0;
	if (!warned)
	    warned++, write_log ("BUG!\n");
	return;
    }
    if (dmaen(0x10)){
	if (dsklen & 0x4000) {
	    if (!chipmem_bank.check (dskpt, 2*dsklength)) {
		write_log ("warning: Bad disk write DMA pointer\n");
	    } else {
		uae_u8 *mfmp = get_real_address (dskpt);
		int i;
		DISK_InitWrite();

		for (i = 0; i < dsklength; i++) {
		    uae_u16 d = (*mfmp << 8) + *(mfmp+1);
		    mfmwrite[i] = d;
		    mfmp += 2;
		}
		DISK_WriteData(dsklength);
	    }
	} else {
	    int result = DISK_ReadMFM (dskpt);
	}
	regs.spcflags &= ~SPCFLAG_DISK;
	INTREQ(0x9002);
	dskdmaen = -1;
    }
}

static void gen_pfield_tables(void)
{
    int i;
    union {
	struct {
	    uae_u8 a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p;
	} foo;
	struct {
	    uae_u32 a, b, c, d;
	} bar;
    } baz;

    for (i = 0; i < 256; i++) {
	/* We lose every second pixel in HiRes if UAE runs in a 320x200 screen. */
	baz.foo.a = i & 64 ? 1 : 0;
	baz.foo.b = i & 16 ? 1 : 0;
	baz.foo.c = i & 4 ? 1 : 0;
	baz.foo.d = i & 1 ? 1 : 0;
	hirestab_l[i][0] = baz.bar.a;

	baz.foo.a = i & 128 ? 1 : 0;
	baz.foo.b = i & 64 ? 1 : 0;
	baz.foo.c = i & 32 ? 1 : 0;
	baz.foo.d = i & 16 ? 1 : 0;
	baz.foo.e = i & 8 ? 1 : 0;
	baz.foo.f = i & 4 ? 1 : 0;
	baz.foo.g = i & 2 ? 1 : 0;
	baz.foo.h = i & 1 ? 1 : 0;
	lorestab_l[i][0] = baz.bar.a;
	lorestab_l[i][1] = baz.bar.b;
	sprtaba[i] = ((((i >> 7) & 1) << 0)
		      | (((i >> 6) & 1) << 2)
		      | (((i >> 5) & 1) << 4)
		      | (((i >> 4) & 1) << 6)
		      | (((i >> 3) & 1) << 8)
		      | (((i >> 2) & 1) << 10)
		      | (((i >> 1) & 1) << 12)
		      | (((i >> 0) & 1) << 14));
	sprtabb[i] = sprtaba[i] * 2;
	clxtab[i] = ((((i & 3) && (i & 12)) << 9)
		     | (((i & 3) && (i & 48)) << 10)
		     | (((i & 3) && (i & 192)) << 11)
		     | (((i & 12) && (i & 48)) << 12)
		     | (((i & 12) && (i & 192)) << 13)
		     | (((i & 48) && (i & 192)) << 14));
    }

    for (i = 0; i < 256; i++) {
	baz.foo.a = i & 128 ? 1 : 0;
	baz.foo.b = i & 64 ? 1 : 0;
	baz.foo.c = i & 32 ? 1 : 0;
	baz.foo.d = i & 16 ? 1 : 0;
	baz.foo.e = i & 8 ? 1 : 0;
	baz.foo.f = i & 4 ? 1 : 0;
	baz.foo.g = i & 2 ? 1 : 0;
	baz.foo.h = i & 1 ? 1 : 0;
	hirestab_h[i][0] = baz.bar.a;
	hirestab_h[i][1] = baz.bar.b;

	baz.foo.a = i & 128 ? 1 : 0;
	baz.foo.b = i & 128 ? 1 : 0;
	baz.foo.c = i & 64 ? 1 : 0;
	baz.foo.d = i & 64 ? 1 : 0;
	baz.foo.e = i & 32 ? 1 : 0;
	baz.foo.f = i & 32 ? 1 : 0;
	baz.foo.g = i & 16 ? 1 : 0;
	baz.foo.h = i & 16 ? 1 : 0;
	baz.foo.i = i & 8 ? 1 : 0;
	baz.foo.j = i & 8 ? 1 : 0;
	baz.foo.k = i & 4 ? 1 : 0;
	baz.foo.l = i & 4 ? 1 : 0;
	baz.foo.m = i & 2 ? 1 : 0;
	baz.foo.n = i & 2 ? 1 : 0;
	baz.foo.o = i & 1 ? 1 : 0;
	baz.foo.p = i & 1 ? 1 : 0;
	lorestab_h[i][0] = baz.bar.a;
	lorestab_h[i][1] = baz.bar.b;
	lorestab_h[i][2] = baz.bar.c;
	lorestab_h[i][3] = baz.bar.d;
    }
}

static void do_sprites(int currvp, int currhp)
{
    int i;
    /* The graph in the HRM, p. 195 seems to indicate that sprite 0 is
     * fetched at cycle 0x14 and thus can't be disabled by bitplane DMA. */
    int maxspr = currhp/4 - 0x14/4;
#if 0
    if (currvp == 0)
	return;
#else
    /* I don't know whether this is right. Some programs write the sprite pointers
     * directly at the start of the copper list. With the currvp==0 check, the
     * first two words of data are read on the second line in the frame. The problem
     * occurs when the program jumps to another copperlist a few lines further down
     * which _also_ writes the sprite pointer registers. This means that a) writing
     * to the sprite pointers sets the state to SPR_restart; or b) that sprite DMA
     * is disabled until the end of the vertical blanking interval. The HRM
     * isn't clear - it says that the vertical sprite position can be set to any
     * value, but this wouldn't be the first mistake... */
    /* Update: I modified one of the programs to write the sprite pointers the
     * second time only _after_ the VBlank interval, and it showed the same behaviour
     * as it did unmodified under UAE with the above check. This indicates that the
     * solution below is correct. */
    if (currvp < vblank_endline)
	return;
#endif
    if (maxspr < 0)
	return;
    if (maxspr > 8)
	maxspr = 8;

    for (i = 0; i < maxspr; i++) {
	int fetch = 0;

	if (spron[i] == 0)
	    continue;

	if (sprst[i] == SPR_restart) {
	    fetch = 2;
	    spron[i] = 1;
	} else if ((sprst[i] == SPR_waiting_start && sprvstart[i] == vpos) || sprst[i] == SPR_waiting_stop) {
	    fetch = 1;
	    sprst[i] = SPR_waiting_stop;
	}
	if (sprst[i] == SPR_waiting_stop && sprvstop[i] == vpos) {
	    fetch = 2;
	    sprst[i] = SPR_waiting_start;
	}

	if (fetch && dmaen(DMA_SPRITE)) {
	    uae_u16 data1 = chipmem_bank.wget(sprpt[i]);
	    uae_u16 data2 = chipmem_bank.wget(sprpt[i]+2);
	    sprpt[i] += 4;

	    if (fetch == 1) {
		/* Hack for X mouse auto-calibration */
		if (i == 0 && !sprvbfl && ((sprpos[0] & 0xff) << 2) > 2 * DISPLAY_LEFT_SHIFT) {
		    spr0ctl=sprctl[0];
		    spr0pos=sprpos[0];
		    sprvbfl=2;
		}
		SPRxDATB_1(data2, i);
		SPRxDATA_1(data1, i);
	    } else {
		SPRxPOS_1(data1, i);
		SPRxCTL_1(data2, i);
	    }
	}
    }
}

static uae_u32 attach_2nd;

static uae_u32 do_sprite_collisions (struct sprite_draw *spd, int prev_overlap, int i, int nr_spr)
{
    int j;
    int sprxp = spd[i].linepos;
    uae_u32 datab = spd[i].datab;
    uae_u32 mask2 = 0;
    int sbit = 1 << spd[i].num;
    int sprx_shift = 1;
    int attach_compare = -1;
    if (currprefs.gfx_lores != 1)
	sprx_shift = 0;

    attach_2nd = 0;
    if ((spd[i].num & 1) == 1 && (spd[i].ctl & 0x80) == 0x80)
	attach_compare = spd[i].num - 1;

    for (j = prev_overlap; j < i; j++) {
	uae_u32 mask1;
	int off;

	if (spd[i].num < spd[j].num)
	    continue;

	off = sprxp - spd[j].linepos;
	off <<= sprx_shift;
	mask1 = spd[j].datab >> off;

	/* If j is an attachment for i, then j doesn't block i */
	if (spd[j].num == attach_compare) {
	    attach_2nd |= mask1;
	} else {
	    mask1 |= (mask1 & 0xAAAAAAAA) >> 1;
	    mask1 |= (mask1 & 0x55555555) << 1;
	    if (datab & mask1)
		clxdat |= clxtab[(sbit | (1 << spd[j].num)) & clx_sprmask];
	    mask2 |= mask1;
	}
    }
    for (j = i+1; j < nr_spr; j++) {
	uae_u32 mask1;
	int off = spd[j].linepos - sprxp;

	if (off >= sprite_width || spd[i].num < spd[j].num)
	    break;

	off <<= sprx_shift;
	mask1 = spd[j].datab << off;

	/* If j is an attachment for i, then j doesn't block i */
	if (spd[j].num == attach_compare) {
	    attach_2nd |= mask1;
	} else {
	    mask1 |= (mask1 & 0xAAAAAAAA) >> 1;
	    mask1 |= (mask1 & 0x55555555) << 1;
	    if (datab & mask1)
		clxdat |= clxtab[(sbit | (1 << spd[j].num)) & clx_sprmask];
	    mask2 |= mask1;
	}
    }
    datab &= ~mask2;
    return datab;
}

static __inline__ void render_sprite (int spr, int sprxp, uae_u32 datab, int ham, int attch, int sprx_inc)
{
    uae_u32 datcd;
    int basecol = 16;
    if (!attch)
	basecol += (spr & ~1)*2;
    if (bpldualpf)
	basecol |= 128;
    if (attch)
	datcd = attach_2nd;

    for(; attch ? datab | datcd : datab; sprxp += sprx_inc) {
	int col;

	if (attch) {
	    col = ((datab & 3) << 2) | (datcd & 3);
	    datcd >>= 2;
	} else
	    col = datab & 3;
	datab >>= 2;
	if (col) {
	    col |= basecol;
	    if (ham) {
		col = colors_for_drawing.color_regs[col];
		ham_linebuf[sprxp] = col;
		if (sprx_inc == 2) {
		    ham_linebuf[sprxp+1] = col;
		}
	    } else {
		pixdata.apixels[sprxp] = col;
		if (sprx_inc == 2) {
		    pixdata.apixels[sprxp+1] = col;
		}
	    }
	}
    }
}

static void walk_sprites (struct sprite_draw *spd, int nr_spr)
{
    int i, prev_overlap;
    int last_sprite_pos = -64;
    uae_u32 plane1 = 0, plane2 = 0;
    int sprx_inc = 1, sprx_shift = 1;

    if (currprefs.gfx_lores == 0)
	sprx_inc = 2, sprx_shift = 0;

    prev_overlap = 0;
    for (i = 0; i < nr_spr; i++) {
	int sprxp = spd[i].linepos;
	int m = 1 << spd[i].num;
	uae_u32 datab;
	while (prev_overlap < i && spd[prev_overlap].linepos + sprite_width <= sprxp)
	    prev_overlap++;

	datab = do_sprite_collisions (spd, prev_overlap, i, nr_spr);
#ifdef LORES_HACK
	if (currprefs.gfx_lores == 2)
	    sprxp >>= 1;
#endif
	if ((bpldualpf && plfpri[1] < 256) || (plfpri[2] < 256)) {
	    if (sprxp != last_sprite_pos) {
		int ok = last_sprite_pos-sprxp+16;
		int ok2;
		if (ok <= 0) {
		    ok = ok2 = 0;
		    plane1 = 0;
		    plane2 = 0;
		} else {
		    ok2 = ok << sprx_shift;
		    plane1 >>= 32 - ok2;
		    plane2 >>= 32 - ok2;
		}
		last_sprite_pos = sprxp;

		if (bpldualpf) {
		    uae_u32 mask = 3 << ok2;
		    int p = sprxp+ok;

		    for (; mask; mask <<= 2, p += sprx_inc) {
			int data = pixdata.apixels[p];
			if (dblpf_2nd2[data] == 2)
			    plane2 |= mask;
			if (dblpf_2nd1[data] == 1)
			    plane1 |= mask;
		    }
		} else {
		    uae_u32 mask = 3 << ok2;
		    int p = sprxp+ok;

		    for (; mask; mask <<= 2, p += sprx_inc) {
			if (pixdata.apixels[p])
			    plane2 |= mask;
		    }
		}
	    }
	    if (bpldualpf && m >= plfpri[1]) {
		datab &= ~plane1;
		attach_2nd &= ~plane1;
	    }
	    if (m >= plfpri[2]) {
		datab &= ~plane2;
		attach_2nd &= ~plane2;
	    }
	}
	if ((spd[i].num & 1) == 1 && (spd[i].ctl & 0x80) == 0x80) {
	    /* Attached sprite */
	    if (bplham) {
		if (sprx_inc == 1) {
		    render_sprite (spd[i].num, sprxp, datab, 1, 1, 1);
		} else {
		    render_sprite (spd[i].num, sprxp, datab, 1, 1, 2);
		}
	    } else {
		if (sprx_inc == 1) {
		    render_sprite (spd[i].num, sprxp, datab, 0, 1, 1);
		} else {
		    render_sprite (spd[i].num, sprxp, datab, 0, 1, 2);
		}
	    }
	    /* This still leaves one attached sprite bug, but at the moment I'm too lazy */
	    if (i + 1 < nr_spr && spd[i+1].num == spd[i].num - 1 && spd[i+1].linepos == spd[i].linepos)
		i++;
	} else {
	    if (bplham) {
		if (sprx_inc == 1) {
		    render_sprite (spd[i].num, sprxp, datab, 1, 0, 1);
		} else {
		    render_sprite (spd[i].num, sprxp, datab, 1, 0, 2);
		}
	    } else {
		if (sprx_inc == 1) {
		    render_sprite (spd[i].num, sprxp, datab, 0, 0, 1);
		} else {
		    render_sprite (spd[i].num, sprxp, datab, 0, 0, 2);
		}
	    }
	}
    }
}

#ifndef SMART_UPDATE
#undef UNALIGNED_PROFITABLE
#endif

#ifdef UNALIGNED_PROFITABLE

static void pfield_doline_unaligned_h (int lineno)
{
    int xpos = dp_for_drawing->plfstrt * 4 - DISPLAY_LEFT_SHIFT * 2;

    if (bplhires) {
	int xpos1 = xpos + 16 + bpldelay1*2;
	int xpos2;
	uae_u8 *dataptr = line_data[lineno];
	int len = dp_for_drawing->plflinelen >> 1;

	if (len <= 0)
	    return;

	if (bpldelay1 == bpldelay2) {
	    switch (bplplanecnt) {
	     case 1: set_hires_h_0_0 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 2: set_hires_h_1_0 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 3: set_hires_h_2_0 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 4: set_hires_h_3_0 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 5: set_hires_h_4_0 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 6: set_hires_h_5_0 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 7: set_hires_h_6_0 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 8: set_hires_h_7_0 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	    }
	} else {
	    switch (bplplanecnt) {
	     case 1: case 2: set_hires_h_0_1 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 3: case 4: set_hires_h_1_1 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 5: case 6: set_hires_h_2_1 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 7: case 8: set_hires_h_3_1 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	    }

	    xpos2 = xpos + 16 + bpldelay2*2;
	    switch (bplplanecnt) {
	     case 2: case 3: set_hires_h_0_2 ((uae_u32 *)(pixdata.apixels + xpos2), dataptr, len); break;
	     case 4: case 5: set_hires_h_1_2 ((uae_u32 *)(pixdata.apixels + xpos2), dataptr, len); break;
	     case 6: case 7: set_hires_h_2_2 ((uae_u32 *)(pixdata.apixels + xpos2), dataptr, len); break;
	     case 8:         set_hires_h_3_2 ((uae_u32 *)(pixdata.apixels + xpos2), dataptr, len); break;
	    }
	}
    } else {
	int xpos1 = xpos + 32 + bpldelay1*2;
	int xpos2;
	uae_u8 *dataptr = line_data[lineno];
	int len = dp_for_drawing->plflinelen >> 2;

	if (len <= 0)
	    return;

	if (bpldelay1 == bpldelay2) {
	    switch (bplplanecnt) {
	     case 1: set_lores_h_0_0 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 2: set_lores_h_1_0 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 3: set_lores_h_2_0 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 4: set_lores_h_3_0 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 5: set_lores_h_4_0 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 6: set_lores_h_5_0 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 7: set_lores_h_6_0 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 8: set_lores_h_7_0 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	    }
	} else {
	    switch (bplplanecnt) {
	     case 1: case 2: set_lores_h_0_1 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 3: case 4: set_lores_h_1_1 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 5: case 6: set_lores_h_2_1 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 7: case 8: set_lores_h_3_1 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	    }

	    xpos2 = xpos + 32 + bpldelay2*2;
	    switch (bplplanecnt) {
	     case 2: case 3: set_lores_h_0_2 ((uae_u32 *)(pixdata.apixels + xpos2), dataptr, len); break;
	     case 4: case 5: set_lores_h_1_2 ((uae_u32 *)(pixdata.apixels + xpos2), dataptr, len); break;
	     case 6: case 7: set_lores_h_2_2 ((uae_u32 *)(pixdata.apixels + xpos2), dataptr, len); break;
	     case 8:         set_lores_h_3_2 ((uae_u32 *)(pixdata.apixels + xpos2), dataptr, len); break;
	    }
	}
    }
}

static void pfield_doline_unaligned_l (int lineno)
{
    int xpos = dp_for_drawing->plfstrt * 2 - DISPLAY_LEFT_SHIFT;

    if (bplhires) {
	int xpos1 = xpos + 8 + bpldelay1;
	int xpos2;
	uae_u8 *dataptr = line_data[lineno];
	int len = dp_for_drawing->plflinelen >> 1;

	if (len <= 0)
	    return;

	if (bpldelay1 == bpldelay2) {
	    switch (bplplanecnt) {
	     case 1: set_hires_l_0_0 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 2: set_hires_l_1_0 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 3: set_hires_l_2_0 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 4: set_hires_l_3_0 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 5: set_hires_l_4_0 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 6: set_hires_l_5_0 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 7: set_hires_l_6_0 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 8: set_hires_l_7_0 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	    }
	} else {
	    switch (bplplanecnt) {
	     case 1: case 2: set_hires_l_0_1 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 3: case 4: set_hires_l_1_1 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 5: case 6: set_hires_l_2_1 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 7: case 8: set_hires_l_3_1 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	    }

	    xpos2 = xpos + 8 + bpldelay2;
	    switch (bplplanecnt) {
	     case 2: case 3: set_hires_l_0_2 ((uae_u32 *)(pixdata.apixels + xpos2), dataptr, len); break;
	     case 4: case 5: set_hires_l_1_2 ((uae_u32 *)(pixdata.apixels + xpos2), dataptr, len); break;
	     case 6: case 7: set_hires_l_2_2 ((uae_u32 *)(pixdata.apixels + xpos2), dataptr, len); break;
	     case 8:         set_hires_l_3_2 ((uae_u32 *)(pixdata.apixels + xpos2), dataptr, len); break;
	    }
	}
    } else {
	int xpos1 = xpos + 16 + bpldelay1;
	int xpos2;
	uae_u8 *dataptr = line_data[lineno];
	int len = dp_for_drawing->plflinelen >> 2;

	if (len <= 0)
	    return;

	if (bpldelay1 == bpldelay2) {
	    switch (bplplanecnt) {
	     case 1: set_hires_h_0_0 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 2: set_hires_h_1_0 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 3: set_hires_h_2_0 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 4: set_hires_h_3_0 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 5: set_hires_h_4_0 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 6: set_hires_h_5_0 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 7: set_hires_h_6_0 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 8: set_hires_h_7_0 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	    }
	} else {
	    switch (bplplanecnt) {
	     case 1: case 2: set_hires_h_0_1 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 3: case 4: set_hires_h_1_1 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 5: case 6: set_hires_h_2_1 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	     case 7: case 8: set_hires_h_3_1 ((uae_u32 *)(pixdata.apixels + xpos1), dataptr, len); break;
	    }

	    xpos2 = xpos + 16 + bpldelay2;
	    switch (bplplanecnt) {
	     case 2: case 3: set_hires_h_0_2 ((uae_u32 *)(pixdata.apixels + xpos2), dataptr, len); break;
	     case 4: case 5: set_hires_h_1_2 ((uae_u32 *)(pixdata.apixels + xpos2), dataptr, len); break;
	     case 6: case 7: set_hires_h_2_2 ((uae_u32 *)(pixdata.apixels + xpos2), dataptr, len); break;
	     case 8:         set_hires_h_3_2 ((uae_u32 *)(pixdata.apixels + xpos2), dataptr, len); break;
	    }
	}
    }
}

#define pfield_doline_h pfield_doline_unaligned_h
#define pfield_doline_l pfield_doline_unaligned_l

#else /* not UNALIGNED_PROFITABLE */


static __inline__ void pfield_orword_hires_h(int data, unsigned char *dp, int bit)
{
    uae_u32 *pixptr = (uae_u32 *)dp;

    *pixptr |= hirestab_h[data >> 8][0] << bit;
    *(pixptr+1) |= hirestab_h[data >> 8][1] << bit;
    *(pixptr+2) |= hirestab_h[data & 255][0] << bit;
    *(pixptr+3) |= hirestab_h[data & 255][1] << bit;
}

static __inline__ void pfield_orword_lores_h(int data, unsigned char *dp, int bit)
{
    uae_u32 *pixptr = (uae_u32 *)dp;

    *pixptr |= lorestab_h[data >> 8][0] << bit;
    *(pixptr+1) |= lorestab_h[data >> 8][1] << bit;
    *(pixptr+2) |= lorestab_h[data >> 8][2] << bit;
    *(pixptr+3) |= lorestab_h[data >> 8][3] << bit;
    *(pixptr+4) |= lorestab_h[data & 255][0] << bit;
    *(pixptr+5) |= lorestab_h[data & 255][1] << bit;
    *(pixptr+6) |= lorestab_h[data & 255][2] << bit;
    *(pixptr+7) |= lorestab_h[data & 255][3] << bit;
}

static __inline__ void pfield_setword_hires_h(int data, unsigned char *dp, int bit)
{
    uae_u32 *pixptr = (uae_u32 *)dp;

    *pixptr = hirestab_h[data >> 8][0] << bit;
    *(pixptr+1) = hirestab_h[data >> 8][1] << bit;
    *(pixptr+2) = hirestab_h[data & 255][0] << bit;
    *(pixptr+3) = hirestab_h[data & 255][1] << bit;
}

static __inline__ void pfield_setword_lores_h(int data, unsigned char *dp, int bit)
{
    uae_u32 *pixptr = (uae_u32 *)dp;

    *pixptr = lorestab_h[data >> 8][0] << bit;
    *(pixptr+1) = lorestab_h[data >> 8][1] << bit;
    *(pixptr+2) = lorestab_h[data >> 8][2] << bit;
    *(pixptr+3) = lorestab_h[data >> 8][3] << bit;
    *(pixptr+4) = lorestab_h[data & 255][0] << bit;
    *(pixptr+5) = lorestab_h[data & 255][1] << bit;
    *(pixptr+6) = lorestab_h[data & 255][2] << bit;
    *(pixptr+7) = lorestab_h[data & 255][3] << bit;
}

static __inline__ void pfield_orword_hires_l(int data, unsigned char *dp, int bit)
{
    uae_u32 *pixptr = (uae_u32 *)dp;

    *pixptr |= hirestab_l[data >> 8][0] << bit;
    *(pixptr+1) |= hirestab_l[data & 255][0] << bit;
}

static __inline__ void pfield_orword_lores_l(int data, unsigned char *dp, int bit)
{
    uae_u32 *pixptr = (uae_u32 *)dp;

    *pixptr |= lorestab_l[data >> 8][0] << bit;
    *(pixptr+1) |= lorestab_l[data >> 8][1] << bit;
    *(pixptr+2) |= lorestab_l[data & 255][0] << bit;
    *(pixptr+3) |= lorestab_l[data & 255][1] << bit;
}

static __inline__ void pfield_setword_hires_l(int data, unsigned char *dp, int bit)
{
    uae_u32 *pixptr = (uae_u32 *)dp;

    *pixptr = hirestab_l[data >> 8][0] << bit;
    *(pixptr+1) = hirestab_l[data & 255][0] << bit;
}

static __inline__ void pfield_setword_lores_l(int data, unsigned char *dp, int bit)
{
    uae_u32 *pixptr = (uae_u32 *)dp;

    *pixptr = lorestab_l[data >> 8][0] << bit;
    *(pixptr+1) = lorestab_l[data >> 8][1] << bit;
    *(pixptr+2) = lorestab_l[data & 255][0] << bit;
    *(pixptr+3) = lorestab_l[data & 255][1] << bit;
}

#define DO_ONE_PLANE(POINTER, MULT, FUNC, DELAY, LL_SUB, P_ADD) { \
    int i; \
    unsigned int bpldat1; \
    uae_u16 data; \
    unsigned int bpldat2 = 0; \
    uae_u8 *ptr = (POINTER); \
    for (i = dp_for_drawing->plflinelen; i > 0; i -= LL_SUB) { \
	bpldat1 = bpldat2; \
	bpldat2 = do_get_mem_word ((uae_u16 *)ptr); \
	ptr+=2; \
	data = (bpldat1 << (16 - DELAY)) | (bpldat2 >> DELAY); \
	FUNC(data, app, MULT); \
	app += P_ADD; \
    } \
    data = bpldat2 << (16 - DELAY); \
    FUNC(data, app, MULT); \
}

#ifdef SMART_UPDATE
#define DATA_POINTER(n) (dataptr + (n)*MAX_WORDS_PER_LINE*2)
#else
#define DATA_POINTER(n) (real_bplpt[n])
#endif

static void pfield_doline_aligned_h (int lineno)
{
    int xpos = dp_for_drawing->plfstrt * 4 - DISPLAY_LEFT_SHIFT * 2;

    if (bplhires) {
	if (bplplanecnt > 0) {
	    int xpos1 = xpos + 16 + (bpldelay1 >= 8 ? 16 : 0);
	    int xpos2 = xpos + 16 + (bpldelay2 >= 8 ? 16 : 0);
	    int delay1 = 2*(bpldelay1 & 7);
	    int delay2 = 2*(bpldelay2 & 7);
	    unsigned char *app = pixdata.apixels + xpos1;
	    uae_u8 *dataptr = line_data[lineno];

	    DO_ONE_PLANE(DATA_POINTER(0), 0, pfield_setword_hires_h, delay1, 4, 16);
	    if (bplplanecnt > 2) {
		app = pixdata.apixels + xpos1;
		DO_ONE_PLANE(DATA_POINTER(2), 2, pfield_orword_hires_h, delay1, 4, 16);
	    }
#if AGA_CHIPSET == 1
	    if (bplplanecnt > 4) {
		app = pixdata.apixels + xpos1;
		DO_ONE_PLANE(DATA_POINTER(4), 4, pfield_orword_hires_h, delay1, 4, 16);
	    }
	    if (bplplanecnt > 6) {
		app = pixdata.apixels + xpos1;
		DO_ONE_PLANE(DATA_POINTER(6), 6, pfield_orword_hires_h, delay1, 4, 16);
	    }
#endif
	    if (bplplanecnt > 1) {
		app = pixdata.apixels + xpos2;
		DO_ONE_PLANE(DATA_POINTER(1), 1, pfield_orword_hires_h, delay2, 4, 16);
	    }
	    if (bplplanecnt > 3) {
		app = pixdata.apixels + xpos2;
		DO_ONE_PLANE(DATA_POINTER(3), 3, pfield_orword_hires_h, delay2, 4, 16);
	    }
#if AGA_CHIPSET == 1
	    if (bplplanecnt > 5) {
		app = pixdata.apixels + xpos2;
		DO_ONE_PLANE(DATA_POINTER(5), 5, pfield_orword_hires_h, delay2, 4, 16);
	    }
	    if (bplplanecnt > 7) {
		app = pixdata.apixels + xpos2;
		DO_ONE_PLANE(DATA_POINTER(7), 7, pfield_orword_hires_h, delay2, 4, 16);
	    }
#endif
	} else {
	    memset(pixdata.apixels, 0, sizeof(pixdata.apixels));
	}
    } else {
	if (bplplanecnt > 0) {
	    int x = xpos + 32;
	    unsigned char *app = pixdata.apixels + x;
	    uae_u8 *dataptr = line_data[lineno];

	    DO_ONE_PLANE(DATA_POINTER(0), 0, pfield_setword_lores_h, bpldelay1, 8, 32);
	    if (bplplanecnt > 2) {
		app = pixdata.apixels + x;
		DO_ONE_PLANE(DATA_POINTER(2), 2, pfield_orword_lores_h, bpldelay1, 8, 32);
	    }
	    if (bplplanecnt > 4) {
		app = pixdata.apixels + x;
		DO_ONE_PLANE(DATA_POINTER(4), 4, pfield_orword_lores_h, bpldelay1, 8, 32);
	    }
#if AGA_CHIPSET == 1
	    if (bplplanecnt > 6) {
		app = pixdata.apixels + x;
		DO_ONE_PLANE(DATA_POINTER(6), 6, pfield_orword_lores_h, bpldelay1, 8, 32);
	    }
#endif
	    if (bplplanecnt > 1) {
		app = pixdata.apixels + x;
		DO_ONE_PLANE(DATA_POINTER(1), 1, pfield_orword_lores_h, bpldelay2, 8, 32);
	    }
	    if (bplplanecnt > 3) {
		app = pixdata.apixels + x;
		DO_ONE_PLANE(DATA_POINTER(3), 3, pfield_orword_lores_h, bpldelay2, 8, 32);
	    }
	    if (bplplanecnt > 5) {
		app = pixdata.apixels + x;
		DO_ONE_PLANE(DATA_POINTER(5), 5, pfield_orword_lores_h, bpldelay2, 8, 32);
	    }
#if AGA_CHIPSET == 1
	    if (bplplanecnt > 7) {
		app = pixdata.apixels + x;
		DO_ONE_PLANE(DATA_POINTER(7), 7, pfield_orword_lores_h, bpldelay2, 8, 32);
	    }
#endif
	} else {
	    memset(pixdata.apixels, 0, sizeof(pixdata.apixels));
	}
    }
}

static void pfield_doline_aligned_l (int lineno)
{
    int xpos = dp_for_drawing->plfstrt * 2 - DISPLAY_LEFT_SHIFT;

    if (bplhires) {
	if (bplplanecnt > 0) {
	    int xpos1 = xpos + 8 + (bpldelay1 >= 8 ? 8 : 0);
	    int xpos2 = xpos + 8 + (bpldelay2 >= 8 ? 8 : 0);
	    int delay1 = (bpldelay1 & 7) * 2;
	    int delay2 = (bpldelay2 & 7) * 2;
	    unsigned char *app = pixdata.apixels + xpos1;
	    uae_u8 *dataptr = line_data[lineno];

	    DO_ONE_PLANE(DATA_POINTER(0), 0, pfield_setword_hires_l, delay1, 4, 8);
	    if (bplplanecnt > 2) {
		app = pixdata.apixels + xpos1;
		DO_ONE_PLANE(DATA_POINTER(2), 2, pfield_orword_hires_l, delay1, 4, 8);
	    }
#if AGA_CHIPSET == 1
	    if (bplplanecnt > 4) {
		app = pixdata.apixels + xpos1;
		DO_ONE_PLANE(DATA_POINTER(4), 4, pfield_orword_hires_l, delay1, 4, 8);
	    }
	    if (bplplanecnt > 6) {
		app = pixdata.apixels + xpos1;
		DO_ONE_PLANE(DATA_POINTER(6), 6, pfield_orword_hires_l, delay1, 4, 8);
	    }
#endif
	    if (bplplanecnt > 1) {
		app = pixdata.apixels + xpos2;
		DO_ONE_PLANE(DATA_POINTER(1), 1, pfield_orword_hires_l, delay2, 4, 8);
	    }
	    if (bplplanecnt > 3) {
		app = pixdata.apixels + xpos2;
		DO_ONE_PLANE(DATA_POINTER(3), 3, pfield_orword_hires_l, delay2, 4, 8);
	    }
#if AGA_CHIPSET == 1
	    if (bplplanecnt > 5) {
		app = pixdata.apixels + xpos2;
		DO_ONE_PLANE(DATA_POINTER(5), 5, pfield_orword_hires_l, delay2, 4, 8);
	    }
	    if (bplplanecnt > 7) {
		app = pixdata.apixels + xpos2;
		DO_ONE_PLANE(DATA_POINTER(7), 7, pfield_orword_hires_l, delay2, 4, 8);
	    }
#endif
	} else {
	    memset(pixdata.apixels, 0, sizeof(pixdata.apixels));
	}
    } else {
	if (bplplanecnt > 0) {
	    int x = xpos + 16;
	    int delay1 = bpldelay1;
	    int delay2 = bpldelay2;
	    unsigned char *app = pixdata.apixels + x;
	    uae_u8 *dataptr = line_data[lineno];

	    DO_ONE_PLANE(DATA_POINTER(0), 0, pfield_setword_lores_l, delay1, 8, 16);
	    if (bplplanecnt > 2) {
		app = pixdata.apixels + x;
		DO_ONE_PLANE(DATA_POINTER(2), 2, pfield_orword_lores_l, delay1, 8, 16);
	    }
	    if (bplplanecnt > 4) {
		app = pixdata.apixels + x;
		DO_ONE_PLANE(DATA_POINTER(4), 4, pfield_orword_lores_l, delay1, 8, 16);
	    }
#if AGA_CHIPSET == 1
	    if (bplplanecnt > 6) {
		app = pixdata.apixels + x;
		DO_ONE_PLANE(DATA_POINTER(6), 6, pfield_orword_lores_l, delay1, 8, 16);
	    }
#endif
	    if (bplplanecnt > 1) {
		app = pixdata.apixels + x;
		DO_ONE_PLANE(DATA_POINTER(1), 1, pfield_orword_lores_l, delay2, 8, 16);
	    }
	    if (bplplanecnt > 3) {
		app = pixdata.apixels + x;
		DO_ONE_PLANE(DATA_POINTER(3), 3, pfield_orword_lores_l, delay2, 8, 16);
	    }
	    if (bplplanecnt > 5) {
		app = pixdata.apixels + x;
		DO_ONE_PLANE(DATA_POINTER(5), 5, pfield_orword_lores_l, delay2, 8, 16);
	    }
#if AGA_CHIPSET == 1
	    if (bplplanecnt > 7) {
		app = pixdata.apixels + x;
		DO_ONE_PLANE(DATA_POINTER(7), 7, pfield_orword_lores_l, delay2, 8, 16);
	    }
#endif
	} else {
	    memset(pixdata.apixels, 0, sizeof(pixdata.apixels));
	}
    }
}

#define pfield_doline_h pfield_doline_aligned_h
#define pfield_doline_l pfield_doline_aligned_l

#endif /* UNALIGNED_PROFITABLE */

static void pfield_adjust_delay (void)
{
    int ddf_left = dp_for_drawing->plfstrt;
    int ddf_right = dp_for_drawing->plfstrt + dp_for_drawing->plflinelen;
    int i;

    for (i = dip_for_drawing->last_delay_change-1; i >= dip_for_drawing->first_delay_change-1; i--) {
	int delayreg = i < dip_for_drawing->first_delay_change ? dp_for_drawing->bplcon1 : delay_changes[i].value;
	int delay1 = delayreg & 0xF;
	int delay2 = (delayreg >> 4) & 0xF;
	int startpos = i == dip_for_drawing->last_delay_change - 1 ? ddf_right + 8 : delay_changes[i+1].linepos;
	int stoppos = i < dip_for_drawing->first_delay_change ? ddf_left : delay_changes[i].linepos;
	int j;
	startpos = PIXEL_XPOS (startpos + (bplhires ? 4 : 8));
	stoppos = PIXEL_XPOS (stoppos + (bplhires ? 4 : 8));
	if (currprefs.gfx_lores == 0)
	    delay1 <<= 1, delay2 <<= 1;
	else if (currprefs.gfx_lores == 2)
	    startpos >>= 1, stoppos >>= 1;
	for (j = startpos-1; j >= stoppos; j--) {
	    pixdata.apixels [j] = (pixdata.apixels[j-delay1] & 0x55) | (pixdata.apixels[j-delay2] & 0xAA);
	}
    }
}

static void init_sprites (void)
{
    int i;

    for (i = 0; i < 8; i++) {
	/* ???? */
	sprst[i] = SPR_stop;
	spron[i] = 0;
    }

    memset(sprpos, 0, sizeof sprpos);
    memset(sprctl, 0, sizeof sprctl);
}

static void init_hardware_frame (void)
{
    next_lineno = 0;
    nln_how = 0;
    diwstate = DIW_waiting_start;
    hdiwstate = DIW_waiting_start;
}

void init_row_map(void)
{
    int i;
    if (gfxvidinfo.height > 2048) {
	write_log ("Resolution too high, aborting\n");
    }
    for (i = 0; i < gfxvidinfo.height + 1; i++)
	row_map[i] = gfxvidinfo.bufmem + gfxvidinfo.rowbytes * i;
}

/*
 * A raster line has been built in the graphics buffer. Tell the graphics code
 * to do anything necessary to display it.
 */
static void do_flush_line_1 (int lineno)
{
    if (lineno < first_drawn_line)
	first_drawn_line = lineno;
    if (lineno > last_drawn_line)
	last_drawn_line = lineno;

    if (gfxvidinfo.maxblocklines == 0)
	flush_line(lineno);
    else {
	if ((last_block_line+1) != lineno) {
	    if (first_block_line != -2)
		flush_block (first_block_line, last_block_line);
	    first_block_line = lineno;
	}
	last_block_line = lineno;
	if (last_block_line - first_block_line >= gfxvidinfo.maxblocklines) {
	    flush_block (first_block_line, last_block_line);
	    first_block_line = last_block_line = -2;
	}
    }
}

static __inline__ void do_flush_line (int lineno)
{
    /* We don't want to call X libraries from the second thread right now. */
#ifndef SUPPORT_PENGUINS
    do_flush_line_1 (lineno);
#else
    line_drawn[lineno] = 1;
#endif
}

/*
 * One drawing frame has been finished. Tell the graphics code about it.
 * Note that the actual flush_screen() call is a no-op for all reasonable
 * systems.
 */

static __inline__ void do_flush_screen (int start, int stop)
{
    int i;
#ifdef SUPPORT_PENGUINS
    for (i = 0; i < gfxvidinfo.height; i++) {
	if (line_drawn[i])
	    do_flush_line_1 (i);
    }
#endif
    if (gfxvidinfo.maxblocklines != 0 && first_block_line != -2) {
	flush_block (first_block_line, last_block_line);
    }
    if (start <= stop)
	flush_screen (start, stop);
}

static void adjust_drawing_colors (int ctable, int bplham)
{
    if (drawing_color_matches != ctable) {
	if (bplham) {
	    memcpy (&colors_for_drawing, curr_color_tables + ctable,
		    sizeof colors_for_drawing);
	    color_match_type = color_match_full;
	} else {
	    memcpy (colors_for_drawing.acolors, curr_color_tables[ctable].acolors,
		    sizeof colors_for_drawing.acolors);
	    color_match_type = color_match_acolors;
	}
	drawing_color_matches = ctable;
    } else if (bplham && color_match_type != color_match_full) {
	memcpy (colors_for_drawing.color_regs, curr_color_tables[ctable].color_regs,
		sizeof colors_for_drawing.color_regs);
	color_match_type = color_match_full;
    }
}

static __inline__ void adjust_color0_for_color_change (void)
{
    drawing_color_matches = -1;
    if (dp_for_drawing->color0 != 0xFFFFFFFFul) {
	colors_for_drawing.color_regs[0] = dp_for_drawing->color0;
	colors_for_drawing.acolors[0] = xcolors[dp_for_drawing->color0];
    }
}

static __inline__ void do_color_changes (line_draw_func worker)
{
    int lastpos = 0, nextpos, i;
    struct color_change *cc = curr_color_changes + dip_for_drawing->first_color_change;

    for (i = dip_for_drawing->first_color_change; i <= dip_for_drawing->last_color_change; i++, cc++) {
	if (i == dip_for_drawing->last_color_change)
	    nextpos = max_diwlastword;
	else
	    nextpos = PIXEL_XPOS (cc->linepos) + (COPPER_MAGIC_FUDGE << lores_shift);
	worker (lastpos, nextpos);
	if (i != dip_for_drawing->last_color_change) {
	    colors_for_drawing.color_regs[cc->regno] = cc->value;
	    colors_for_drawing.acolors[cc->regno] = xcolors[cc->value];
	}
	if (nextpos > lastpos) {
	    lastpos = nextpos;
	    if (lastpos >= linetoscr_right_x)
		break;
	}
    }
}

/* We only save hardware registers during the hardware frame. Now, when
 * drawing the frame, we expand the data into a slightly more useful
 * form. */
static void pfield_expand_dp_bplcon (void)
{
    bplhires = (dp_for_drawing->bplcon0 & 0x8000) == 0x8000;
    bplplanecnt = (dp_for_drawing->bplcon0 & 0x7000) >> 12;
    bplham = (dp_for_drawing->bplcon0 & 0x800) == 0x800;
#if AGA_CHIPSET == 1 /* The KILLEHB bit exists in ECS, but is apparently meant for Genlock
		      * stuff, and it's set by some demos (e.g. Andromeda Seven Seas) */
    bplehb = ((dp_for_drawing->bplcon0 & 0xFCC0) == 0x6000 && !(dp_for_drawing->bplcon2 & 0x200));
#else
    bplehb = (dp_for_drawing->bplcon0 & 0xFC00) == 0x6000;
#endif
    bpldelay1 = dp_for_drawing->bplcon1 & 0xF;
    bpldelay2 = (dp_for_drawing->bplcon1 >> 4) & 0xF;
    plfpri[1] = 1 << 2*(dp_for_drawing->bplcon2 & 7);
    plfpri[2] = 1 << 2*((dp_for_drawing->bplcon2 >> 3) & 7);
    bpldualpf = (dp_for_drawing->bplcon0 & 0x400) == 0x400;
    bpldualpfpri = (dp_for_drawing->bplcon2 & 0x40) == 0x40;
}

static __inline__ void pfield_draw_line(int lineno, int gfx_ypos, int follow_ypos)
{
    int border = 0;
    int to_screen = 0;
    int do_double = 0;

    dp_for_drawing = 0;
    dip_for_drawing = 0;
    switch (linestate[lineno]) {
     case LINE_AS_PREVIOUS:
     case LINE_REMEMBERED_AS_PREVIOUS:
	{
	    static int warned = 0;
	    if (!warned)
		write_log ("Shouldn't get here... this is a bug.\n"), warned++;
	}
	line_decisions[lineno].which = -2;
	return;

     case LINE_BORDER_PREV:
	border = 1;
	dp_for_drawing = line_decisions + lineno - 1;
	dip_for_drawing = curr_drawinfo + lineno - 1;
	break;

     case LINE_BORDER_NEXT:
	border = 1;
	dp_for_drawing = line_decisions + lineno + 1;
	dip_for_drawing = curr_drawinfo + lineno + 1;
	break;

     case LINE_DONE_AS_PREVIOUS:
	line_decisions[lineno].which = -2;
	/* fall through */
     case LINE_DONE:
	return;

     case LINE_DECIDED_DOUBLE:
	line_decisions[lineno+1].which = -2;
	if (follow_ypos != -1) {
	    do_double = 1;
	    linetoscr_double_offset = gfxvidinfo.rowbytes * (follow_ypos - gfx_ypos);
	}

	/* fall through */
     default:
	dip_for_drawing = curr_drawinfo + lineno;
	dp_for_drawing = line_decisions + lineno;
	if (dp_for_drawing->which != 1)
	    border = 1;
	break;
    }

    if (!line_changed[lineno] && !frame_redraw_necessary) {
	/* The case where we can skip redrawing this line. If this line
	 * is supposed to be doubled, and the next line is remembered as
	 * having been doubled, then the next line is done as well. */
	if (do_double) {
	    if (linestate[lineno+1] != LINE_REMEMBERED_AS_PREVIOUS) {
		if (gfxvidinfo.linemem == NULL)
		    memcpy (row_map[follow_ypos], row_map[gfx_ypos], gfxvidinfo.rowbytes);
		line_decisions[lineno + 1].which = -2;
		do_flush_line (follow_ypos);
	    }
	    linestate[lineno + 1] = LINE_DONE_AS_PREVIOUS;
	}
	linestate[lineno] = LINE_DONE;
	return;
    }

    if (!border) {
	xlinebuffer = gfxvidinfo.linemem;
	if (xlinebuffer == NULL)
	    xlinebuffer = row_map[gfx_ypos];
	xlinebuffer -= linetoscr_x_adjust_bytes;
	aga_lbufptr = aga_linebuf;

	pfield_expand_dp_bplcon ();

#ifdef LORES_HACK
	if (gfxvidinfo.can_double && !bplhires && !currprefs.gfx_lores
	    && dip_for_drawing->nr_color_changes == 0 && !bplham)
	    currprefs.gfx_lores = 2;
#endif
	pfield_init_linetoscr ();
	if (dip_for_drawing->first_delay_change != dip_for_drawing->last_delay_change) {
	    bpldelay1 = bpldelay2 = 0;
	    if (currprefs.gfx_lores)
		pfield_doline_l (lineno);
	    else
		pfield_doline_h (lineno);
	    pfield_adjust_delay ();
	} else {
	    if (currprefs.gfx_lores)
		pfield_doline_l (lineno);
	    else
		pfield_doline_h (lineno);
	}

	/* Check color0 adjust only if we have color changes - shouldn't happen
	 * otherwise. */
	adjust_drawing_colors (dp_for_drawing->ctable, bplham || bplehb);

	/* The problem is that we must call decode_ham6() BEFORE we do the
	 * sprites. */
	if (bplham) {
	    init_ham_decoding(linetoscr_x_adjust);
	    if (dip_for_drawing->nr_color_changes == 0) {
		/* The easy case: need to do HAM decoding only once for the
		 * full line. */
		decode_ham6 (linetoscr_x_adjust, linetoscr_right_x);
	    } else /* Argh. */ {
		adjust_color0_for_color_change ();
		do_color_changes (decode_ham6);
		adjust_drawing_colors (dp_for_drawing->ctable, bplham || bplehb);
	    }
	}

	if (dip_for_drawing->nr_sprites != 0) {
	    int spr;
	    walk_sprites (curr_sprite_positions + dip_for_drawing->first_sprite_draw, dip_for_drawing->nr_sprites);
	}
	if (dip_for_drawing->nr_color_changes == 0) {
	    pfield_do_linetoscr_full (gfxvidinfo.linemem == NULL ? do_double : 0);
	    do_flush_line (gfx_ypos);
	    linestate[lineno] = LINE_DONE;

	    if (do_double) {
		linestate[lineno + 1] = LINE_DONE_AS_PREVIOUS;
		do_flush_line (follow_ypos);
	    }
	} else {
	    int lastpos = 0, nextpos, i;

	    adjust_color0_for_color_change ();
	    do_color_changes (pfield_do_linetoscr);

	    linestate[lineno] = LINE_DONE;
	    do_flush_line (gfx_ypos);
	    if (do_double) {
		if (gfxvidinfo.linemem == NULL)
		    memcpy (row_map[follow_ypos], row_map[gfx_ypos], gfxvidinfo.rowbytes);
		linestate[lineno + 1] = LINE_DONE_AS_PREVIOUS;
		line_decisions[lineno + 1].which = -2;
		do_flush_line (follow_ypos);
	    }
	}
#ifdef LORES_HACK
	if (currprefs.gfx_lores == 2)
	    currprefs.gfx_lores = 0;
#endif
    } else {
	/* Border. */
	int i, lastpos = 0, nextpos;
	struct color_change *cc;

	xlinebuffer = gfxvidinfo.linemem;
	if (xlinebuffer == NULL)
	    xlinebuffer = row_map[gfx_ypos];
	xlinebuffer -= linetoscr_x_adjust_bytes;

	adjust_drawing_colors (dp_for_drawing->ctable, 0);
	/* Check color0 adjust only if we have color changes - shouldn't happen
	 * otherwise. */

	if (dip_for_drawing->nr_color_changes == 0) {
	    fill_line ();
	    do_flush_line (gfx_ypos);
	    linestate[lineno] = LINE_DONE;
	    if (do_double) {
		if (gfxvidinfo.linemem == NULL) {
		    xlinebuffer = row_map[follow_ypos] - linetoscr_x_adjust_bytes;
		    fill_line ();
		}
		do_flush_line (follow_ypos);
		linestate[lineno+1] = LINE_DONE_AS_PREVIOUS;
	    }
	    return;
	}

	adjust_color0_for_color_change ();
	do_color_changes (pfield_do_fill_line);

	do_flush_line (gfx_ypos);
	linestate[lineno] = LINE_DONE;
	if (do_double) {
	    if (gfxvidinfo.linemem == NULL)
		memcpy (row_map[follow_ypos], row_map[gfx_ypos], gfxvidinfo.rowbytes);
	    linestate[lineno + 1] = LINE_DONE_AS_PREVIOUS;
	    line_decisions[lineno + 1].which = -2;
	    do_flush_line (follow_ypos);
	}
    }
}

#ifdef SUPPORT_PENGUINS
static smp_comm_pipe drawing_pipe;
static uae_sem_t drawing_lock;

static void *drawing_penguin (void *cruxmedo)
{
    int l;
    fprintf(stderr, "Hello, world!\n");

    for (;;) {
	/* Start of a frame. */
	int k;
	new_frame:
	k = read_comm_pipe_int_blocking (&drawing_pipe);

	switch (k) {
	 case -2:
	    /* Hoopy */
	    break;

	 default:
	    write_log ("Penguin got out of sync.\n");
	    /* what can we do? Try to reduce the damage */
	    for (;;) {
		k = read_comm_pipe_int_blocking (&drawing_pipe);
		if (k == -3)
		    break;
		if (k == -1) {
		    uae_sem_post (&drawing_lock);
		    goto new_frame;
		}
	    }
	 case -3:
	    UAE_PENGUIN_EXIT;
	    /* Can't happen */
	    return 0;
	}

	for (;;) {
	    int i, where;
	    int l = read_comm_pipe_int_blocking (&drawing_pipe);
	    switch (l) {
	     case -1:
		/* End-of-frame synchronization. */
		uae_sem_post (&drawing_lock);
		goto new_frame;
	     case -2:
		/* customreset called a bit too often. That's harmless. */
		continue;
	     case -3:
		write_log ("Penguin got out of sync.\n");
		UAE_PENGUIN_EXIT;
		return 0;
	    }

	    /* l is the line that has been finished for drawing. */
	    i = l - thisframe_y_adjust_real;
	    if (i < 0 || i >= max_ypos_thisframe)
		continue;

	    if (linestate[l] == LINE_UNDECIDED) {
		fprintf (stderr, "Line scheduled for drawing, but undecided %d!?\n", l);
		continue;
	    }
	    if (inhibit_frame != 0)
		continue;
	    where = amiga2aspect_line_map[i+min_ypos_for_screen];
	    if (where >= gfxvidinfo.height || where == -1)
		continue;

	    pfield_draw_line (l, where, amiga2aspect_line_map[i+min_ypos_for_screen+1]);
	}
    }
}

static penguin_id our_penguin;

static void kill_drawing_penguin (void)
{
    /* ??? does libpthread do that for us? */
}

#endif

static int penguins_enabled_thisframe;

static void adjust_array_sizes (void)
{
#ifdef OS_WITHOUT_MEMORY_MANAGEMENT
    if (delta_sprite_draw) {
	void *p1,*p2;
	int mcc = max_sprite_draw + 200 + delta_sprite_draw;
	delta_sprite_draw = 0;
	p1 = realloc (sprite_positions[0], mcc * sizeof (struct sprite_draw));
	p2 = realloc (sprite_positions[1], mcc * sizeof (struct sprite_draw));
	if (p1) sprite_positions[0] = p1;
	if (p2) sprite_positions[1] = p2;
	if (p1 && p2) {
	    fprintf (stderr, "new max_sprite_draw=%d\n",mcc);
	    max_sprite_draw = mcc;
	}
    }
    if (delta_color_change) {
	void *p1,*p2;
	int mcc = max_color_change + 200 + delta_color_change;
	delta_color_change = 0;
	p1 = realloc (color_changes[0], mcc * sizeof (struct color_change));
	p2 = realloc (color_changes[1], mcc * sizeof (struct color_change));
	if (p1) color_changes[0] = p1;
	if (p2) color_changes[1] = p2;
	if (p1 && p2) {
	    fprintf (stderr, "new max_color_change=%d\n",mcc);
	    max_color_change = mcc;
	}
    }
    if (delta_delay_change) {
	void *p;
	int mcc = max_delay_change + 200 + delta_delay_change;
	delta_delay_change = 0;
	p = realloc (delay_changes, mcc * sizeof (struct delay_change));
	if (p) {
	    fprintf (stderr, "new max_delay_change=%d\n",mcc);
	    delay_changes = p;
	    max_delay_change = mcc;
	}
    }
#endif
}

static void init_drawing_frame (void)
{
    int i, maxline;

    adjust_array_sizes ();

    check_prefs_changed_custom ();
    check_prefs_changed_cpu ();
    
    if (max_diwstop == 0)
	max_diwstop = diwlastword;
    if (min_diwstart > max_diwstop)
	min_diwstart = 0;

    if (thisframe_first_drawn_line == -1)
	thisframe_first_drawn_line = minfirstline;
    if (thisframe_first_drawn_line > thisframe_last_drawn_line)
	thisframe_last_drawn_line = thisframe_first_drawn_line;

    next_color_change = 0;
    next_delay_change = 0;
    next_sprite_draw = 0;
    maxline = currprefs.gfx_linedbl ? (maxvpos+1) * 2 + 1 : (maxvpos+1) + 1;
#ifdef SMART_UPDATE
    for (i = 0; i < maxline; i++)
	linestate[i] = linestate[i] == LINE_DONE_AS_PREVIOUS ? LINE_REMEMBERED_AS_PREVIOUS : LINE_UNDECIDED;
#else
    memset (linestate, LINE_UNDECIDED, maxline);
#endif
    last_drawn_line = 0;
    first_drawn_line = 32767;

    first_block_line = last_block_line = -2;
    if (currprefs.test_drawing_speed)
	frame_redraw_necessary = 1;
    else if (frame_redraw_necessary)
	frame_redraw_necessary--;

    next_color_entry = 0;
    remembered_color_entry = -1;
    prev_sprite_positions = sprite_positions[current_change_set];
    curr_sprite_positions = sprite_positions[current_change_set ^ 1];
    prev_color_changes = color_changes[current_change_set];
    curr_color_changes = color_changes[current_change_set ^ 1];
    prev_color_tables = color_tables[current_change_set];
    curr_color_tables = color_tables[current_change_set ^ 1];

    prev_drawinfo = line_drawinfo[current_change_set];
    curr_drawinfo = line_drawinfo[current_change_set ^= 1];
    drawing_color_matches = -1;
    color_src_match = color_dest_match = -1;

    prev_x_adjust = linetoscr_x_adjust;
    prev_y_adjust = thisframe_y_adjust;

    if (currprefs.gfx_xcenter) {
	if (max_diwstop - min_diwstart < gfxvidinfo.width && currprefs.gfx_xcenter == 2)
	    /* Try to center. */
	    linetoscr_x_adjust = ((max_diwstop - min_diwstart - gfxvidinfo.width) / 2 + min_diwstart) & ~1;
	else
	    linetoscr_x_adjust = max_diwstop - gfxvidinfo.width;

	/* Would the old value be good enough? If so, leave it as it is if we want to
	 * be clever. */
	if (currprefs.gfx_xcenter == 2) {
	    if (linetoscr_x_adjust < prev_x_adjust && prev_x_adjust < min_diwstart)
		linetoscr_x_adjust = prev_x_adjust;
	}
    } else
	linetoscr_x_adjust = max_diwlastword - gfxvidinfo.width;
    if (linetoscr_x_adjust < 0)
	linetoscr_x_adjust = 0;

    linetoscr_x_adjust_bytes = linetoscr_x_adjust * gfxvidinfo.pixbytes;

    linetoscr_right_x = linetoscr_x_adjust + gfxvidinfo.width;
    if (linetoscr_right_x > max_diwlastword)
	linetoscr_right_x = max_diwlastword;

    thisframe_y_adjust = minfirstline;
    if (currprefs.gfx_ycenter && thisframe_first_drawn_line != -1) {
	if (thisframe_last_drawn_line - thisframe_first_drawn_line < max_drawn_amiga_line && currprefs.gfx_ycenter == 2)
	    thisframe_y_adjust = (thisframe_last_drawn_line - thisframe_first_drawn_line - max_drawn_amiga_line) / 2 + thisframe_first_drawn_line;
	else
	    thisframe_y_adjust = thisframe_first_drawn_line;
	/* Would the old value be good enough? If so, leave it as it is if we want to
	 * be clever. */
	if (currprefs.gfx_ycenter == 2) {
	    if (thisframe_y_adjust != prev_y_adjust
		&& prev_y_adjust <= thisframe_first_drawn_line
		&& prev_y_adjust + max_drawn_amiga_line > thisframe_last_drawn_line)
		thisframe_y_adjust = prev_y_adjust;
	}
	/* Make sure the value makes sense */
	if (thisframe_y_adjust + max_drawn_amiga_line > maxvpos)
	    thisframe_y_adjust = maxvpos - max_drawn_amiga_line;
	if (thisframe_y_adjust < minfirstline)
	    thisframe_y_adjust = minfirstline;
    }
    thisframe_y_adjust_real = thisframe_y_adjust << (currprefs.gfx_linedbl ? 1 : 0);
    max_ypos_thisframe = (maxvpos - thisframe_y_adjust) << (currprefs.gfx_linedbl ? 1 : 0);

    if (prev_x_adjust != linetoscr_x_adjust || prev_y_adjust != thisframe_y_adjust)
	frame_redraw_necessary |= (bplcon0 & 4) && currprefs.gfx_linedbl ? 2 : 1;

    max_diwstop = 0;
    min_diwstart = 10000;
    thisframe_first_drawn_line = -1;
    thisframe_last_drawn_line = -1;
#ifdef SUPPORT_PENGUINS
    penguins_enabled_thisframe = 1;
    /* Tell the other thread that it can now expect data from us. */
    write_comm_pipe_int (&drawing_pipe, -2, 1);
    memset (line_drawn, 0, sizeof line_drawn);
#endif
}

static void finish_drawing_frame (void)
{
    int i;

#ifdef SUPPORT_PENGUINS
    /* Synchronize with other thread, then see whether there's something left for
     * us to draw. @@@ This is probably a big waste of cycles if the two threads
     * run at very different speeds - this one could draw stuff as well. */
    write_comm_pipe_int (&drawing_pipe, -1, 1);
    uae_sem_wait (&drawing_lock);
#else

#ifndef SMART_UPDATE
    /* @@@ This isn't exactly right yet. FIXME */
    if (!interlace_seen) {
	do_flush_screen (first_drawn_line, last_drawn_line);
	return;
    }
#endif
    for (i = 0; i < max_ypos_thisframe; i++) {
	int where;
	int line = i + thisframe_y_adjust_real;

	if (linestate[line] == LINE_UNDECIDED)
	    break;

	where = amiga2aspect_line_map[i+min_ypos_for_screen];
	if (where >= gfxvidinfo.height)
	    break;
	if (where == -1)
	    continue;

	pfield_draw_line (line, where, amiga2aspect_line_map[i+min_ypos_for_screen+1]);
    }
#endif
    do_flush_screen (first_drawn_line, last_drawn_line);
}

static __inline__ void check_picasso (void)
{
#ifdef PICASSO96
    if (picasso_requested_on == picasso_on)
	return;

    picasso_on = picasso_requested_on;

    if (!picasso_on)
	clear_inhibit_frame (2);
    else
	set_inhibit_frame (2);

    gfx_set_picasso_state (picasso_on);
    picasso_enablescreen (picasso_requested_on);

    notice_screen_contents_lost ();
#endif
}

static void vsync_handler (void)
{
#ifdef FRAME_RATE_HACK /* put this here or at the end of the function? */
    {
	frame_time_t curr_time = read_processor_time();
	vsyncmintime += vsynctime;
	/* @@@ Mathias? How do you think we should do this? */
	/* If we are too far behind, or we just did a reset, adjust the
	 * needed time. */
	if ((long int)(curr_time - vsyncmintime) > 0 || did_reset)
	    vsyncmintime = curr_time + vsynctime;
	did_reset = 0;
    }
#endif
    handle_events ();

    getjoystate (0, &joy0dir, &joy0button);
    getjoystate (1, &joy1dir, &joy1button);

    INTREQ(0x8020);
    if (bplcon0 & 4)
	lof ^= 0x8000;

    if (quit_program < 0) {
	quit_program = -quit_program;
	set_inhibit_frame (1);
	regs.spcflags |= SPCFLAG_BRK;
	if (framecnt == 0)
	    finish_drawing_frame ();
	filesys_prepare_reset ();
#ifdef SUPPORT_PENGUINS
	if (quit_program == 1)
	    /* Stop eating herring */
	    write_comm_pipe_int (&drawing_pipe, -3, 1);
#endif
	return;
    }

    last_redraw_point++;
    if (lof_changed || !interlace_seen || last_redraw_point >= 2 || lof) {
	static int cnt = 0;

	if (framecnt == 0)
	    finish_drawing_frame ();
	count_frame ();
	last_redraw_point = 0;

	check_picasso ();

	uae_sem_wait (&ihf_sem);
	if (inhibit_frame != 0)
	    framecnt = 1;
	if (frame_do_semup)
	    uae_sem_post (&frame_sem);
	frame_do_semup = 0;
	uae_sem_post (&ihf_sem);

	if (cnt == 0) {
	    /* resolution_check_change (); */
	    DISK_check_change ();
	    cnt = 5;
	}
	cnt--;
	if (framecnt == 0)
	    init_drawing_frame ();
    }

    lof_changed = 0;
    interlace_seen = 0;
    COPJMP1(0);

    init_hardware_frame();
#ifdef HAVE_GETTIMEOFDAY
    {
	struct timeval tv;
	unsigned long int newtime;

	gettimeofday(&tv,NULL);
	newtime = (tv.tv_sec-seconds_base) * 1000 + tv.tv_usec / 1000;

	if (!bogusframe) {
	    frametime += newtime - msecs;
	    timeframes++;
	}
	msecs = newtime;
	bogusframe = 0;
    }
#endif
    if (ievent_alive > 0)
	ievent_alive--;
    CIA_vsync_handler();
}

static void hsync_handler(void)
{
    int lineno = next_lineno;
    int lineisdouble = 0;
    int line_was_doubled = 0;

    finish_decisions ();
    do_modulos ();

    if (framecnt == 0) {
	switch (nln_how) {
	 case 0:
	    linestate[lineno] = LINE_DECIDED;
	    break;
	 case 1:
	    linestate[lineno] = LINE_DECIDED_DOUBLE;
	    if (linestate[lineno+1] != LINE_REMEMBERED_AS_PREVIOUS)
		linestate[lineno+1] = LINE_AS_PREVIOUS;
	    break;
	 case 2:
	    if (linestate[lineno-1] == LINE_UNDECIDED)
		linestate[lineno-1] = LINE_BORDER_NEXT;
	    linestate[lineno] = LINE_DECIDED;
	    break;
	 case 3:
	    linestate[lineno] = LINE_DECIDED;
	    if (linestate[lineno+1] == LINE_UNDECIDED
		|| linestate[lineno+1] == LINE_REMEMBERED_AS_PREVIOUS
		|| linestate[lineno+1] == LINE_AS_PREVIOUS)
		linestate[lineno+1] = LINE_BORDER_PREV;
	    break;
	}
    }

    eventtab[ev_hsync].evtime += cycles - eventtab[ev_hsync].oldcycles;
    eventtab[ev_hsync].oldcycles = cycles;
    CIA_hsync_handler();

    if (currprefs.produce_sound > 0) {
	int nr;
	/* Sound data is fetched at the beginning of each line */
	for (nr = 0; nr < 4; nr++) {
	    struct audio_channel_data *cdp = audio_channel + nr;

	    if (cdp->data_written == 2) {
		cdp->data_written = 0;
		cdp->nextdat = chipmem_bank.wget(cdp->pt);
		cdp->pt += 2;
		if (cdp->state == 2 || cdp->state == 3) {
		    if (cdp->wlen == 1) {
			cdp->pt = cdp->lc;
			cdp->wlen = cdp->len;
			cdp->intreq2 = 1;
		    } else
			cdp->wlen--;
		}
	    }
	}
    }
#ifdef SUPPORT_PENGUINS
    if (framecnt == 0 && penguins_enabled_thisframe) {
	write_comm_pipe_int (&drawing_pipe, next_lineno, 0);
    }
#endif
#ifndef SMART_UPDATE
    {
	int i, where;
	/* l is the line that has been finished for drawing. */
	i = next_lineno - thisframe_y_adjust_real;
	if (i >= 0 && i < max_ypos_thisframe) {
	    where = amiga2aspect_line_map[i+min_ypos_for_screen];
	    if (where < gfxvidinfo.height && where != -1)
		pfield_draw_line (next_lineno, where, amiga2aspect_line_map[i+min_ypos_for_screen+1]);
	}
    }
#endif
    if (++vpos == (maxvpos + (lof != 0))) {
	vpos = 0;
	vsync_handler();
    }

    is_lastline = vpos + 1 == maxvpos + (lof != 0);

    if ((bplcon0 & 4) && currprefs.gfx_linedbl) {
	interlace_seen = 1, penguins_enabled_thisframe = 0;
    }

    if (framecnt == 0) {
	lineno = vpos;
	nln_how = 0;
	if (currprefs.gfx_linedbl) {
	    lineno *= 2;
	    nln_how = 1;
	    if (bplcon0 & 4) {
		if (!lof) {
		    lineno++;
		    nln_how = 2;
		} else {
		    nln_how = 3;
		}
	    }
	}
	next_lineno = lineno;
	reset_decisions ();
    }
    if (uae_int_requested) {
	set_uae_int_flag ();
	INTREQ (0xA000);
    }
}

static void init_eventtab (void)
{
    int i;

    for(i = 0; i < ev_max; i++) {
	eventtab[i].active = 0;
	eventtab[i].oldcycles = 0;
    }
    copper_active = 0;
    eventtab[ev_cia].handler = CIA_handler;
    eventtab[ev_copper].handler = /* currprefs.copper_pos >= 0 ? do_copper_cheat : */ do_copper;
    eventtab[ev_hsync].handler = hsync_handler;
    eventtab[ev_hsync].evtime = maxhpos + cycles;
    eventtab[ev_hsync].active = 1;

    eventtab[ev_blitter].handler = blitter_handler;
    eventtab[ev_blitter].active = 0;
    eventtab[ev_diskblk].handler = diskblk_handler;
    eventtab[ev_diskblk].active = 0;
    eventtab[ev_diskindex].handler = diskindex_handler;
    eventtab[ev_diskindex].active = 0;
#ifndef DONT_WANT_SOUND
    eventtab[ev_aud0].handler = aud0_handler;
    eventtab[ev_aud0].active = 0;
    eventtab[ev_aud1].handler = aud1_handler;
    eventtab[ev_aud1].active = 0;
    eventtab[ev_aud2].handler = aud2_handler;
    eventtab[ev_aud2].active = 0;
    eventtab[ev_aud3].handler = aud3_handler;
    eventtab[ev_aud3].active = 0;
    if (sound_available && currprefs.produce_sound >= 2) {
	eventtab[ev_sample].active = 1;
	eventtab[ev_sample].evtime = sample_evtime;
    } else {
	eventtab[ev_sample].active = 0;
    }
#endif
    events_schedule ();
}

void customreset (void)
{
    int i, maxl;
    double native_lines_per_amiga_line;
#ifdef HAVE_GETTIMEOFDAY
    struct timeval tv;
#endif

    for (i = 0; i < sizeof current_colors.color_regs / sizeof *current_colors.color_regs; i++)
	current_colors.color_regs[i] = -1;

#ifdef FRAME_RATE_HACK
    did_reset = 1;
    vsyncmintime = read_processor_time() + vsynctime;
#endif
    inhibit_frame = 0;
    frame_do_semup = 0;
    uae_sem_init (&frame_sem, 0, 0);
    uae_sem_init (&gui_sem, 0, 1);
    uae_sem_init (&ihf_sem, 0, 1);
    expamem_reset ();
    filesys_reset ();
    CIA_reset ();
#ifdef PICASSO96
    InitPicasso96 ();
    picasso_on = 0;
    picasso_requested_on = 0;
    gfx_set_picasso_state (0);
#endif
    cycles = 0;
    regs.spcflags &= SPCFLAG_BRK;

    lores_factor = currprefs.gfx_lores ? 1 : 2;
    lores_shift = currprefs.gfx_lores ? 0 : 1;
    sprite_width = currprefs.gfx_lores ? 16 : 32;

    vpos = 0;
    is_lastline = 0;
    lof = 0;
    max_diwstop = 0;

    if (needmousehack()) {
#if 0
	if (mousestate != follow_mouse) setfollow();
#else
	if (mousestate != dont_care_mouse) setdontcare();
#endif
    } else {
	mousestate = normal_mouse;
    }
    ievent_alive = 0;

    clx_sprmask = 0xFF;
    clxdat = 0;

    memset(sprarmed, 0, sizeof sprarmed);
    nr_armed = 0;

    /*memset(blitcount, 0, sizeof(blitcount));  blitter debug */
    for (i = 0; i < (maxvpos+1)*2 + 1; i++) {
	linestate[i] = LINE_UNDECIDED;
    }
    xlinebuffer = gfxvidinfo.bufmem;

    dmacon = intena = 0;
    bltstate = BLT_done;
    copstate = COP_stop;
    diwstate = DIW_waiting_start;
    hdiwstate = DIW_waiting_start;
    copcon = 0;
    dskdmaen = 0;
    cycles = 0;

    audio_reset ();

    bplcon4 = 0x11; /* Get AGA chipset into ECS compatibility mode */
    bplcon3 = 0xC00;

    init_eventtab ();

    if (native2amiga_line_map)
	free (native2amiga_line_map);
    if (amiga2aspect_line_map)
	free (amiga2aspect_line_map);

    /* At least for this array the +1 is necessary. */
    amiga2aspect_line_map = (int *)malloc (sizeof (int) * (maxvpos+1)*2 + 1);
    native2amiga_line_map = (int *)malloc (sizeof (int) * gfxvidinfo.height);

    if (currprefs.gfx_correct_aspect)
	native_lines_per_amiga_line = ((double)gfxvidinfo.height
				       * (currprefs.gfx_lores ? 320 : 640)
				       / (currprefs.gfx_linedbl ? 512 : 256)
				       / gfxvidinfo.width);
    else
	native_lines_per_amiga_line = 1;

    maxl = (maxvpos+1) * (currprefs.gfx_linedbl ? 2 : 1);
    min_ypos_for_screen = minfirstline << (currprefs.gfx_linedbl ? 1 : 0);
    max_drawn_amiga_line = -1;
    for (i = 0; i < maxl; i++) {
	int v = (i - min_ypos_for_screen) * native_lines_per_amiga_line;
	if (v >= gfxvidinfo.height && max_drawn_amiga_line == -1)
	    max_drawn_amiga_line = i-min_ypos_for_screen;
	if (i < min_ypos_for_screen || v >= gfxvidinfo.height)
	    v = -1;
	amiga2aspect_line_map[i] = v;
    }
    if (currprefs.gfx_linedbl)
	max_drawn_amiga_line >>= 1;

    if (currprefs.gfx_ycenter && !(currprefs.gfx_correct_aspect)) {
	extra_y_adjust = (gfxvidinfo.height - (maxvpos << (currprefs.gfx_linedbl ? 1 : 0))) >> 1;
	if (extra_y_adjust < 0)
	    extra_y_adjust = 0;
    }

    for (i = 0; i < gfxvidinfo.height; i++)
	native2amiga_line_map[i] = -1;

    if (native_lines_per_amiga_line < 1) {
	/* Must omit drawing some lines. */
	for (i = maxl-1; i > min_ypos_for_screen; i--) {
	    if (amiga2aspect_line_map[i] == amiga2aspect_line_map[i-1]) {
		if (currprefs.gfx_linedbl && (i & 1) == 0 && amiga2aspect_line_map[i+1] != -1) {
		    /* If only the first line of a line pair would be omitted,
		     * omit the second one instead to avoid problems with line
		     * doubling. */
		    amiga2aspect_line_map[i] = amiga2aspect_line_map[i+1];
		    amiga2aspect_line_map[i+1] = -1;
		} else
		    amiga2aspect_line_map[i] = -1;
	    }
	}
    }

    for (i = maxl-1; i >= min_ypos_for_screen; i--) {
	int j;
	if (amiga2aspect_line_map[i] == -1)
	    continue;
	for (j = amiga2aspect_line_map[i]; j < gfxvidinfo.height && native2amiga_line_map[j] == -1; j++)
	    native2amiga_line_map[j] = i >> (currprefs.gfx_linedbl ? 1 : 0);
    }

    if (line_drawn == 0)
	line_drawn = (char *)malloc (gfxvidinfo.height);
    init_row_map();

    init_sprites ();

    init_hardware_frame ();
    init_drawing_frame ();
    last_redraw_point = 0;
    reset_decisions ();

#ifdef HAVE_GETTIMEOFDAY
    gettimeofday(&tv,NULL);
    seconds_base = tv.tv_sec;
    bogusframe = 1;
#endif
}

void dumpcustom (void)
{
    int i;
    fprintf(stderr, "DMACON: %x INTENA: %x INTREQ: %x VPOS: %x HPOS: %x\n", DMACONR(),
	   intena, intreq, vpos, current_hpos());
    if (timeframes) {
	fprintf(stderr, "Average frame time: %d ms [frames: %d time: %d]\n",
	       frametime/timeframes, timeframes, frametime);
    }
    dump_audio_bench ();
    /*for (i=0; i<256; i++) if (blitcount[i]) fprintf(stderr, "minterm %x = %d\n",i,blitcount[i]);  blitter debug */
}

int intlev (void)
{
    uae_u16 imask = intreq & intena;
    if (imask && (intena & 0x4000)){
	if (imask & 0x2000) return 6;
	if (imask & 0x1800) return 5;
	if (imask & 0x0780) return 4;
	if (imask & 0x0070) return 3;
	if (imask & 0x0008) return 2;
	if (imask & 0x0007) return 1;
    }
    return -1;
}

void custom_init (void)
{
    int num;

#ifdef OS_WITHOUT_MEMORY_MANAGEMENT
    for(num=0;num<2;++num) {
       sprite_positions[num] = xmalloc(max_sprite_draw * sizeof(struct sprite_draw));
       color_changes[num] = xmalloc(max_color_change * sizeof(struct color_change));
    }
    delay_changes = xmalloc(max_delay_change * sizeof(struct delay_change));
#endif

    if (needmousehack())
	setfollow();
    init_regchanges ();
    init_decisions ();

    /* For now, the AGA stuff is broken in the dual playfield case. We encode
     * sprites in dpf mode by ORing the pixel value with 0x80. To make dual
     * playfield rendering easy, the lookup tables contain are made linear for
     * values >= 128. That only works for OCS/ECS, though. */

    for (num = 0; num < 256; num++) {
	int plane1 = (num & 1) | ((num >> 1) & 2) | ((num >> 2) & 4) | ((num >> 3) & 8);
	int plane2 = ((num >> 1) & 1) | ((num >> 2) & 2) | ((num >> 3) & 4) | ((num >> 4) & 8);
	dblpf_2nd1[num] = plane1 == 0 ? (plane2 == 0 ? 0 : 2) : 1;
	dblpf_2nd2[num] = plane2 == 0 ? (plane1 == 0 ? 0 : 1) : 2;
	if (plane2 > 0) plane2 += 8;
	dblpf_ind1[num] = num >= 128 ? num & 0x7F : (plane1 == 0 ? plane2 : plane1);
	dblpf_ind2[num] = num >= 128 ? num & 0x7F : (plane2 == 0 ? plane1 : plane2);

	lots_of_twos[num] = num == 0 ? 0 : 2;
	linear_map_256[num] = num;
    }
    build_blitfilltable();
    gen_pfield_tables();
    native2amiga_line_map = 0;
    amiga2aspect_line_map = 0;
    line_drawn = 0;

#ifdef SUPPORT_PENGUINS
    init_comm_pipe (&drawing_pipe, 800, 5);
    uae_sem_init (&drawing_lock, 0, 0);
    start_penguin (drawing_penguin, 0, &our_penguin);
    /*atexit(kill_drawing_penguin);*/
#endif
}

/* Custom chip memory bank */

static uae_u32 custom_lget(uaecptr) REGPARAM;
static uae_u32 custom_wget(uaecptr) REGPARAM;
static uae_u32 custom_bget(uaecptr) REGPARAM;
static void  custom_lput(uaecptr, uae_u32) REGPARAM;
static void  custom_wput(uaecptr, uae_u32) REGPARAM;
static void  custom_bput(uaecptr, uae_u32) REGPARAM;

addrbank custom_bank = {
    custom_lget, custom_wget, custom_bget,
    custom_lput, custom_wput, custom_bput,
    default_xlate, default_check
};

uae_u32 REGPARAM2 custom_wget (uaecptr addr)
{
    switch(addr & 0x1FE) {
     case 0x002: return DMACONR();
     case 0x004: return VPOSR();
     case 0x006: return VHPOSR();

     case 0x008: return DSKDATR();

     case 0x00A: return JOY0DAT();
     case 0x00C: return JOY1DAT();
     case 0x00E: return CLXDAT();
     case 0x010: return ADKCONR();

     case 0x012: return POT0DAT();
     case 0x016: return POTGOR();
     case 0x018: return SERDATR();
     case 0x01A: return DSKBYTR();
     case 0x01C: return INTENAR();
     case 0x01E: return INTREQR();
#if AGA_CHIPSET == 1
     case 0x07C: return 0xF8;
#elif defined ECS_DENISE
     case 0x07C: return 0xFC;
#endif
     default:
	custom_wput(addr,0);
	return 0xffff;
    }
}

uae_u32 REGPARAM2 custom_bget (uaecptr addr)
{
    return custom_wget(addr & 0xfffe) >> (addr & 1 ? 0 : 8);
}

uae_u32 REGPARAM2 custom_lget (uaecptr addr)
{
    return ((uae_u32)custom_wget(addr & 0xfffe) << 16) | custom_wget((addr+2) & 0xfffe);
}

void REGPARAM2 custom_wput (uaecptr addr, uae_u32 value)
{
    addr &= 0x1FE;
    cregs[addr>>1] = value;
    switch (addr) {
     case 0x020: DSKPTH (value); break;
     case 0x022: DSKPTL (value); break;
     case 0x024: DSKLEN (value); break;
     case 0x026: DSKDAT (value); break;

     case 0x02A: VPOSW (value); break;
     case 0x2E:  COPCON (value); break;
     case 0x030: SERDAT (value); break;
     case 0x032: SERPER (value); break;
     case 0x34: POTGO (value); break;
     case 0x040: BLTCON0 (value); break;
     case 0x042: BLTCON1 (value); break;

     case 0x044: BLTAFWM (value); break;
     case 0x046: BLTALWM (value); break;

     case 0x050: BLTAPTH (value); break;
     case 0x052: BLTAPTL (value); break;
     case 0x04C: BLTBPTH (value); break;
     case 0x04E: BLTBPTL (value); break;
     case 0x048: BLTCPTH (value); break;
     case 0x04A: BLTCPTL (value); break;
     case 0x054: BLTDPTH (value); break;
     case 0x056: BLTDPTL (value); break;

     case 0x058: BLTSIZE (value); break;

     case 0x064: BLTAMOD (value); break;
     case 0x062: BLTBMOD (value); break;
     case 0x060: BLTCMOD (value); break;
     case 0x066: BLTDMOD (value); break;

     case 0x070: BLTCDAT (value); break;
     case 0x072: BLTBDAT (value); break;
     case 0x074: BLTADAT (value); break;

     case 0x07E: DSKSYNC (value); break;

     case 0x080: COP1LCH (value); break;
     case 0x082: COP1LCL (value); break;
     case 0x084: COP2LCH (value); break;
     case 0x086: COP2LCL (value); break;

     case 0x088: COPJMP1 (value); break;
     case 0x08A: COPJMP2 (value); break;

     case 0x08E: DIWSTRT (value); break;
     case 0x090: DIWSTOP (value); break;
     case 0x092: DDFSTRT (value); break;
     case 0x094: DDFSTOP (value); break;

     case 0x096: DMACON (value); break;
     case 0x098: CLXCON (value); break;
     case 0x09A: INTENA (value); break;
     case 0x09C: INTREQ (value); break;
     case 0x09E: ADKCON (value); break;

     case 0x0A0: AUDxLCH (0, value); break;
     case 0x0A2: AUDxLCL (0, value); break;
     case 0x0A4: AUDxLEN (0, value); break;
     case 0x0A6: AUDxPER (0, value); break;
     case 0x0A8: AUDxVOL (0, value); break;
     case 0x0AA: AUDxDAT (0, value); break;

     case 0x0B0: AUDxLCH (1, value); break;
     case 0x0B2: AUDxLCL (1, value); break;
     case 0x0B4: AUDxLEN (1, value); break;
     case 0x0B6: AUDxPER (1, value); break;
     case 0x0B8: AUDxVOL (1, value); break;
     case 0x0BA: AUDxDAT (1, value); break;

     case 0x0C0: AUDxLCH (2, value); break;
     case 0x0C2: AUDxLCL (2, value); break;
     case 0x0C4: AUDxLEN (2, value); break;
     case 0x0C6: AUDxPER (2, value); break;
     case 0x0C8: AUDxVOL (2, value); break;
     case 0x0CA: AUDxDAT (2, value); break;

     case 0x0D0: AUDxLCH (3, value); break;
     case 0x0D2: AUDxLCL (3, value); break;
     case 0x0D4: AUDxLEN (3, value); break;
     case 0x0D6: AUDxPER (3, value); break;
     case 0x0D8: AUDxVOL (3, value); break;
     case 0x0DA: AUDxDAT (3, value); break;

     case 0x0E0: BPLPTH (value, 0); break;
     case 0x0E2: BPLPTL (value, 0); break;
     case 0x0E4: BPLPTH (value, 1); break;
     case 0x0E6: BPLPTL (value, 1); break;
     case 0x0E8: BPLPTH (value, 2); break;
     case 0x0EA: BPLPTL (value, 2); break;
     case 0x0EC: BPLPTH (value, 3); break;
     case 0x0EE: BPLPTL (value, 3); break;
     case 0x0F0: BPLPTH (value, 4); break;
     case 0x0F2: BPLPTL (value, 4); break;
     case 0x0F4: BPLPTH (value, 5); break;
     case 0x0F6: BPLPTL (value, 5); break;

     case 0x100: BPLCON0 (value); break;
     case 0x102: BPLCON1 (value); break;
     case 0x104: BPLCON2 (value); break;
     case 0x106: BPLCON3 (value); break;

     case 0x108: BPL1MOD (value); break;
     case 0x10A: BPL2MOD (value); break;

     case 0x110: BPL1DAT (value); break;
     case 0x112: BPL2DAT (value); break;
     case 0x114: BPL3DAT (value); break;
     case 0x116: BPL4DAT (value); break;
     case 0x118: BPL5DAT (value); break;
     case 0x11A: BPL6DAT (value); break;

     case 0x180: case 0x182: case 0x184: case 0x186: case 0x188: case 0x18A:
     case 0x18C: case 0x18E: case 0x190: case 0x192: case 0x194: case 0x196:
     case 0x198: case 0x19A: case 0x19C: case 0x19E: case 0x1A0: case 0x1A2:
     case 0x1A4: case 0x1A6: case 0x1A8: case 0x1AA: case 0x1AC: case 0x1AE:
     case 0x1B0: case 0x1B2: case 0x1B4: case 0x1B6: case 0x1B8: case 0x1BA:
     case 0x1BC: case 0x1BE:
	COLOR (value & 0xFFF, (addr & 0x3E) / 2);
	break;
     case 0x120: case 0x124: case 0x128: case 0x12C:
     case 0x130: case 0x134: case 0x138: case 0x13C:
	SPRxPTH (value, (addr - 0x120) / 4);
	break;
     case 0x122: case 0x126: case 0x12A: case 0x12E:
     case 0x132: case 0x136: case 0x13A: case 0x13E:
	SPRxPTL (value, (addr - 0x122) / 4);
	break;
     case 0x140: case 0x148: case 0x150: case 0x158:
     case 0x160: case 0x168: case 0x170: case 0x178:
	SPRxPOS (value, (addr - 0x140) / 8);
	break;
     case 0x142: case 0x14A: case 0x152: case 0x15A:
     case 0x162: case 0x16A: case 0x172: case 0x17A:
	SPRxCTL (value, (addr - 0x142) / 8);
	break;
     case 0x144: case 0x14C: case 0x154: case 0x15C:
     case 0x164: case 0x16C: case 0x174: case 0x17C:
	SPRxDATA (value, (addr - 0x144) / 8);
	break;
     case 0x146: case 0x14E: case 0x156: case 0x15E:
     case 0x166: case 0x16E: case 0x176: case 0x17E:
	SPRxDATB (value, (addr - 0x146) / 8);
	break;

     case 0x36: JOYTEST (value); break;
#if defined (ECS_AGNUS) || (AGA_CHIPSET == 1)
     case 0x5A: BLTCON0L(value); break;
     case 0x5C: BLTSIZV (value); break;
     case 0x5E: BLTSIZH (value); break;
#endif
#if AGA_CHIPSET == 1
     case 0x10C: BPLCON4 (value); break;
     case 0x1FC: fmode = value; break;
#endif
    }
}

void REGPARAM2 custom_bput(uaecptr addr, uae_u32 value)
{
    static int warned = 0;
    /* Is this correct now? (There are people who bput things to the upper byte of AUDxVOL). */
    uae_u16 rval = (value << 8) | (value & 0xFF);
    custom_wput(addr, rval);
    if (!warned)
	write_log ("Byte put to custom register.\n"), warned++;
}

void REGPARAM2 custom_lput(uaecptr addr, uae_u32 value)
{
    custom_wput(addr & 0xfffe, value >> 16);
    custom_wput((addr+2) & 0xfffe, (uae_u16)value);
}
