 /*
  * UAE - The Un*x Amiga Emulator
  *
  * X interface
  *
  * Copyright 1995, 1996 Bernd Schmidt
  * Copyright 1996 Ed Hanway, Andre Beck, Samuel Devulder, Bruno Coste
  * DGA support by Kai Kollmorgen
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>

#include <ctype.h>
#include <signal.h>

#include "config.h"
#include "options.h"
#include "threaddep/penguin.h"
#include "uae.h"
#include "memory.h"
#include "custom.h"
#include "readcpu.h"
#include "newcpu.h"
#include "xwin.h"
#include "keyboard.h"
#include "keybuf.h"
#include "gui.h"
#include "debug.h"
#include "picasso96.h"

#ifdef __cplusplus
#define VI_CLASS c_class
#else
#define VI_CLASS class
#endif

#ifdef USE_DGA_EXTENSION

#ifdef USE_VIDMODE_EXTENSION
#include <X11/extensions/xf86vmode.h>
#define VidMode_MINMAJOR 0
#define VidMode_MINMINOR 0
#endif

#include <X11/extensions/xf86dga.h>
#define DGA_MINMAJOR 0
#define DGA_MINMINOR 0

#elif SHM_SUPPORT_LINKS == 1

#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/extensions/XShm.h>

#define DO_PUTIMAGE(IMG, SRCX, SRCY, DSTX, DSTY, WIDTH, HEIGHT) \
    do { \
	if (currprefs.use_mitshm) \
	     XShmPutImage (display, mywin, blackgc, IMG, SRCX, SRCY, DSTX, DSTY, WIDTH, HEIGHT, 0); \
	else \
	    XPutImage (display, mywin, blackgc, IMG, SRCX, SRCY, DSTX, DSTY, WIDTH, HEIGHT); \
    } while (0)
#else
#define DO_PUTIMAGE(IMG, SRCX, SRCY, DSTX, DSTY, WIDTH, HEIGHT) \
    XPutImage (display, mywin, blackgc, IMG, SRCX, SRCY, DSTX, DSTY, WIDTH, HEIGHT)
#endif

#ifdef __cplusplus
static RETSIGTYPE sigbrkhandler(...)
#else
static RETSIGTYPE sigbrkhandler(int foo)
#endif
{
    activate_debugger();

#if !defined(__unix) || defined(__NeXT__)
    signal(SIGINT, sigbrkhandler);
#endif
}

void setup_brkhandler(void)
{
#if defined(__unix) && !defined(__NeXT__)
    struct sigaction sa;
    sa.sa_handler = sigbrkhandler;
    sa.sa_flags = 0;
#ifdef SA_RESTART
    sa.sa_flags = SA_RESTART;
#endif
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
#else
    signal(SIGINT, sigbrkhandler);
#endif
}

static Display *display;
static int screen;
static Window rootwin, mywin;

static GC whitegc,blackgc,picassogc;
static XColor black,white;
static Colormap cmap, cmap2;

/* Kludge-O-Matic */
static int dga_colormap_installed;

static int need_dither;

static int screen_is_picasso;
static char picasso_invalid_lines[1201];

static int autorepeatoff = 0;
static char *image_mem;
static XImage *img, *picasso_img;
static Visual *vis;
static XVisualInfo visualInfo;
static int bitdepth, bit_unit;
static Cursor blankCursor, xhairCursor;
static int cursorOn;
static int inverse_byte_order = 0;

static int current_width, current_height;

static int x11_init_ok;

 /* Keyboard and mouse */

static int keystate[256];

static int inwindow;
const long int eventmask = (KeyPressMask|KeyReleaseMask|ButtonPressMask
			    |ButtonReleaseMask|PointerMotionMask
#ifndef USE_DGA_EXTENSION
			    |FocusChangeMask|EnterWindowMask
			    |ExposureMask
			    |LeaveWindowMask
#endif
			    );

static XImage *get_image (int w, int h, char **mem_p)
{
    XImage *new_img;
    char *p;

#if SHM_SUPPORT_LINKS == 1
    if (currprefs.use_mitshm) {
	XShmSegmentInfo *shminfo = xmalloc (sizeof (XShmSegmentInfo));

	printf ("Using MIT-SHM extension.\n");
	new_img = XShmCreateImage (display, vis, bitdepth, ZPixmap, 0, shminfo, w, h);

	shminfo->shmid = shmget (IPC_PRIVATE, h * new_img->bytes_per_line,
				 IPC_CREAT | 0777);
	shminfo->shmaddr = new_img->data = (char *)shmat (shminfo->shmid, 0, 0);
	if (mem_p != 0)
	    *mem_p = new_img->data;
	shminfo->readOnly = False;
	/* let the xserver attach */
	XShmAttach (display, shminfo);
	XSync (display,0);
	/* now deleting means making it temporary */
	shmctl (shminfo->shmid, IPC_RMID, 0);
	return new_img;
    }
#endif

    /* Question for people who know about X: Could we allocate the buffer
     * after creating the image and then do new_img->data = buffer, as above in
     * the SHM case?
     */
    printf ("Using normal image buffer.\n");
    p = (char *)xmalloc (h * w * ((bit_unit + 7) / 8)); /* ??? */
    if (mem_p != 0)
	*mem_p = p;
    new_img = XCreateImage (display, vis, bitdepth, ZPixmap, 0, p,
			    w, h, 32, 0);
    if (new_img->bytes_per_line != w * ((bit_unit + 7) / 8))
	fprintf (stderr, "Possible bug here... graphics may look strange.\n");

    return new_img;
}

#ifdef USE_DGA_EXTENSION

static int fb_bank, fb_banks, fb_mem;
static char *fb_addr;
static int fb_width;

static void switch_to_dga_mode (void)
{
    XF86DGADirectVideo(display, screen, XF86DGADirectGraphics | XF86DGADirectMouse | XF86DGADirectKeyb);
    XF86DGASetViewPort (display, screen, 0, 0);
    memset (fb_addr, 0, fb_mem * 1024);
}


#ifdef USE_VIDMODE_EXTENSION
static XF86VidModeModeInfo **allmodes;
static int vidmodecount;

static int get_vidmodes (void)
{
    return XF86VidModeGetAllModeLines (display, screen, &vidmodecount, &allmodes);
}
#endif

