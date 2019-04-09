 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Interface to the graphics system (X, SVGAlib)
  *
  * Copyright 1995-1997 Bernd Schmidt
  */

typedef long int xcolnr;

typedef int (*allocfunc_type)(int, int, int, xcolnr *);

extern xcolnr xcolors[4096];

extern int buttonstate[3];
extern int newmousecounters;
extern int lastmx, lastmy;

extern int graphics_setup(void);
extern int graphics_init(void);
extern void graphics_leave(void);
extern void handle_events(void);
extern void setup_brkhandler(void);

extern void flush_line(int);
extern void flush_block(int, int);
extern void flush_screen(int, int);

extern int debuggable(void);
extern int needmousehack(void);
extern void togglemouse(void);
extern void LED(int);

extern unsigned long doMask(int p, int bits, int shift);
extern void setup_maxcol(int);
extern void alloc_colors256(int (*)(int, int, int, xcolnr *));
extern void alloc_colors64k(int, int, int, int, int, int);
extern void setup_greydither(int bits, allocfunc_type allocfunc);
extern void setup_greydither_maxcol(int maxcol, allocfunc_type allocfunc);
extern void setup_dither(int bits, allocfunc_type allocfunc);
extern void DitherLine(uae_u8 *l, uae_u16 *r4g4b4, int x, int y, uae_s16 len, int bits) ASM_SYM_FOR_FUNC("DitherLine");

struct vidbuf_description
{
    char *bufmem; /* Pointer to either the video memory or an area as large which is used as a buffer. */
    char *linemem; /* Pointer to a single line in memory to draw into. If NULL, use bufmem. */
    int rowbytes; /* Bytes per row in the memory pointed at by bufmem. */
    int pixbytes; /* Bytes per pixel. */
    int width;
    int height;
    int maxblocklines; /* Set to 0 if you want calls to flush_line after each drawn line, or the number of
			* lines that flush_block wants to/can handle (it isn't really useful to use another
			* value than maxline here). */
    int can_double; /* Set if the high part of each entry in xcolors contains the same value
		     * as the low part, so that two pixels can be drawn at once. */
};

extern struct vidbuf_description gfxvidinfo;

/* For ports using tui.c, this should be built by graphics_setup(). */
extern struct bstring *video_mode_menu;
extern void vidmode_menu_selected(int);

/* Some definitions that are useful for multithreaded setups. */

/* If frame_do_semup is nonzero, custom.c will do a sem_post on frame_sem
 * the next time it checks the contents of inhibit_frame.  That way, a
 * thread set a bit in inhibit_frame and wait until custom.c has stopped
 * drawing.
 * gui_sem is currently unused.
 * ihf_sem protects modifications of inhibit_frame.
 */
extern uae_sem_t frame_sem, gui_sem, ihf_sem;
extern volatile int inhibit_frame;
extern volatile int frame_do_semup;

static __inline__ void set_inhibit_frame (int bit)
{
    uae_sem_wait (&ihf_sem);
    inhibit_frame |= 1 << bit;
    uae_sem_post (&ihf_sem);
}
static __inline__ void clear_inhibit_frame (int bit)
{
    uae_sem_wait (&ihf_sem);
    inhibit_frame &= ~(1 << bit);
    uae_sem_post (&ihf_sem);
}
static __inline__ void toggle_inhibit_frame (int bit)
{
    uae_sem_wait (&ihf_sem);
    inhibit_frame ^= ~(1 << bit);
    uae_sem_post (&ihf_sem);
}