static void switch_to_best_mode (void)
{
#ifdef USE_VIDMODE_EXTENSION
    int i, best;
    best = 0;
    for (i = 1; i < vidmodecount; i++) {
	if (allmodes[i]->hdisplay >= current_width
	    && allmodes[i]->vdisplay >= current_height
	    && allmodes[i]->hdisplay <= allmodes[best]->hdisplay
	    && allmodes[i]->vdisplay <= allmodes[best]->vdisplay)
	    best = i;
    }
    printf ("entering DGA mode: %dx%d (%d, %d)\n",
	    allmodes[best]->hdisplay, allmodes[best]->vdisplay,
	    current_width, current_height);
    XF86VidModeSwitchToMode (display, screen, allmodes[best]);
    XF86VidModeSetViewPort (display, screen, 0, 0);
#endif
    XMoveWindow (display, mywin, 0, 0);
    XWarpPointer (display, None, rootwin, 0, 0, 0, 0, 0, 0);
    switch_to_dga_mode ();
}

static void enter_dga_mode (void)
{
    Screen *scr = ScreenOfDisplay (display, screen);
    int w = WidthOfScreen (scr);
    int h = HeightOfScreen (scr);

    XRaiseWindow (display, mywin);

    /* We want all the key presses */
    XGrabKeyboard (display, rootwin, 1, GrabModeAsync,
		   GrabModeAsync,  CurrentTime);

    /* and all the mouse moves */
    XGrabPointer (display, rootwin, 1, PointerMotionMask | ButtonPressMask | ButtonReleaseMask,
		  GrabModeAsync, GrabModeAsync, None,  None, CurrentTime);

    XF86DGAGetVideo (display, screen, &fb_addr, &fb_width, &fb_bank, &fb_mem);
    fprintf (stderr, "addr:%X, width %d, bank size %d mem size %d\n",
	     fb_addr, fb_width, fb_bank,fb_mem);

    if (fb_bank < fb_mem)
	fprintf (stderr, "banksize < memsize, must use XF86DGASetVidPage !\n");

    switch_to_best_mode ();

    gfxvidinfo.rowbytes = fb_width*gfxvidinfo.pixbytes;
    gfxvidinfo.bufmem = fb_addr;
}

static void leave_dga_mode (void)
{
#ifdef USE_VIDMODE_EXTENSION
    XF86VidModeSwitchToMode (display, screen, allmodes[0]);
#endif
    XF86DGADirectVideo (display, screen, 0);
    XUngrabPointer (display, CurrentTime);
    XUngrabKeyboard (display, CurrentTime);
}

#endif
static char *oldpixbuf;

void flush_line(int y)
{
#ifdef USE_DGA_EXTENSION
    if (need_dither) {
	char *addr = gfxvidinfo.linemem;
	if (addr == NULL)
	    addr = y*gfxvidinfo.rowbytes + gfxvidinfo.bufmem;

	DitherLine((unsigned char *)(fb_addr + fb_width*y), (uae_u16 *)addr, 0, y,
		   gfxvidinfo.width, bit_unit);
    }
#else
    char *linebuf;
    int xs = 0, xe;
    int len;

    linebuf = gfxvidinfo.linemem;
    if (linebuf == NULL)
	linebuf = y*gfxvidinfo.rowbytes + gfxvidinfo.bufmem;

    xe = gfxvidinfo.width-1;

    if (currprefs.use_low_bandwidth) {
	char *src, *dst;
	switch(gfxvidinfo.pixbytes) {
	 case 4:
	    {
		uae_u32 *newp = (uae_u32 *)linebuf;
		uae_u32 *oldp = (uae_u32 *)((uae_u8 *)image_mem + y*img->bytes_per_line);
		while (newp[xs] == oldp[xs]) {
		    if (xs == xe)
			return;
		    xs++;
		}
		while (newp[xe] == oldp[xe]) xe--;

		dst = (char *)(oldp + xs); src = (char *)(newp + xs);
	    }
	    break;
	 case 2:
	    {
		uae_u16 *newp = (uae_u16 *)linebuf;
		uae_u16 *oldp = (uae_u16 *)((uae_u8 *)image_mem + y*img->bytes_per_line);
		while (newp[xs] == oldp[xs]) {
		    if (xs == xe)
			return;
		    xs++;
		}
		while (newp[xe] == oldp[xe]) xe--;

		dst = (char *)(oldp + xs); src = (char *)(newp + xs);
	    }
	    break;
	 case 1:
	    {
		uae_u8 *newp = (uae_u8 *)linebuf;
		uae_u8 *oldp = (uae_u8 *)((uae_u8 *)image_mem + y*img->bytes_per_line);
		while (newp[xs] == oldp[xs]) {
		    if (xs == xe)
			return;
		    xs++;
		}
		while (newp[xe] == oldp[xe]) xe--;

		dst = (char *)(oldp + xs); src = (char *)(newp + xs);
	    }
	    break;

	 default:
	    abort();
	    break;
	}

	len = xe - xs + 1;
	memcpy (dst, src, len * gfxvidinfo.pixbytes);
    } else if (need_dither) {
	uae_u8 *target = (uae_u8 *)image_mem + img->bytes_per_line * y;
	len = currprefs.gfx_width;
	DitherLine(target, (uae_u16 *)linebuf, 0, y, gfxvidinfo.width, bit_unit);
    } else {
	fprintf(stderr, "Bug!\n");
	abort();
    }

    DO_PUTIMAGE (img, xs, y, xs, y, len, 1);
#endif
}

void flush_block (int ystart, int ystop)
{
#ifdef USE_DGA_EXTENSION
#else
    int len, xs = 0;

    len = gfxvidinfo.width;
    DO_PUTIMAGE (img, xs, ystart, 0, ystart, len, ystop - ystart + 1);
#endif
}

void flush_screen (int ystart, int ystop)
{
#ifdef USE_DGA_EXTENSION
#else
#if SHM_SUPPORT_LINKS == 1
    if (currprefs.use_mitshm) XSync(display, 0);
#endif
#endif
}

static __inline__ int bitsInMask(unsigned long mask)
{
    /* count bits in mask */
    int n = 0;
    while(mask) {
	n += mask&1;
	mask >>= 1;
    }
    return n;
}

static __inline__ int maskShift(unsigned long mask)
{
    /* determine how far mask is shifted */
    int n = 0;
    while(!(mask&1)) {
	n++;
	mask >>= 1;
    }
    return n;
}

static unsigned long pixel_return[256];
static XColor parsed_xcolors[256];
static int ncolors = 0;

static int blackval = 32767;
static int whiteval = 0;

static int get_color(int r, int g, int b, xcolnr *cnp)
{
    XColor *col = parsed_xcolors + ncolors;
    char str[10];

    sprintf(str, "rgb:%x/%x/%x", r, g, b);
    XParseColor(display, cmap, str, col);
    *cnp = col->pixel = pixel_return[ncolors];
    XStoreColor (display, cmap, col);
    XStoreColor (display, cmap2, col);

    if (r + g + b < blackval)
	blackval = r + g + b, black = *col;
    if (r + g + b > whiteval)
	whiteval = r + g + b, white = *col;

    ncolors++;
    return 1;
}

static int init_colors(void)
{
    int i;

    if (need_dither) {
	XAllocColorCells (display, cmap, 0, 0, 0, pixel_return, 256);
	XAllocColorCells (display, cmap2, 0, 0, 0, pixel_return, 256);
	if (bitdepth == 1)
	    setup_greydither (1, get_color);
	else
	    setup_dither (bitdepth, get_color);
    } else {
	if (bitdepth != 8 && bitdepth != 12 && bitdepth != 15
	    && bitdepth != 16 && bitdepth != 24) {
	    fprintf(stderr, "Unsupported bit depth (%d)\n", bitdepth);
	    return 0;
	}

	switch (visualInfo.VI_CLASS) {
	 case TrueColor:
	    {
		int red_bits = bitsInMask(visualInfo.red_mask);
		int green_bits = bitsInMask(visualInfo.green_mask);
		int blue_bits = bitsInMask(visualInfo.blue_mask);
		int red_shift = maskShift(visualInfo.red_mask);
		int green_shift = maskShift(visualInfo.green_mask);
		int blue_shift = maskShift(visualInfo.blue_mask);
		alloc_colors64k(red_bits, green_bits, blue_bits, red_shift,
				green_shift, blue_shift);
	    }
	    XParseColor(display, cmap, "#000000", &black);
	    if (!XAllocColor(display, cmap, &black))
		fprintf(stderr, "Whoops??\n");
	    XParseColor(display, cmap, "#ffffff", &white);
	    if (!XAllocColor(display, cmap, &white))
		fprintf(stderr, "Whoops??\n");
	    break;

	 case GrayScale:
	 case PseudoColor:
	    XAllocColorCells (display, cmap, 0, 0, 0, pixel_return, 256);
	    XAllocColorCells (display, cmap2, 0, 0, 0, pixel_return, 256);
	    alloc_colors256(get_color);
	    break;

	 default:
	    fprintf (stderr, "Unsupported visual class (%d)\n", visualInfo.VI_CLASS);
	    return 0;
	}
    }
    switch (gfxvidinfo.pixbytes) {
     case 2:
	for (i = 0; i < 4096; i++)
	    xcolors[i] = xcolors[i] * 0x00010001;
	gfxvidinfo.can_double = 1;
	break;
     case 1:
	for (i = 0; i < 4096; i++)
	    xcolors[i] = xcolors[i] * 0x01010101;
	gfxvidinfo.can_double = 1;
	break;
     default:
	gfxvidinfo.can_double = 0;
	break;
    }
    if(inverse_byte_order)
	switch(gfxvidinfo.pixbytes) {
	 case 4:
	    for(i = 0; i < 4096; i++)
		xcolors[i] = ((((xcolors[i]>>0)&255) << 24)
			      | (((xcolors[i]>>8)&255) << 16)
			      | (((xcolors[i]>>16)&255) << 8)
			      | (((xcolors[i]>>24)&255) << 0));
	    break;
	 case 2:
	    for (i = 0; i < 4096; i++)
		xcolors[i] = (xcolors[i]>>8) | ((xcolors[i]&255)<<8);
	    break;
	 default:
	    break;
	}
    return 1;
}

int graphics_setup(void)
{
    char *display_name = 0;
#ifdef USE_DGA_EXTENSION
    int MajorVersion, MinorVersion;
    int EventBase, ErrorBase;
    if (geteuid()) {
	fprintf(stderr, "You must be root to use UAE with the DGA extensions.\n");
	return 0;
    }
#endif

    display = XOpenDisplay(display_name);
    if (display == 0)  {
	fprintf(stderr, "Can't connect to X server %s\n", XDisplayName(display_name));
	return 0;
    }

#ifdef USE_DGA_EXTENSION
    if (!XF86DGAQueryVersion (display, &MajorVersion, &MinorVersion)) {
	fprintf(stderr, "Unable to query video extension version\n");
	return 0;
    }

    if (!XF86DGAQueryExtension (display, &EventBase, &ErrorBase)) {
	fprintf(stderr, "Unable to query video extension information\n");
	return 0;
    }

    /* Fail if the extension version in the server is too old */
    if (MajorVersion < DGA_MINMAJOR ||
	(MajorVersion == DGA_MINMAJOR && MinorVersion < DGA_MINMINOR)) {
	fprintf (stderr,
		 "Xserver is running an old XFree86-DGA version"
		 " (%d.%d)\n", MajorVersion, MinorVersion);
	fprintf (stderr, "Minimum required version is %d.%d\n",
		DGA_MINMAJOR, DGA_MINMINOR);
	return 0;
    }
#ifdef USE_VIDMODE_EXTENSION
    if (!XF86VidModeQueryVersion (display, &MajorVersion, &MinorVersion)) {
	fprintf (stderr, "Unable to query video extension version\n");
	return 0;
    }

    if (!XF86VidModeQueryExtension (display, &EventBase, &ErrorBase)) {
	fprintf (stderr, "Unable to query video extension information\n");
	return 0;
    }

    /* Fail if the extension version in the server is too old */
    if (MajorVersion < VidMode_MINMAJOR ||
	(MajorVersion == VidMode_MINMAJOR && MinorVersion < VidMode_MINMINOR)) {
	fprintf (stderr,
		 "Xserver is running an old XFree86-VidMode version"
		 " (%d.%d)\n", MajorVersion, MinorVersion);
	fprintf (stderr, "Minimum required version is %d.%d\n",
		 VidMode_MINMAJOR, VidMode_MINMINOR);
	return 0;
    }
    if (!get_vidmodes ()) {
	fprintf (stderr, "Error getting video mode information\n");
	return 0;
    }
#endif
    
#endif

    {
	int local_byte_order;
	int x = 0x04030201;
	char *y=(char*)&x;

	local_byte_order = y[0] == 0x04 ? MSBFirst : LSBFirst;
	if (ImageByteOrder(display) != local_byte_order)
	    inverse_byte_order = 1;
    }

    return 1;
}

static void fixup_prefs_dimensions (void)
{
    if (currprefs.gfx_width < 320)
	currprefs.gfx_width = 320;
    if (currprefs.gfx_height < 200)
	currprefs.gfx_height = 200;
    if (currprefs.gfx_height > 300 && ! currprefs.gfx_linedbl)
	currprefs.gfx_height = 300;
    if (currprefs.gfx_height > 600)
	currprefs.gfx_height = 600;

    currprefs.gfx_width += 7; /* X86.S wants multiples of 4 bytes, might be 8 in the future. */
    currprefs.gfx_width &= ~7;
}

int graphics_init(void)
{
    int i,j;
    XSetWindowAttributes wattr;
    XPixmapFormatValues *xpfvs;

    if (currprefs.color_mode > 5)
	fprintf(stderr, "Bad color mode selected. Using default.\n"), currprefs.color_mode = 0;

    x11_init_ok = 0;
    need_dither = 0;
    screen_is_picasso = 0;

    screen = XDefaultScreen(display);
    rootwin = XRootWindow(display,screen);

    /* try for a 12 bit visual first, then a 16 bit, then a 24 bit, then 8 bit */
    if (XMatchVisualInfo(display, screen, 12, TrueColor, &visualInfo)) {
    } else if (XMatchVisualInfo(display, screen, 15, TrueColor, &visualInfo)) {
    } else if (XMatchVisualInfo(display, screen, 16, TrueColor, &visualInfo)) {
    } else if (XMatchVisualInfo(display, screen, 24, TrueColor, &visualInfo)) {
    } else if (XMatchVisualInfo(display, screen, 8, PseudoColor, &visualInfo)) {
	/* for our HP boxes */
    } else if (XMatchVisualInfo(display, screen, 8, GrayScale, &visualInfo)) {
    } else if (XMatchVisualInfo(display, screen, 4, PseudoColor, &visualInfo)) {
	/* VGA16 server. Argh. */
    } else if (XMatchVisualInfo(display, screen, 1, StaticGray, &visualInfo)) {
	/* Mono server. Yuk */
    } else {
	fprintf(stderr, "Can't obtain appropriate X visual.\n");
	return 0;
    }
    vis = visualInfo.visual;
    bitdepth = visualInfo.depth;

    /* We now have the bitdepth of the display, but that doesn't tell us yet
     * how many bits to use per pixel. The VGA16 server has a bitdepth of 4,
     * but uses 1 byte per pixel. */
    xpfvs = XListPixmapFormats(display, &i);
    for (j = 0; j < i && xpfvs->depth != bitdepth; j++, xpfvs++)
	;
    if (j == i) {
	fprintf(stderr, "Your X server is feeling ill.\n");
	return 0;
    }

    bit_unit = xpfvs->bits_per_pixel;
    fprintf(stderr, "Using %d bit visual, %d bits per pixel\n", bitdepth, bit_unit);

    fixup_prefs_dimensions ();

    gfxvidinfo.width = currprefs.gfx_width;
    gfxvidinfo.height = currprefs.gfx_height;

    cmap = XCreateColormap(display, rootwin, vis, AllocNone);
    cmap2 = XCreateColormap(display, rootwin, vis, AllocNone);
#ifdef USE_DGA_EXTENSION
    wattr.override_redirect = 1;
#endif
    wattr.event_mask = eventmask;
    wattr.background_pixel = /*black.pixel*/0;
    wattr.backing_store = Always;
    wattr.backing_planes = bitdepth;
    wattr.border_pixmap = None;
    wattr.border_pixel = /*black.pixel*/0;
    wattr.colormap = cmap;

    mywin = XCreateWindow(display, rootwin, 0, 0, currprefs.gfx_width, currprefs.gfx_height, 0,
			  bitdepth, InputOutput, vis,
			  CWEventMask|CWBackPixel|CWBorderPixel|CWBackingStore
			  |CWBackingPlanes|CWColormap
#ifdef USE_DGA_EXTENSION
			  |CWOverrideRedirect
#endif
			  ,&wattr);

    current_width = currprefs.gfx_width;
    current_height = currprefs.gfx_height;

    XMapWindow(display, mywin);
#ifdef USE_DGA_EXTENSION
    XRaiseWindow(display, mywin);
#else
    XStoreName(display, mywin, "UAE");
#endif

    if (bitdepth < 8 || (bitdepth == 8 && currprefs.color_mode == 3)) {
	gfxvidinfo.pixbytes = 2;
	currprefs.use_low_bandwidth = 0;
	need_dither = 1;
    } else {
	gfxvidinfo.pixbytes = bit_unit >> 3;
    }

#ifdef USE_DGA_EXTENSION
    enter_dga_mode ();
    /*setuid(getuid());*/
#else

    img = get_image (currprefs.gfx_width, currprefs.gfx_height, &image_mem);
#endif

    if (need_dither) {
	gfxvidinfo.maxblocklines = 0;
	gfxvidinfo.rowbytes = gfxvidinfo.pixbytes * currprefs.gfx_width;
	gfxvidinfo.linemem = (char *)malloc(gfxvidinfo.rowbytes);
    } else if (currprefs.use_low_bandwidth) {
	gfxvidinfo.maxblocklines = 0;
	gfxvidinfo.rowbytes = img->bytes_per_line;
	gfxvidinfo.linemem = gfxvidinfo.bufmem = (char *)malloc(gfxvidinfo.rowbytes);
    } else {
	gfxvidinfo.maxblocklines = 100; /* whatever... */
#ifndef USE_DGA_EXTENSION
	gfxvidinfo.rowbytes = img->bytes_per_line;
	gfxvidinfo.bufmem = image_mem;
#endif
	gfxvidinfo.linemem = NULL;
    }

    if (!init_colors())
	return 0;

#ifndef USE_DGA_EXTENSION
    blankCursor = XCreatePixmapCursor(display,
				      XCreatePixmap(display, mywin, 1, 1, 1),
				      XCreatePixmap(display, mywin, 1, 1, 1),
				      &black, &white, 0, 0);
    xhairCursor = XCreateFontCursor(display, XC_crosshair);

    whitegc = XCreateGC(display,mywin,0,0);
    blackgc = XCreateGC(display,mywin,0,0);
    picassogc = XCreateGC (display,mywin,0,0);

    XSetForeground(display, blackgc, black.pixel);
    XSetForeground(display, whitegc, white.pixel);
#endif

    buttonstate[0] = buttonstate[1] = buttonstate[2] = 0;
    for(i=0; i<256; i++)
	keystate[i] = 0;

    lastmx = lastmy = 0;
    newmousecounters = 0;
    inwindow = 0;

#ifndef USE_DGA_EXTENSION
    if (!currprefs.no_xhair)
	XDefineCursor(display, mywin, xhairCursor);
    else
	XDefineCursor(display, mywin, blankCursor);
    cursorOn = 1;
#else
    dga_colormap_installed = 0;
    XF86DGAInstallColormap (display, screen, cmap);
#endif

    return x11_init_ok = 1;
}

void graphics_leave(void)
{
    if (!x11_init_ok)
	return;

    if (autorepeatoff)
	XAutoRepeatOn(display);
    XFlush(display);
    XSync(display, 0);
#ifdef USE_DGA_EXTENSION
    leave_dga_mode ();
    dumpcustom();
#endif
}

/* Decode KeySyms. This function knows about all keys that are common
 * between different keyboard languages. */
static int kc_decode (KeySym ks)
{
    switch (ks) {
     case XK_B: case XK_b: return AK_B;
     case XK_C: case XK_c: return AK_C;
     case XK_D: case XK_d: return AK_D;
     case XK_E: case XK_e: return AK_E;
     case XK_F: case XK_f: return AK_F;
     case XK_G: case XK_g: return AK_G;
     case XK_H: case XK_h: return AK_H;
     case XK_I: case XK_i: return AK_I;
     case XK_J: case XK_j: return AK_J;
     case XK_K: case XK_k: return AK_K;
     case XK_L: case XK_l: return AK_L;
     case XK_N: case XK_n: return AK_N;
     case XK_O: case XK_o: return AK_O;
     case XK_P: case XK_p: return AK_P;
     case XK_R: case XK_r: return AK_R;
     case XK_S: case XK_s: return AK_S;
     case XK_T: case XK_t: return AK_T;
     case XK_U: case XK_u: return AK_U;
     case XK_V: case XK_v: return AK_V;
     case XK_X: case XK_x: return AK_X;

     case XK_0: return AK_0;
     case XK_1: return AK_1;
     case XK_2: return AK_2;
     case XK_3: return AK_3;
     case XK_4: return AK_4;
     case XK_5: return AK_5;
     case XK_6: return AK_6;
     case XK_7: return AK_7;
     case XK_8: return AK_8;
     case XK_9: return AK_9;

	/* You never know which Keysyms might be missing on some workstation
	 * This #ifdef should be enough. */
#if defined(XK_KP_Prior) && defined(XK_KP_Left) && defined(XK_KP_Insert) && defined (XK_KP_End)
     case XK_KP_0: case XK_KP_Insert: return AK_NP0;
     case XK_KP_1: case XK_KP_End: return AK_NP1;
     case XK_KP_2: case XK_KP_Down: return AK_NP2;
     case XK_KP_3: case XK_KP_Next: return AK_NP3;
     case XK_KP_4: case XK_KP_Left: return AK_NP4;
     case XK_KP_5: case XK_KP_Begin: return AK_NP5;
     case XK_KP_6: case XK_KP_Right: return AK_NP6;
     case XK_KP_7: case XK_KP_Home: return AK_NP7;
     case XK_KP_8: case XK_KP_Up: return AK_NP8;
     case XK_KP_9: case XK_KP_Prior: return AK_NP9;
#else
     case XK_KP_0: return AK_NP0;
     case XK_KP_1: return AK_NP1;
     case XK_KP_2: return AK_NP2;
     case XK_KP_3: return AK_NP3;
     case XK_KP_4: return AK_NP4;
     case XK_KP_5: return AK_NP5;
     case XK_KP_6: return AK_NP6;
     case XK_KP_7: return AK_NP7;
     case XK_KP_8: return AK_NP8;
     case XK_KP_9: return AK_NP9;
#endif
     case XK_KP_Divide: return AK_NPDIV;
     case XK_KP_Multiply: return AK_NPMUL;
     case XK_KP_Subtract: return AK_NPSUB;
     case XK_KP_Add: return AK_NPADD;
     case XK_KP_Decimal: return AK_NPDEL;
     case XK_KP_Enter: return AK_ENT;

     case XK_F1: return AK_F1;
     case XK_F2: return AK_F2;
     case XK_F3: return AK_F3;
     case XK_F4: return AK_F4;
     case XK_F5: return AK_F5;
     case XK_F6: return AK_F6;
     case XK_F7: return AK_F7;
     case XK_F8: return AK_F8;
     case XK_F9: return AK_F9;
     case XK_F10: return AK_F10;

     case XK_BackSpace: return AK_BS;
     case XK_Delete: return AK_DEL;
     case XK_Control_L: return AK_CTRL;
     case XK_Control_R: return AK_RCTRL;
     case XK_Tab: return AK_TAB;
     case XK_Alt_L: return AK_LALT;
     case XK_Alt_R: return AK_RALT;
     case XK_Meta_R: case XK_Hyper_R: return AK_RAMI;
     case XK_Meta_L: case XK_Hyper_L: return AK_LAMI;
     case XK_Return: return AK_RET;
     case XK_space: return AK_SPC;
     case XK_Shift_L: return AK_LSH;
     case XK_Shift_R: return AK_RSH;
     case XK_Escape: return AK_ESC;

     case XK_Insert: return AK_HELP;
     case XK_Home: return AK_NPLPAREN;
     case XK_End: return AK_NPRPAREN;
     case XK_Caps_Lock: return AK_CAPSLOCK;

     case XK_Up: return AK_UP;
     case XK_Down: return AK_DN;
     case XK_Left: return AK_LF;
     case XK_Right: return AK_RT;

     case XK_F11: return AK_BACKSLASH;
#ifdef USE_DGA_EXTENSION
     case XK_F12:
	uae_quit();
	return -1;
#else
     case XK_F12: return AK_mousestuff;
#endif
#ifdef XK_F14
     case XK_F14:
#endif
     case XK_Scroll_Lock: return AK_inhibit;

#ifdef XK_Page_Up /* These are missing occasionally */
     case XK_Page_Up: return AK_RAMI;          /* PgUp mapped to right amiga */
     case XK_Page_Down: return AK_LAMI;        /* PgDn mapped to left amiga */
#endif
    }
    return -1;
}

static int decode_fr(KeySym ks)
{
    switch(ks) {        /* FR specific */
     case XK_A: case XK_a: return AK_Q;
     case XK_M: case XK_m: return AK_SEMICOLON;
     case XK_Q: case XK_q: return AK_A;
     case XK_Y: case XK_y: return AK_Y;
     case XK_W: case XK_w: return AK_Z;
     case XK_Z: case XK_z: return AK_W;
     case XK_bracketleft: return AK_LBRACKET;
     case XK_bracketright: return AK_RBRACKET;
     case XK_comma: return AK_M;
     case XK_less: case XK_greater: return AK_LTGT;
     case XK_period: return AK_COMMA;
     case XK_parenright: return AK_MINUS;
     case XK_equal: return AK_SLASH;
     case XK_numbersign: return AK_NUMBERSIGN;
     case XK_slash: return AK_PERIOD;
     case XK_minus: return AK_EQUAL;
     case XK_backslash: return AK_BACKSLASH;
    }

    return -1;
}

static int decode_us(KeySym ks)
{
    switch(ks) {	/* US specific */
     case XK_A: case XK_a: return AK_A;
     case XK_M: case XK_m: return AK_M;
     case XK_Q: case XK_q: return AK_Q;
     case XK_Y: case XK_y: return AK_Y;
     case XK_W: case XK_w: return AK_W;
     case XK_Z: case XK_z: return AK_Z;
     case XK_bracketleft: return AK_LBRACKET;
     case XK_bracketright: return AK_RBRACKET;
     case XK_comma: return AK_COMMA;
     case XK_period: return AK_PERIOD;
     case XK_slash: return AK_SLASH;
     case XK_semicolon: return AK_SEMICOLON;
     case XK_minus: return AK_MINUS;
     case XK_equal: return AK_EQUAL;
	/* this doesn't work: */
     case XK_quoteright: return AK_QUOTE;
     case XK_quoteleft: return AK_BACKQUOTE;
     case XK_backslash: return AK_BACKSLASH;
    }

    return -1;
}

static int decode_de(KeySym ks)
{
    switch(ks) {
	/* DE specific */
     case XK_A: case XK_a: return AK_A;
     case XK_M: case XK_m: return AK_M;
     case XK_Q: case XK_q: return AK_Q;
     case XK_W: case XK_w: return AK_W;
     case XK_Y: case XK_y: return AK_Z;
     case XK_Z: case XK_z: return AK_Y;
     case XK_Odiaeresis: case XK_odiaeresis: return AK_SEMICOLON;
     case XK_Adiaeresis: case XK_adiaeresis: return AK_QUOTE;
     case XK_Udiaeresis: case XK_udiaeresis: return AK_LBRACKET;
     case XK_plus: case XK_asterisk: return AK_RBRACKET;
     case XK_comma: return AK_COMMA;
     case XK_period: return AK_PERIOD;
     case XK_less: case XK_greater: return AK_LTGT;
     case XK_numbersign: return AK_NUMBERSIGN;
     case XK_ssharp: return AK_MINUS;
     case XK_apostrophe: return AK_EQUAL;
     case XK_asciicircum: return AK_BACKQUOTE;
     case XK_minus: return AK_SLASH;
    }

    return -1;
}

static int decode_se(KeySym ks)
{
    switch(ks) {
	/* SE specific */
     case XK_A: case XK_a: return AK_A;
     case XK_M: case XK_m: return AK_M;
     case XK_Q: case XK_q: return AK_Q;
     case XK_W: case XK_w: return AK_W;
     case XK_Y: case XK_y: return AK_Y;
     case XK_Z: case XK_z: return AK_Z;
     case XK_Odiaeresis: case XK_odiaeresis: return AK_SEMICOLON;
     case XK_Adiaeresis: case XK_adiaeresis: return AK_QUOTE;
     case XK_Aring: case XK_aring: return AK_LBRACKET;
     case XK_comma: return AK_COMMA;
     case XK_period: return AK_PERIOD;
     case XK_minus: return AK_SLASH;
     case XK_less: case XK_greater: return AK_LTGT;
     case XK_plus: case XK_question: return AK_EQUAL;
     case XK_at: case XK_onehalf: return AK_BACKQUOTE;
     case XK_asciitilde: case XK_asciicircum: return AK_RBRACKET;
     case XK_backslash: case XK_bar: return AK_MINUS;

     case XK_numbersign: return AK_NUMBERSIGN;
    }

    return -1;
 }

static int decode_it(KeySym ks)
{
    switch(ks) {
	/* IT specific */
     case XK_A: case XK_a: return AK_A;
     case XK_M: case XK_m: return AK_M;
     case XK_Q: case XK_q: return AK_Q;
     case XK_W: case XK_w: return AK_W;
     case XK_Y: case XK_y: return AK_Y;
     case XK_Z: case XK_z: return AK_Z;
     case XK_Ograve: case XK_ograve: return AK_SEMICOLON;
     case XK_Agrave: case XK_agrave: return AK_QUOTE;
     case XK_Egrave: case XK_egrave: return AK_LBRACKET;
     case XK_plus: case XK_asterisk: return AK_RBRACKET;
     case XK_comma: return AK_COMMA;
     case XK_period: return AK_PERIOD;
     case XK_less: case XK_greater: return AK_LTGT;
     case XK_backslash: case XK_bar: return AK_BACKQUOTE;
     case XK_apostrophe: return AK_MINUS;
     case XK_Igrave: case XK_igrave: return AK_EQUAL;
     case XK_minus: return AK_SLASH;
     case XK_numbersign: return AK_NUMBERSIGN;
    }

    return -1;
}

static int decode_es(KeySym ks)
{
    switch(ks) {
	/* ES specific */
     case XK_A: case XK_a: return AK_A;
     case XK_M: case XK_m: return AK_M;
     case XK_Q: case XK_q: return AK_Q;
     case XK_W: case XK_w: return AK_W;
     case XK_Y: case XK_y: return AK_Y;
     case XK_Z: case XK_z: return AK_Z;
     case XK_ntilde: case XK_Ntilde: return AK_SEMICOLON;
#ifdef XK_dead_acute
     case XK_dead_acute: case XK_dead_diaeresis: return AK_QUOTE;
     case XK_dead_grave: case XK_dead_circumflex: return AK_LBRACKET;
#endif
     case XK_plus: case XK_asterisk: return AK_RBRACKET;
     case XK_comma: return AK_COMMA;
     case XK_period: return AK_PERIOD;
     case XK_less: case XK_greater: return AK_LTGT;
     case XK_backslash: case XK_bar: return AK_BACKQUOTE;
     case XK_apostrophe: return AK_MINUS;
     case XK_Igrave: case XK_igrave: return AK_EQUAL;
     case XK_minus: return AK_SLASH;
     case XK_numbersign: return AK_NUMBERSIGN;
    }

    return -1;
}

static int keycode2amiga(XKeyEvent *event)
{
    KeySym ks;
    int as;
    int index = 0;

    do {
	ks = XLookupKeysym(event, index);
	as = kc_decode (ks);

	if (as == -1) {
	    switch(currprefs.keyboard_lang) {

	    case KBD_LANG_FR:
		as = decode_fr(ks);
		break;

	    case KBD_LANG_US:
		as = decode_us(ks);
		break;

	     case KBD_LANG_DE:
		as = decode_de(ks);
		break;

	     case KBD_LANG_SE:
		as = decode_se(ks);
		break;

	     case KBD_LANG_IT:
		as = decode_it(ks);
		break;

	     case KBD_LANG_ES:
		as = decode_es(ks);
		break;

	     default:
		as = -1;
		break;
	    }
	}
	if(-1 != as)
		return as;
	index++;
    } while (ks != NoSymbol);
    return -1;
}

static struct timeval lastMotionTime;

static int refresh_necessary = 0;

void handle_events(void)
{
    newmousecounters = 0;
    gui_handle_events();

    for (;;) {
	XEvent event;
#if 0
	if (!XCheckMaskEvent(display, eventmask, &event)) break;
#endif
	if (!XPending(display)) break;
	XNextEvent(display, &event);

	switch(event.type) {
	 case KeyPress: {
	     int kc = keycode2amiga((XKeyEvent *)&event);
	     if (kc == -1) break;
	     switch (kc) {
	      case AK_mousestuff:
		 togglemouse();
		 break;

	      case AK_inhibit:
		 toggle_inhibit_frame (0);
		 break;

	      default:
		 if (!keystate[kc]) {
		     keystate[kc] = 1;
		     record_key (kc << 1);
		 }
		 break;
	     }
	     break;
	 }
	 case KeyRelease: {
	     int kc = keycode2amiga((XKeyEvent *)&event);
	     if (kc == -1) break;
	     keystate[kc] = 0;
	     record_key ((kc << 1) | 1);
	     break;
	 }
	 case ButtonPress:
	    buttonstate[((XButtonEvent *)&event)->button-1] = 1;
	    break;
	 case ButtonRelease:
	    buttonstate[((XButtonEvent *)&event)->button-1] = 0;
	    break;
#ifndef USE_DGA_EXTENSION
	 case EnterNotify:
	    newmousecounters = 1;
	    lastmx = ((XCrossingEvent *)&event)->x;
	    lastmy = ((XCrossingEvent *)&event)->y;
	    inwindow = 1;
	    break;
	 case LeaveNotify:
	    inwindow = 0;
	    break;
	 case FocusIn:
	    if (!autorepeatoff)
		XAutoRepeatOff(display);
	    autorepeatoff = 1;
	    break;
	 case FocusOut:
	    if (autorepeatoff)
		XAutoRepeatOn(display);
	    autorepeatoff = 0;
	    break;
	 case MotionNotify:
	    if (inwindow) {
		lastmx = ((XMotionEvent *)&event)->x;
		lastmy = ((XMotionEvent *)&event)->y;
		if(!cursorOn && !currprefs.no_xhair) {
		    XDefineCursor(display, mywin, xhairCursor);
		    cursorOn = 1;
		}
		gettimeofday(&lastMotionTime, NULL);
	    }
	    break;
	 case Expose:
	    refresh_necessary = 1;
	    break;
#else
	 case MotionNotify:
	    newmousecounters = 0;
	    lastmx += ((XMotionEvent *)&event)->x_root;
	    lastmy += ((XMotionEvent *)&event)->y_root;
#endif
	}
    }
#if defined PICASSO96 && !defined USE_DGA_EXTENSION
    if (screen_is_picasso && refresh_necessary) {
	DO_PUTIMAGE (picasso_img, 0, 0, 0, 0,
		     picasso_vidinfo.width, picasso_vidinfo.height);
	refresh_necessary = 0;
    } else if (screen_is_picasso) {
	int i;
	int strt = -1, stop = -1;
	picasso_invalid_lines[picasso_vidinfo.height] = 0;
	for (i = 0; i < picasso_vidinfo.height + 1; i++) {
	    if (picasso_invalid_lines[i]) {
		picasso_invalid_lines[i] = 0;
		if (strt != -1)
		    continue;
		strt = i;
	    } else {
		if (strt == -1)
		    continue;
		DO_PUTIMAGE (picasso_img, 0, strt, 0, strt,
			     picasso_vidinfo.width, i-strt);
		strt = -1;
	    }
	}
    }
#endif

#ifndef USE_DGA_EXTENSION
    if (!screen_is_picasso && refresh_necessary) {
	DO_PUTIMAGE (img, 0, 0, 0, 0, currprefs.gfx_width, currprefs.gfx_height);
	refresh_necessary = 0;
    }
    if(cursorOn && !currprefs.no_xhair) {
	struct timeval now;
	int diff;
	gettimeofday(&now, NULL);
	diff = (now.tv_sec - lastMotionTime.tv_sec) * 1000000 +
	    (now.tv_usec - lastMotionTime.tv_usec);
	if(diff > 1000000) {
	    XDefineCursor(display, mywin, blankCursor);
	    cursorOn = 0;
	}
    }
#endif
    /* "Affengriff" */
    if(keystate[AK_CTRL] && keystate[AK_LAMI] && keystate[AK_RAMI])
	uae_reset();
}

int debuggable(void)
{
    return 1;
}

int needmousehack(void)
{
#ifdef USE_DGA_EXTENSION
    return 0;
#else
    return 1;
#endif
}

void LED(int on)
{
#if 0 /* Maybe that is responsible for the joystick emulation problems on SunOS? */
    static int last_on = -1;
    XKeyboardControl control;

    if (last_on == on) return;
    last_on = on;
    control.led = 1; /* implementation defined */
    control.led_mode = on ? LedModeOn : LedModeOff;
    XChangeKeyboardControl(display, KBLed | KBLedMode, &control);
#endif
}

void write_log (const char *buf)
{
    fprintf (stderr, buf);
}

#ifdef PICASSO96

void DX_Invalidate (int first, int last)
{
    do {
	picasso_invalid_lines[first] = 1;
	first++;
    } while (first <= last);
}

int DX_BitsPerCannon (void)
{
    return 8;
}

void DX_SetPalette(int start, int count)
{
    if (!screen_is_picasso || picasso_vidinfo.pixbytes != 1)
	return;

    while (count-- > 0) {
	XColor col = parsed_xcolors[start];
	col.red = picasso96_state.CLUT[start].Red * 0x0101;
	col.green = picasso96_state.CLUT[start].Green * 0x0101;
	col.blue = picasso96_state.CLUT[start].Blue * 0x0101;
	XStoreColor (display, cmap, &col);
	XStoreColor (display, cmap2, &col);
	start++;
    }
#ifdef USE_DGA_EXTENSION
    dga_colormap_installed ^= 1;
    if (dga_colormap_installed == 1)
	XF86DGAInstallColormap(display, screen, cmap2);
    else
	XF86DGAInstallColormap(display, screen, cmap);
#endif
}

#define MAX_SCREEN_MODES 11

static int x_size_table[MAX_SCREEN_MODES] = { 320, 320, 320, 320, 640, 640, 640, 800, 1024, 1152, 1280 };
static int y_size_table[MAX_SCREEN_MODES] = { 200, 240, 256, 400, 350, 480, 512, 600, 768, 864, 1024 };

int DX_FillResolutions (uae_u16 *ppixel_format)
{
    Screen *scr = ScreenOfDisplay (display, screen);
    int i, count = 0;
    int w = WidthOfScreen (scr);
    int h = HeightOfScreen (scr);
    int maxw = 0, maxh = 0;

    *ppixel_format = (bit_unit == 8 ? RGBFF_CHUNKY
		      : bitdepth == 15 && bit_unit == 16 ? RGBFF_R5G5B5PC
		      : bitdepth == 16 && bit_unit == 16 ? RGBFF_R5G6B5PC
		      : bit_unit == 24 ? RGBFF_B8G8R8
		      : bit_unit == 32 ? RGBFF_B8G8R8A8
		      : 0);

#if defined USE_DGA_EXTENSION && defined USE_VIDMODE_EXTENSION
    for (i = 0; i < vidmodecount && i < MAX_PICASSO_MODES ; i++) {
	DisplayModes[i].res.width = allmodes[i]->hdisplay;
	DisplayModes[i].res.height = allmodes[i]->vdisplay;
	DisplayModes[i].depth = bit_unit >> 3;
	DisplayModes[i].refresh = 75;
    }
    count = i;
#else
    for (i = 0; i < MAX_SCREEN_MODES && count < MAX_PICASSO_MODES ; i++) {
	if (x_size_table[i] <= w && y_size_table[i] <= h) {
	    if (x_size_table[i] > maxw)
		maxw = x_size_table[i];
	    if (y_size_table[i] > maxh)
		maxh = y_size_table[i];
	    DisplayModes[count].res.width = x_size_table[i];
	    DisplayModes[count].res.height = y_size_table[i];
	    DisplayModes[count].depth = bit_unit >> 3;
	    DisplayModes[count].refresh = 75;
	    count++;
	}
    }
#endif

#ifndef USE_DGA_EXTENSION
    picasso_img = get_image (maxw, maxh, 0);
#endif

    return count;
}

static void set_window_for_picasso (void)
{
    if (current_width != picasso_vidinfo.width || current_height != picasso_vidinfo.height) {
	current_width = picasso_vidinfo.width;
	current_height = picasso_vidinfo.height;
	XResizeWindow (display, mywin, picasso_vidinfo.width,
		       picasso_vidinfo.height);
#if defined USE_DGA_EXTENSION && defined USE_VIDMODE_EXTENSION
	switch_to_best_mode ();
#endif
    }
    DX_SetPalette (0, 256);
}

static void set_window_for_amiga (void)
{
    int i;

    if (current_width != currprefs.gfx_width || current_height != currprefs.gfx_height) {
	current_width = currprefs.gfx_width;
	current_height = currprefs.gfx_height;
	XResizeWindow (display, mywin, currprefs.gfx_width,
		       currprefs.gfx_height);
#if defined USE_DGA_EXTENSION && defined USE_VIDMODE_EXTENSION
	switch_to_best_mode ();
#endif
    }

    if (visualInfo.VI_CLASS != TrueColor)
	for (i = 0; i < 256; i++) {
	    XStoreColor (display, cmap, parsed_xcolors + i);
	    XStoreColor (display, cmap2, parsed_xcolors + i);
	}
#ifdef USE_DGA_EXTENSION
    dga_colormap_installed ^= 1;
    if (dga_colormap_installed == 1)
	XF86DGAInstallColormap(display, screen, cmap2);
    else
	XF86DGAInstallColormap(display, screen, cmap);
#endif
}

void gfx_set_picasso_modeinfo (int w, int h, int depth)
{
    picasso_vidinfo.width = w;
    picasso_vidinfo.height = h;
    picasso_vidinfo.depth = depth;
    picasso_vidinfo.pixbytes = bit_unit >> 3;
#ifdef USE_DGA_EXTENSION
    picasso_vidinfo.rowbytes = fb_width * picasso_vidinfo.pixbytes;
#else
    picasso_vidinfo.rowbytes = picasso_img->bytes_per_line;
#endif
    picasso_vidinfo.extra_mem = 1;

    if (screen_is_picasso)
	set_window_for_picasso ();
}

void gfx_set_picasso_baseaddr (uaecptr a)
{
}

void gfx_set_picasso_state (int on)
{
    if (on == screen_is_picasso)
	return;
    screen_is_picasso = on;
    if (on)
	set_window_for_picasso ();
    else
	set_window_for_amiga ();
}

void begindrawing (void)
{
}

void enddrawing (void)
{
}

uae_u8 *lockscr (void)
{
#ifdef USE_DGA_EXTENSION
    return fb_addr;
#else
    return picasso_img->data;
#endif
}
void unlockscr (void)
{
}
#endif
