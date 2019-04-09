/*
 * UAE - The Un*x Amiga Emulator
 *
 * Win32 interface
 *
 * Copyright 1997 Mathias Ortmann
 */

#ifdef __GNUC__
#define __int64 long long
#include "machdep/winstuff.h"
#else
#include <windows.h>
#include <ddraw.h>
#include <stdlib.h>
#include <stdarg.h>
#include <commctrl.h>
#include <commdlg.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <io.h>
#endif

#include "config.h"
#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "gensound.h"
#include "sounddep/sound.h"
#include "uae.h"
#include "threaddep/penguin.h"
#include "memory.h"
#include "custom.h"
#include "events.h"
#include "xwin.h"
#include "keyboard.h"
#include "picasso96.h"

#include "osdep/win32gui.h"
#include "resource.h"
#include "osdep/win32.h"

extern struct picasso_vidbuf_description picasso_vidinfo;
#define IHF_WINDOWHIDDEN 6

static BOOL (WINAPI *pGetOpenFileNameA) (LPOPENFILENAME);
static HRESULT (WINAPI *pDirectDrawCreate) (GUID FAR *, LPDIRECTDRAW FAR *, IUnknown FAR *);
static HRESULT CALLBACK modesCallback (LPDDSURFACEDESC modeDesc, LPVOID context);

HWND hAmigaWnd, hMainWnd;
HINSTANCE hInst;
/*DWORD Keys;*/
static RECT amigawin_rect;

drive_specs drives[NUM_DRIVES];
char *start_path = NULL;

static LPDIRECTDRAW lpDD;
static LPDIRECTDRAWSURFACE lpDDS;
static LPDIRECTDRAWCLIPPER lpDDC;
static LPDIRECTDRAWPALETTE lpDDP;
static DDSURFACEDESC ddsd;
HANDLE hEmuThread;

static HDC hBackDC;
static HBITMAP hBackBM;

static CRITICAL_SECTION csDraw;

#define TITLETEXT PROGNAME " -- Amiga Display"
char VersionStr[256];
extern int current_width, current_height;

static uae_sem_t picasso_switch_sem;

static int fullscreen = 0;
int current_width, current_height;
static int current_pixbytes;

static int changemode;

static int screen_is_picasso = 0;
static int picasso_fullscreen = 1;
int amiga_fullscreen = 1, customsize = 0;

int bActive;
int capslock;
int toggle_sound;

int process_desired_pri;

int cycles_per_instruction = 4;
int good_cpu = 0;

BOOL allow_quit_from_property_sheet = TRUE;
BOOL viewing_child = FALSE;

static char scrlinebuf[8192];	/* this is too large, but let's rather play on the safe side here */
static int scrindirect;

static void set_linemem (void)
{
    if (scrindirect)
	gfxvidinfo.linemem = scrlinebuf;
    else
	gfxvidinfo.linemem = 0;
}

/* Keyboard emulation, Win32 helper routines. */
static LPARAM keysdown[256];
static short numkeysdown;

int getcapslock (void)
{
    BYTE keyState[256];

    GetKeyboardState (keyState);
    return keyState[VK_CAPITAL] & 1;
}

int helppressed (void)
{
    return GetAsyncKeyState (VK_END) & 0x8000;
}

int shiftpressed (void)
{
    return GetAsyncKeyState (VK_SHIFT) & 0x8000 ? 1 : 0;
}

int checkkey (int vkey, LPARAM lParam)
{
    switch (vkey) {
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
	return GetKeyState (vkey) & 0x8000;
    }
    return GetAsyncKeyState (vkey) & 0x8000;
}

/* Mouse emulation, Win32 interface */
static int mousecx = 160, mousecy = 100, mousedx = 160, mousedy = 100;
static int mousecl = MAKELONG (160, 100);
int mouseactive;

void setmouseactive (int active)
{
    mousedx = (amigawin_rect.right - amigawin_rect.left) / 2;
    mousedy = (amigawin_rect.bottom - amigawin_rect.top) / 2;
    mousecl = MAKELONG (mousedx, mousedy);
    mousecx = amigawin_rect.left + mousedx;
    mousecy = amigawin_rect.top + mousedy;

    if (active == mouseactive)
	return;
    mouseactive = active;

    if (active) {
	ShowCursor (FALSE);
	SetCursorPos (mousecx, mousecy);
	SetWindowText (hMainWnd ? hMainWnd : hAmigaWnd, TITLETEXT " [Mouse active - press Alt-Tab to cancel]");
	ClipCursor (&amigawin_rect);
    } else {
	ShowCursor (TRUE);
	SetWindowText (hMainWnd ? hMainWnd : hAmigaWnd, TITLETEXT);
	ClipCursor (NULL);
    }
}

static int hascapture = 0;

void setcapture (void)
{
    if (hascapture)
	return;
    hascapture++;
    SetCapture (hAmigaWnd);
}

static __inline__ void releasecapture (void)
{
    if (!hascapture)
	return;
    hascapture--;
    ReleaseCapture ();
}

static void figure_processor_speed (void)
{
    SYSTEM_INFO sysinfo;
    OSVERSIONINFO osinfo;
    HKEY hKey;
    DWORD value;
    DWORD type, size;
    unsigned long mhz = 0;
    char cpuid[40];
    char *envptr;

    vsynctime = -1;
    if ((envptr = getenv ("PROCESSOR_LEVEL")) && atoi (envptr) < 5)
	goto bad_cpu;

    if (RegOpenKeyEx (HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0L, KEY_READ, &hKey) == ERROR_SUCCESS) {
	size = sizeof (value);
	if (RegQueryValueEx (hKey, "~MHz", NULL, &type, (LPBYTE) & value, &size) == ERROR_SUCCESS) 
	    mhz = value;
	size = sizeof cpuid;
	if (RegQueryValueEx (hKey, "VendorIdentifier", NULL, &type, (LPBYTE) cpuid, &size) == ERROR_SUCCESS) {
	    good_cpu = (strncmp (cpuid, "AuthenticAMD", 12) == 0
			|| strncmp (cpuid, "GenuineIntel", 12) == 0);
	}
	RegCloseKey (hKey);
    }
    if (!good_cpu)
	goto bad_cpu;

#ifdef FRAME_RATE_HACK
    if (mhz == 0) {
	frame_time_t t;
	DWORD oldpri = GetPriorityClass (GetCurrentProcess ());

	write_log ("Calibrating delay loop.. ");

	SetPriorityClass (GetCurrentProcess (), REALTIME_PRIORITY_CLASS);
	vsynctime = 1;
	t = read_processor_time ();
	Sleep (1001);
	t = 2*read_processor_time () - t;
	Sleep (1);
	t -= read_processor_time ();
	SetPriorityClass (GetCurrentProcess (), oldpri);

	MyOutputDebugString ("ok - %.2f BogoMIPS\n", (double)t / 1000000.0);
	vsynctime = t / 50;
	return;
    }
#endif
    if (mhz != 0) {
	vsynctime = mhz * 1000000 / 50;
	return;
    }
    write_log ("Could not determine clock speed of your CPU.\n");
    return;
bad_cpu:
    write_log ("Processor level too low - disabling frame rate hack\n");
}

/* DirectDraw stuff*/
char *getddrname (HRESULT ddrval)
{
    switch (ddrval) {
    case DDERR_ALREADYINITIALIZED:
	return "This object is already initialized.";
    case DDERR_BLTFASTCANTCLIP:
	return "Cannot use BLTFAST with Clipper attached to surface.";
    case DDERR_CANNOTATTACHSURFACE:
	return "Cannot attach surface.";
    case DDERR_CANNOTDETACHSURFACE:
	return "Cannot detach surface.";
    case DDERR_CANTCREATEDC:
	return "Cannot create DC Device Context.";
    case DDERR_CANTDUPLICATE:
	return "Cannot duplicate.";
    case DDERR_CANTLOCKSURFACE:
	return "Access to surface refused (no DCI provider).";
    case DDERR_CANTPAGELOCK:
	return "PageLock failure.";
    case DDERR_CANTPAGEUNLOCK:
	return "PageUnlock failure.";
    case DDERR_CLIPPERISUSINGHWND:
	return "Can't set a clip-list for a Clipper which is attached to an HWND.";
    case DDERR_COLORKEYNOTSET:
	return "No source colour-key provided.";
    case DDERR_CURRENTLYNOTAVAIL:
	return "Support unavailable.";
    case DDERR_DCALREADYCREATED:
	return "Surface already has a Device Context.";
    case DDERR_DIRECTDRAWALREADYCREATED:
	return "DirectDraw already bound to this process.";
    case DDERR_EXCEPTION:
	return "Unexpected exception.";
    case DDERR_EXCLUSIVEMODEALREADYSET:
	return "Already in exclusive mode.";
    case DDERR_GENERIC:
	return "Undefined";	/* THIS MAKES SENSE!  FUCKING M$ */

    case DDERR_HEIGHTALIGN:
	return "Height needs to be aligned.";
    case DDERR_HWNDALREADYSET:
	return "HWND already set for cooperative level.";
    case DDERR_HWNDSUBCLASSED:
	return "HWND has been subclassed.";
    case DDERR_IMPLICITLYCREATED:
	return "Can't restore an implicitly created surface.";
    case DDERR_INCOMPATIBLEPRIMARY:
	return "New params doesn't match existing primary surface.";
    case DDERR_INVALIDCAPS:
	return "Device doesn't have requested capabilities.";
    case DDERR_INVALIDCLIPLIST:
	return "Provided clip-list not supported.";
    case DDERR_INVALIDDIRECTDRAWGUID:
	return "Invalid GUID.";
    case DDERR_INVALIDMODE:
	return "Mode not supported.";
    case DDERR_INVALIDOBJECT:
	return "Invalid object.";
    case DDERR_INVALIDPARAMS:
	return "Invalid params.";
    case DDERR_INVALIDPIXELFORMAT:
	return "Device doesn't support requested pixel format.";
    case DDERR_INVALIDPOSITION:
	return "Overlay position illegal.";
    case DDERR_INVALIDRECT:
	return "Invalid RECT.";
    case DDERR_INVALIDSURFACETYPE:
	return "Wrong type of surface.";
    case DDERR_LOCKEDSURFACES:
	return "Surface locked.";
    case DDERR_NO3D:
	return "No 3d capabilities.";
    case DDERR_NOALPHAHW:
	return "No alpha h/w.";
    case DDERR_NOBLTHW:
	return "No blit h/w.";
    case DDERR_NOCLIPLIST:
	return "No clip-list.";
    case DDERR_NOCLIPPERATTACHED:
	return "No Clipper attached.";
    case DDERR_NOCOLORCONVHW:
	return "No colour-conversion h/w.";
    case DDERR_NOCOLORKEY:
	return "No colour-key.";

    case DDERR_NOTLOCKED:
	return "Not locked.";
    case DDERR_NOTPAGELOCKED:
	return "Not page-locked.";
    case DDERR_NOTPALETTIZED:
	return "Not palette-based.";

    case DDERR_OUTOFCAPS:
	return "out of caps";
    case DDERR_OUTOFMEMORY:
	return "Out of memory.";
    case DDERR_OUTOFVIDEOMEMORY:
	return "out of video memory.";
    case DDERR_PALETTEBUSY:
	return "Palette busy.";
    case DDERR_PRIMARYSURFACEALREADYEXISTS:
	return "Already a primary surface.";

    case DDERR_SURFACEBUSY:
	return "Surface busy.";
	/*case DDERR_SURFACEOBSCURED:     return "Surface is obscured.";*/
    case DDERR_SURFACELOST:
	return "Surface lost.";

    case DDERR_UNSUPPORTED:
	return "Unsupported.";
    case DDERR_UNSUPPORTEDFORMAT:
	return "Unsupported format.";

    case DDERR_WASSTILLDRAWING:
	return "Was still drawing.";
    }
    return "";
}

static int lockcnt = 0;

static int do_surfacelock (void)
{
    HRESULT ddrval = IDirectDrawSurface_Lock (lpDDS, NULL, &ddsd, DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT, NULL);
    if (ddrval != DD_OK) {
	if (ddrval == DDERR_SURFACELOST)
	    ddrval = IDirectDrawSurface_Restore (lpDDS);
	else if (ddrval != DDERR_SURFACEBUSY)
	    MyOutputDebugString ("lpDDS->Lock() failed - %s (%d)\n", getddrname (ddrval), (unsigned short) ddrval);
	return 0;
    }
    lockcnt++;
    return 1;
}

void unlockscr (void)
{
    lockcnt--;
    IDirectDrawSurface_Unlock (lpDDS, ddsd.lpSurface);
}

static void release_ddraw_stuff (void)
{
    if (lpDD) {
	IDirectDraw_RestoreDisplayMode (lpDD);
	IDirectDraw_SetCooperativeLevel (lpDD, hAmigaWnd, DDSCL_NORMAL);
    }
    if (lpDDC)
	IDirectDrawClipper_Release (lpDDC);
    if (lpDDS)
	IDirectDrawSurface_Release (lpDDS);
    if (lpDDP)
	IDirectDrawPalette_Release (lpDDP);
    if (lpDD)
	IDirectDraw_Release (lpDD);
    lpDDC = 0;
    lpDDS = 0;
    lpDDP = 0;
    lpDD = 0;
}

static int set_ddraw (int width, int height, int wantfull, int *pbytes,
		      LPPALETTEENTRY pal)
{
    int bits = 8 * *pbytes;
    HRESULT ddrval;

#ifdef __GNUC__
    ddrval = (*pDirectDrawCreate) (NULL, &lpDD, NULL);
#else
    ddrval = DirectDrawCreate (NULL, &lpDD, NULL);
#endif
    if (ddrval != DD_OK)
	goto oops;

    ddrval = IDirectDraw_SetCooperativeLevel (lpDD, hAmigaWnd,
					       (wantfull
						? DDSCL_ALLOWREBOOT | DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN
						: DDSCL_NORMAL));
    if (ddrval != DD_OK)
	goto oops;

    ddrval = IDirectDraw_CreateClipper (lpDD, 0, &lpDDC, NULL);
    if (ddrval != DD_OK) {
	MyOutputDebugString ("DirectDraw error: Clipper unavailable! (%d)\n", ddrval);
    } else {
	if (IDirectDrawClipper_SetHWnd (lpDDC, 0, hAmigaWnd) != DD_OK)
	    MyOutputDebugString ("DirectDraw error: Can't attach HWnd!\n");
    }

    if (wantfull) {
	ddrval = IDirectDraw_SetDisplayMode (lpDD, width, height, bits);
	if (ddrval != DD_OK)
	    goto oops;
    }
    ddsd.dwSize = sizeof (ddsd);
    ddsd.dwFlags = DDSD_CAPS;
    ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;

    ddrval = IDirectDraw_CreateSurface (lpDD, &ddsd, &lpDDS, NULL);
    if (ddrval != DD_OK)
	goto oops;

    if (! do_surfacelock ())
	return 0;
    unlockscr ();

    bits = (ddsd.ddpfPixelFormat.dwRGBBitCount + 7) & ~7;
    current_pixbytes = *pbytes = bits >> 3;

    if (bits == 8) {
	ddrval = IDirectDraw_CreatePalette (lpDD, DDPCAPS_8BIT | DDPCAPS_ALLOW256,
					     pal,
					     &lpDDP, NULL);
	if (ddrval != DD_OK)
	    goto oops;

	ddrval = IDirectDrawSurface2_SetPalette (lpDDS, lpDDP);
	if (ddrval != DD_OK)
	    goto oops;
    }
    /* Set global values for current width,height */
	current_width  = width;
	current_height = height;

    return 1;

oops:
    MyOutputDebugString ("DirectDraw initialization failed with %s/%d\n", getddrname (ddrval), ddrval);
    return 0;
}

/* Color management */
static int bits_in_mask (unsigned long mask)
{
    int n = 0;
    while (mask) {
	n += mask & 1;
	mask >>= 1;
    }
    return n;
}

static int mask_shift (unsigned long mask)
{
    int n = 0;
    while (!(mask & 1)) {
	n++;
	mask >>= 1;
    }
    return n;
}

static xcolnr xcol8[4096];
static PALETTEENTRY colors256[256];
static int ncols256 = 0;

static int get_color (int r, int g, int b, xcolnr *cnp)
{
    if (ncols256 == 256)
	return 0;
    colors256[ncols256].peRed = r * 0x11;
    colors256[ncols256].peGreen = g * 0x11;
    colors256[ncols256].peBlue = b * 0x11;
    colors256[ncols256].peFlags = 0;
    *cnp = ncols256;
    ncols256++;
    return 1;
}

static void init_colors (void)
{
    if (ncols256 == 0)
	alloc_colors256 (get_color);
    memcpy (xcol8, xcolors, sizeof xcol8);

    /* init colors */

    switch (current_pixbytes) {
     case 1:
	memcpy (xcolors, xcol8, sizeof xcolors);
	if (lpDDP != 0) {
	    HRESULT ddrval = IDirectDrawPalette_SetEntries (lpDDP, 0, 0, 256, colors256);
	    if (ddrval != DD_OK)
		MyOutputDebugString ("DX_SetPalette() failed with %s/%d\n", getddrname (ddrval), ddrval);
	}
	break;

     case 2:
     case 3:
     case 4:
	{
	    int red_bits = bits_in_mask (ddsd.ddpfPixelFormat.dwRBitMask);
	    int green_bits = bits_in_mask (ddsd.ddpfPixelFormat.dwGBitMask);
	    int blue_bits = bits_in_mask (ddsd.ddpfPixelFormat.dwBBitMask);
	    int red_shift = mask_shift (ddsd.ddpfPixelFormat.dwRBitMask);
	    int green_shift = mask_shift (ddsd.ddpfPixelFormat.dwGBitMask);
	    int blue_shift = mask_shift (ddsd.ddpfPixelFormat.dwBBitMask);

	    alloc_colors64k (red_bits, green_bits, blue_bits, red_shift,
			     green_shift, blue_shift);
	}
	break;
    }
}

static void close_windows (void)
{
    int i;

    gfxvidinfo.bufmem = 0;
    gfxvidinfo.linemem = 0;

    releasecapture ();
    setmouseactive (0);
    ClipCursor (NULL);
    release_ddraw_stuff ();

    if (!hMainWnd && hAmigaWnd) {
	/*ShowWindow (hAmigaWnd, SW_MINIMIZE);*/
    }
    if (hAmigaWnd)
	DestroyWindow (hAmigaWnd);
    if (hMainWnd)
	DestroyWindow (hMainWnd);
    PostQuitMessage (0);
    hMainWnd = 0;
    hAmigaWnd = 0;
}

static void destroy_window (int nowait)
{
    if (currprefs.produce_sound > 1)
	stopsound ();

    if (!nowait) {
	frame_do_semup = 1;
	set_inhibit_frame (5);
	uae_sem_wait (&frame_sem);
    }

    /*SuspendThread (hEmuThread);*/
    close_windows ();
}

void displaychange (void)
{
    changemode = 1;
    destroy_window (0);
}

void toggle_fullscreen (void)
{
    changemode = 1;
    if (screen_is_picasso)
	picasso_fullscreen ^= 1;
    else
	amiga_fullscreen ^= 1;
    destroy_window (0);
}

int can_draw (void)
{
    return gfxvidinfo.pixbytes != 0;
}

long FAR PASCAL AmigaWindowProc (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT ps;
    HDC hDC;

    if (changemode != 0)
	return DefWindowProc (hWnd, message, wParam, lParam);

    switch (message) {
    case WM_ACTIVATEAPP:
	if (bActive = wParam) {
	    if (fullscreen) {
		SetCursor (NULL);
		SetCursorPos (mousecx, mousecy);
#ifdef PICASSO96
        if( !picasso96_state.RefreshPending )
            picasso96_state.RefreshPending = 1;
#endif
	    }
	    my_kbd_handler (VK_CAPITAL, 0x3a, TRUE);
	} else {
	    if (!fullscreen)
		setmouseactive (0);
	}
	break;

    case WM_ACTIVATE:
	if (LOWORD (wParam) != WA_INACTIVE) {
	    ShowWindow (hWnd, SW_RESTORE);
	}
	break;

    case WM_SETCURSOR:
	if (fullscreen) {
	    SetCursor (NULL);
	    return TRUE;
	}
	break;

    case WM_SYSCOMMAND:
	if (wParam == SC_ZOOM) {
	    toggle_fullscreen ();
	    return 0;
	}
	break;

    case WM_KEYUP:
    case WM_SYSKEYUP:
	numkeysdown--;
	keysdown[wParam] = 0;
	my_kbd_handler (wParam, (lParam >> 16) & 0x1ff, FALSE);
	break;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
	if (LOWORD (lParam) == 1) {
	    if (numkeysdown) {
		int key;
		numkeysdown = 0;

		for (key = 256; key--;) {
		    if (keysdown[key]) {
			if (checkkey (key, lParam))
			    numkeysdown++;
			else {
			    my_kbd_handler (key, (keysdown[key] >> 16) & 0x1ff, FALSE);
			    keysdown[key] = 0;
			}
		    }
		}
	    }
	    if (!keysdown[wParam]) {
		keysdown[wParam] = lParam;
		numkeysdown++;
	    }
	    numkeysdown++;
	    my_kbd_handler (wParam, (lParam >> 16) & 0x1ff, TRUE);
	}
	break;

    case WM_LBUTTONDOWN:
	if (ievent_alive) {
	    setcapture ();
	    buttonstate[0] = 1;
	} else if (!fullscreen && !mouseactive)
	    setmouseactive (1);
	else
	    buttonstate[0] = 1;
	break;

    case WM_LBUTTONUP:
	releasecapture ();
	buttonstate[0] = 0;
	break;

    case WM_MBUTTONDOWN:
	if (ievent_alive)
	    setcapture ();
	buttonstate[1] = 1;
	break;

    case WM_MBUTTONUP:
	releasecapture ();
	buttonstate[1] = 0;
	break;

    case WM_RBUTTONDOWN:
	if (ievent_alive)
	    setcapture ();
	buttonstate[2] = 1;
	break;

    case WM_RBUTTONUP:
	releasecapture ();
	buttonstate[2] = 0;
	break;

    case WM_MOUSEMOVE:
	if ((mouseactive && !ievent_alive) || fullscreen) {
	    /*
	     * In this mode, the mouse pointer is always centered in the window,
	     * this is ensured by the SetCursorPos call below.
	     * We don't want to handle messages that result from such a SetCursorPos
	     * call (recursion!), so exit early if we see one.
	     */
	    if (lParam == mousecl)
		break;
	    lastmx += (signed short) LOWORD (lParam) - mousedx;
	    lastmy += (signed short) HIWORD (lParam) - mousedy;
	    if (ievent_alive) {
		if (lastmx < 0)
		    lastmx = 0;
		if (lastmx > current_width)
		    lastmx = current_width;
		if (lastmy < 0)
		    lastmy = 0;
		if (lastmy > current_height)
		    lastmy = current_height;

	    }
	    SetCursorPos (mousecx, mousecy);
	    break;
	}
	lastmx = (signed short) LOWORD (lParam);
	lastmy = (signed short) HIWORD (lParam);
	break;

    case WM_PAINT:
	hDC = BeginPaint (hWnd, &ps);
	clear_inhibit_frame (IHF_WINDOWHIDDEN);
	if (hBackDC)
	    BitBlt (hDC, ps.rcPaint.left, ps.rcPaint.top,
		    ps.rcPaint.right - ps.rcPaint.left,
		    ps.rcPaint.bottom - ps.rcPaint.top, hBackDC,
		    ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
	else
	    notice_screen_contents_lost ();

	EndPaint (hWnd, &ps);
	break;

    case WM_DROPFILES:
	if (DragQueryFile ((HDROP) wParam, (UINT) - 1, NULL, 0)) {
	    if (DragQueryFile ((HDROP) wParam, 0, NULL, 0) < 255)
		DragQueryFile ((HDROP) wParam, 0, changed_prefs.df[0], sizeof (changed_prefs.df[0]));
	}
	DragFinish ((HDROP) wParam);
	break;

    case WM_CAPTURECHANGED:
	if (lParam != hWnd)
	    buttonstate[0] = buttonstate[1] = buttonstate[2] = 0;
	break;

    case WM_TIMER:
#if 0
	if (wParam == 2) {
	    KillTimer (hwnd, timer_id);
	    timer_id = 0;
	    ClearVisibleAreaAndRefresh ();
	} else
#endif
	{
	    finishjob ();
	}
	break;

    case WM_USER + 0x200:
	DoSomeWeirdPrintingStuff(wParam);
	break;

    case WM_USER + 0x201:
	changemode = 2;
	destroy_window (1);
	break;

    case WM_CREATE:
	DragAcceptFiles (hWnd, TRUE);
	break;

    case WM_DESTROY:
	changemode = 3;
	destroy_window (0);
	break;
    }

    return DefWindowProc (hWnd, message, wParam, lParam);
}

long FAR PASCAL MainWindowProc (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT ps;
    RECT rc, rc2;
    HDC hDC;

    if (changemode != 0)
	return DefWindowProc (hWnd, message, wParam, lParam);

    switch (message) {
     case WM_LBUTTONDOWN:
     case WM_MOUSEMOVE:
     case WM_ACTIVATEAPP:
     case WM_ACTIVATE:
     case WM_SETCURSOR:
     case WM_SYSCOMMAND:
     case WM_KEYUP:
     case WM_SYSKEYUP:
     case WM_KEYDOWN:
     case WM_SYSKEYDOWN:
     case WM_LBUTTONUP:
     case WM_MBUTTONDOWN:
     case WM_MBUTTONUP:
     case WM_RBUTTONDOWN:
     case WM_RBUTTONUP:
     case WM_DROPFILES:
     case WM_CREATE:
     case WM_DESTROY:
     case WM_USER + 0x200:
     case WM_USER + 0x201:
	return AmigaWindowProc (hWnd, message, wParam, lParam);

     case WM_DISPLAYCHANGE:
	if (!fullscreen && !changemode && (wParam + 7) / 8 != current_pixbytes)
	    displaychange ();
	break;

     case WM_ENTERSIZEMOVE:
	if (!fullscreen) {
	    frame_do_semup = 1;
	    set_inhibit_frame (5);
	    uae_sem_wait (&frame_sem);
	}
	break;

     case WM_EXITSIZEMOVE:
	clear_inhibit_frame (5);
	/* fall through */

     case WM_WINDOWPOSCHANGED:
	GetWindowRect (hAmigaWnd, &amigawin_rect);

	if (!fullscreen && hAmigaWnd) {
	    if (current_pixbytes == 2) {
		if (amigawin_rect.left & 1) {
		    GetWindowRect (hMainWnd, &rc2);
		    if (1 /*!mon || rc2.left + 4 < GetSystemMetrics (SM_CXSCREEN)*/)
			MoveWindow (hMainWnd, rc2.left + 1, rc2.top,
				    rc2.right - rc2.left, rc2.bottom - rc2.top, TRUE);
		}
	    } else if (current_pixbytes == 3) {
		if (amigawin_rect.left >= 0) {
		    if (amigawin_rect.left & 3) {
			GetWindowRect (hMainWnd, &rc2);
			if (1 /*!mon || rc2.left + 4 < GetSystemMetrics (SM_CXSCREEN)*/)
			    MoveWindow (hMainWnd, rc2.left - amigawin_rect.left % 4, rc2.top,
					rc2.right - rc2.left, rc2.bottom - rc2.top, TRUE);
		    }
		}
	    }
	    setmouseactive (0);
	    return 0;
	}
	break;

     case WM_PAINT:
	hDC = BeginPaint (hWnd, &ps);
	GetClientRect (hWnd, &rc);
	DrawEdge (hDC, &rc, EDGE_SUNKEN, BF_RECT);

	EndPaint (hWnd, &ps);
	break;

     case WM_NCLBUTTONDBLCLK:
	if (wParam == HTCAPTION) {
	    toggle_fullscreen ();
	    return 0;
	}
    }

    return DefWindowProc (hWnd, message, wParam, lParam);
}

static int HasConsole;
static HANDLE stdouthandle;
static HANDLE debugfile;

/* Console Win32 helper routines */
void activate_debugger ();

static BOOL __stdcall ctrlchandler (DWORD type)
{
    SetConsoleCtrlHandler ((PHANDLER_ROUTINE) ctrlchandler, FALSE);

    if (type == CTRL_C_EVENT) {
	activate_debugger ();
	return TRUE;
    }
    return FALSE;
}

void setup_brkhandler (void)
{
    SetConsoleCtrlHandler ((PHANDLER_ROUTINE) ctrlchandler, TRUE);
}

void remove_brkhandler (void)
{
    SetConsoleCtrlHandler ((PHANDLER_ROUTINE) ctrlchandler, FALSE);
}

int PASCAL WinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine,
		    int nCmdShow)
{
    char *posn;
    int i;

    hInst = hInstance;

    debugfile = CreateFile ("outfile", GENERIC_WRITE, 0, 0, CREATE_ALWAYS,
			    0, NULL);

    for (i = 0; i < NUM_DRIVES; i++) {
	drives[i].path[0] = 0;
	drives[i].name[0] = 0;
	drives[i].rw = TRUE;
    }

    /* Get our executable's root-path */
    start_path = xmalloc(MAX_PATH);
    GetModuleFileName (hInst, start_path, MAX_PATH);
    if (posn = strrchr (start_path, '\\'))
	*posn = 0;


    HasConsole = 0;/*AllocConsole ();*/
    if (HasConsole)
	stdouthandle = GetStdHandle (STD_OUTPUT_HANDLE);

    write_log ("UAE " UAEWINVERSION " Win32/DirectX, release " UAEWINRELEASE "\n");
    strcpy (VersionStr, PROGNAME);
    write_log ("\n(c) 1995-1997 Bernd Schmidt   - Core UAE concept and implementation."
	       "\n(c) 1996-1997 Mathias Ortmann - Win32 port and enhancements."
	       "\n(c) 1996-1997 Brian King      - Picasso96 and AHI support, GUI.\n"
	       "\nPress F12 to show the Settings Dialog (GUI), Alt-F4 to quit.\n"
	       "\nhttp://www.informatik.tu-muenchen.de/~ortmann/uae/\n\n");

    currprefs.copper_pos = -1;
    /*initgfxspecs();*/
    if (initlibs ())
	real_main (__argc, __argv);
    cleanuplibs();

    if (HasConsole)
	FreeConsole ();

    return FALSE;
}

static int register_classes (void)
{
    WNDCLASS wc;

    wc.style = 0;
    wc.lpfnWndProc = AmigaWindowProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = 0;
    wc.hIcon = LoadIcon (GetModuleHandle (NULL), IDI_APPICON);
    wc.hCursor = LoadCursor (NULL, IDC_ARROW);
    wc.hbrBackground = GetStockObject (BLACK_BRUSH);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "AmigaPowah";
    if (!RegisterClass (&wc))
	return 0;

    wc.style = 0;
    wc.lpfnWndProc = MainWindowProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = 0;
    wc.hIcon = LoadIcon (GetModuleHandle (NULL), IDI_APPICON);
    wc.hCursor = LoadCursor (NULL, IDC_ARROW);
    wc.hbrBackground = GetStockObject (BLACK_BRUSH);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "PCsuxRox";
    if (!RegisterClass (&wc))
	return 0;
    return 1;
}

int graphics_setup (void)
{
    DWORD ddrval;

    if (!register_classes ())
	return 0;

    uae_sem_init (&picasso_switch_sem, 0, 0);
    return 1;
}

#define NORMAL_WINDOW_STYLE (WS_VISIBLE | WS_BORDER | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)

static int create_windows (void)
{
    POINT pt;

    if (!fullscreen) {
	RECT rc;

	rc.left = 0;
	rc.top = 0;
	rc.right = current_width;
	rc.bottom = current_height;
	rc.right += 4;
	rc.bottom += 4;
	AdjustWindowRect (&rc, NORMAL_WINDOW_STYLE, FALSE);

	hMainWnd = CreateWindowEx (WS_EX_ACCEPTFILES,
				   "PCsuxRox",
				   TITLETEXT,
				   NORMAL_WINDOW_STYLE,
				   CW_USEDEFAULT, CW_USEDEFAULT,
				   rc.right - rc.left, rc.bottom - rc.top,
				   NULL,
				   NULL,
				   0,
				   NULL);

	if (! hMainWnd)
	    return 0;
    } else
	hMainWnd = NULL;

    hAmigaWnd = CreateWindowEx (fullscreen ? WS_EX_TOPMOST : WS_EX_ACCEPTFILES,
				"AmigaPowah",
				PROGNAME,
				hMainWnd ? WS_VISIBLE | WS_CHILD : WS_VISIBLE | WS_POPUP,
				hMainWnd ? 2 : CW_USEDEFAULT, hMainWnd ? 2 : CW_USEDEFAULT,
				fullscreen ? GetSystemMetrics (SM_CXSCREEN) : current_width,
				fullscreen ? GetSystemMetrics (SM_CYSCREEN) : current_height,
				hMainWnd,
				NULL,
				0,
				NULL);

    if (!hAmigaWnd) {
	if (hMainWnd)
	    DestroyWindow (hMainWnd);
	return 0;
    }

    if (hMainWnd)
	UpdateWindow (hMainWnd);
    if (hAmigaWnd)
	UpdateWindow (hAmigaWnd);

    return 1;
}

void doPreInit( void )
{
    void *blah;
    DWORD ddrval;
#ifdef __GNUC__
    ddrval = (*pDirectDrawCreate)(NULL,&lpDD,NULL);
#else
    ddrval = DirectDrawCreate( NULL, &lpDD, NULL );
#endif
    if (ddrval == DD_OK)
    {
        //IDirectDraw2_QueryInterface( lpDD, &IID_IDirectDraw2, &blah );

        mode_count = DX_FillResolutions( &picasso96_pixel_format );

	    // If we've got no command-line arguments, then we use the integrated-GUI
	    if( ( __argc == 1 ) && 
	        ( GetSettings() == FALSE ) )
	    {
	        MyOutputDebugString("GetSettings() trying to quit...\n"); 
	        shutdownmain();
	    }
    }
}

BOOL doInit (void)
{
    if (! create_windows ())
	return 0;
#ifdef PICASSO96
    if (screen_is_picasso) {
	if (!set_ddraw (current_width, current_height, fullscreen, &picasso_vidinfo.depth,
			(LPPALETTEENTRY)&picasso96_state.CLUT))
	    goto oops;
	picasso_vidinfo.rowbytes = ddsd.lPitch;
    } else
#endif
    {
	gfxvidinfo.pixbytes = 2;
	if (!set_ddraw (current_width, current_height, fullscreen, &gfxvidinfo.pixbytes, colors256))
	    goto oops;
	gfxvidinfo.bufmem = 0;
	gfxvidinfo.linemem = 0;
	gfxvidinfo.maxblocklines = 0;
	gfxvidinfo.width = current_width;
	gfxvidinfo.height = current_height;
	gfxvidinfo.rowbytes = ddsd.lPitch;
	gfxvidinfo.can_double = 1;
    }

    if (fullscreen) {
	scrindirect = 0;
	gfxvidinfo.linemem = 0;
	mousecx = 160, mousecy = 100, mousedx = 160, mousedy = 100, mousecl = MAKELONG (160, 100);
    }

    if (! do_surfacelock ())
	goto oops;
    unlockscr ();

    if ((ddsd.ddpfPixelFormat.dwFlags & (DDPF_RGB | DDPF_PALETTEINDEXED8 | DDPF_RGBTOYUV)) != 0)
    {
	MyOutputDebugString ("%s mode (bits: %d, pixbytes: %d)\n", fullscreen ? "Full screen" : "Window",
			     ddsd.ddpfPixelFormat.dwRGBBitCount, current_pixbytes);
#ifdef PICASSO96
            if( ddsd.ddpfPixelFormat.dwRBitMask == 0x0000F800 )
                picasso96_pixel_format |= RGBFF_R5G6B5PC;
            else if( ddsd.ddpfPixelFormat.dwRBitMask == 0x00007C00 )
                picasso96_pixel_format |= RGBFF_R5G5B5PC;
            else if( ddsd.ddpfPixelFormat.dwBBitMask == 0x0000F800 )
                picasso96_pixel_format |= RGBFF_B5G6R5PC;
            else
                picasso96_pixel_format |= RGBFF_B5G5R5PC;
#endif
    } else {
	MyOutputDebugString ("Error: Unsupported pixel format - use a different screen mode\n");
	goto oops;
    }
#ifdef PICASSO96
    if (screen_is_picasso)
	DX_SetPalette (0, 256);
    else
#endif
	init_colors ();

    if (!fullscreen)
	MainWindowProc (0, WM_WINDOWPOSCHANGED, 0, 0);
    return 1;

oops:
    if (hMainWnd)
	DestroyWindow (hMainWnd);
    if (hAmigaWnd)
	DestroyWindow (hAmigaWnd);
    return 0;
}

struct myRGNDATA {
    RGNDATAHEADER rdh;
    RECT rects[640];	/* fixed buffers suck, but this is _very_ unlikely to overflow */
} ClipList = { {
    sizeof (ClipList), RDH_RECTANGLES, 0, 640 * sizeof (RECT)
} };


/* this is the way the display line is put to screen
 * if the display is not 16 bits deep or the window is not fully visible */
static void clipped_linetoscr (char *dst, char *src, int y)
{
    LPRECT lpRect = ClipList.rects;
    int i;

    switch (current_pixbytes) {
    case 1:
	for (i = ClipList.rdh.nCount; i--; lpRect++) {
	    if (y >= lpRect->top && y < lpRect->bottom) {
		memcpy (dst + lpRect->left, src + lpRect->left, lpRect->right);
	    }
	}
	break;

    case 2:
	for (i = ClipList.rdh.nCount; i--; lpRect++) {
	    if (y >= lpRect->top && y < lpRect->bottom) {
		memcpy (dst + lpRect->left*2, src + lpRect->left*2, lpRect->right*2);
	    }
	}
	break;

    case 3:
	for (i = ClipList.rdh.nCount; i--; lpRect++) {
	    if (y >= lpRect->top && y < lpRect->bottom) {
		memcpy (dst + lpRect->left*3, src + lpRect->left*3, lpRect->right*3);
	    }
	}
	break;

     case 4:
	for (i = ClipList.rdh.nCount; i--; lpRect++) {
	    if (y >= lpRect->top && y < lpRect->bottom) {
		memcpy (dst + lpRect->left*4, src + lpRect->left*4, lpRect->right*4);
	    }
	}
	break;
    }
}

void flush_line(int lineno)
{
    if (scrindirect)
	clipped_linetoscr(gfxvidinfo.bufmem + lineno * gfxvidinfo.rowbytes,
			  scrlinebuf, lineno);
}

void flush_block(int a, int b)
{
}

void flush_screen(int a, int b)
{
}

char *lockscr (void)
{
    char *surface, *oldsurface;
    DWORD tmp;
    LPRECT lpRect;

    if (!do_surfacelock ())
	return NULL;

    surface = ddsd.lpSurface;
    oldsurface = gfxvidinfo.bufmem;
    if (!fullscreen) {
	surface += amigawin_rect.top * ddsd.lPitch + current_pixbytes * amigawin_rect.left;
    }
    gfxvidinfo.bufmem = surface;
    if (surface != oldsurface && ! screen_is_picasso) {
	write_log ("Need to init_row_map\n");
	init_row_map ();
    }

    scrindirect = 0;

    if (fullscreen) {
	set_linemem ();
	clear_inhibit_frame (IHF_WINDOWHIDDEN);
	return surface;
    }

    tmp = sizeof (ClipList.rects);

    /* This is the VERY instruction that drags other threads (input/file system) down when in windowed
     * mode - WHY can't Microsoft implement the IsClipListChanged() method as documented? ARGH! */
    if (IDirectDrawClipper_GetClipList (lpDDC, NULL, (LPRGNDATA) & ClipList, &tmp) == DD_OK) {
	lpRect = ClipList.rects;

	if (!ClipList.rdh.nCount) {
	    write_log("ClipList empty\n");
	    set_inhibit_frame (IHF_WINDOWHIDDEN);
	    unlockscr ();
	    return 0;
	}

	if (ClipList.rdh.nCount != 1
	    || lpRect->right - lpRect->left != current_width
	    || lpRect->bottom - lpRect->top != current_height)
	{
	    scrindirect = 1;
	    for (tmp = ClipList.rdh.nCount; tmp--; lpRect++) {
		lpRect->left -= amigawin_rect.left;
		lpRect->right -= amigawin_rect.left;
		lpRect->top -= amigawin_rect.top;
		lpRect->bottom -= amigawin_rect.top;

		lpRect->right -= lpRect->left;
	    }
	}
    }

    set_linemem ();
    clear_inhibit_frame (IHF_WINDOWHIDDEN);
    return surface;
}

int graphics_init (void)
{
    static int first_time = 1;
    if (first_time)
	figure_processor_speed ();
    first_time = 0;
#ifdef PICASSO96
    if (screen_is_picasso) {
	fullscreen = picasso_fullscreen;
	current_width = picasso_vidinfo.width;
	current_height = picasso_vidinfo.height;
    } else 
#endif
    {
	fullscreen = amiga_fullscreen;
	current_width = currprefs.gfx_width;
	current_height = currprefs.gfx_height;

	if (!fullscreen) {
	    if (current_height > 290 << currprefs.gfx_linedbl)
		current_height = 290 << currprefs.gfx_linedbl;
	    if (current_width > 800 >> currprefs.gfx_lores)
		current_width = 800 >> currprefs.gfx_lores;
	}
	current_width = (current_width + 7) & ~7;
    }

    if (!doInit ())
	return 0;

    *xcolors = 0;

    return 1;
}

void graphics_leave (void)
{
    dumpcustom ();
}

void shutdownmain (void)
{
    changemode = 3;
    PostMessage (hAmigaWnd, WM_QUIT, 0, 0);
}

void workthread(void)
{
    if (use_debugger && debuggable ())
	activate_debugger ();

    do_start_program();
    shutdownmain();
}

void start_program (void)
{
    DWORD id;
    MSG msg;

    SetPriorityClass (GetCurrentProcess (), process_desired_pri);

    hEmuThread = CreateThread (NULL, 0, (LPTHREAD_START_ROUTINE) workthread, 0, CREATE_SUSPENDED, &id);
    SetThreadPriority (hEmuThread, THREAD_PRIORITY_BELOW_NORMAL);
    ResumeThread (hEmuThread);

    for (;;) {
	int cm;
	while (GetMessage (&msg, NULL, 0, 0)) {
	    TranslateMessage (&msg);
	    DispatchMessage (&msg);
	}
	cm = changemode;
	if (cm == 3)
	    break;

	changemode = 0;
	current_pixbytes = 0;
	if (!graphics_init ())
	    break;

	if (cm == 2) {
	    uae_sem_post (&picasso_switch_sem);
	}
	clear_inhibit_frame (5);
	notice_screen_contents_lost ();
	notice_new_xcolors ();

	notice_new_xcolors ();

	/*ResumeThread (hEmuThread);*/

	if (currprefs.produce_sound > 1)
	    startsound ();
    }
}

void handle_events (void)
{
}

#ifdef CONFIG_FOR_SMP
int uae_sem_initcs (LPCRITICAL_SECTION * cs)
{
    *cs = (LPCRITICAL_SECTION) GlobalAlloc (GPTR, sizeof csDraw);
    InitializeCriticalSection (*cs);
}

void uae_sem_enter (LPCRITICAL_SECTION cs)
{
    EnterCriticalSection (cs);
}

void uae_sem_leave (LPCRITICAL_SECTION cs)
{
    LeaveCriticalSection (cs);
}

void uae_sleep (int ms)
{
    Sleep (ms);
}

#endif

/* this is the semaphore for protecting the actual drawing against intermittent window movements */
void begindrawing (void)
{
    EnterCriticalSection (&csDraw);
}

void enddrawing (void)
{
    LeaveCriticalSection (&csDraw);
}

/* this truly sucks, I'll include a native gunzip() routine soon */
int gunzip_hack (const char *src, const char *dst)
{
    char buf[1024];
    STARTUPINFO si =
    {sizeof si};
    PROCESS_INFORMATION pi;

    strcpy (buf, dst);
    strcat (buf, ".gz");

    if (CopyFile (src, buf, FALSE)) {
	sprintf (buf, "gzip -f -d \"%s.gz\"", dst);
	si.dwFlags = STARTF_USESTDHANDLES;
	if (CreateProcess (NULL, buf, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
	    WaitForSingleObject (pi.hProcess, INFINITE);
	    return -1;
	} else {
	    MyOutputDebugString ("Error: You need gzip.exe (32 bit) to use .adz/.roz files!\n");
	}
    }
    return 0;
}

/* file name requester. hBackBM will hopefully be dropped in the near future. */
static OPENFILENAME ofn = {
    sizeof (OPENFILENAME),
    NULL, NULL, "Amiga Disk Files\000*.adf;*.adz\000",
    NULL, 0, 0, 0, 256, NULL, 0, "",
    0,
    OFN_NOCHANGEDIR | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
    0,
    0,
    "adf",
    0,
    NULL,
    NULL
};

int requestfname (char *title, char *name)
{
    HDC hDC;
    char *result;
    RECT rc;
#ifdef PICASSO96
    uae_u8 old_switch_state = picasso96_state.SwitchState;
#endif

    ofn.hwndOwner = hAmigaWnd;
    ofn.lpstrTitle = title;
    ofn.lpstrFile = name;

    if (pGetOpenFileNameA == 0)
	return 0;

    if (IDirectDrawSurface_GetDC (lpDDS, &hDC) == DD_OK) {
	if ((hBackBM = CreateCompatibleBitmap (hDC, currprefs.gfx_width, currprefs.gfx_height))
	    && (hBackDC = CreateCompatibleDC (hDC))) {
	    SelectObject (hBackDC, hBackBM);

	    if (fullscreen)
		BitBlt (hBackDC, 0, 0, currprefs.gfx_width, currprefs.gfx_height, hDC, 0, 0, SRCCOPY);
	    else {
		BitBlt (hBackDC, 0, 0, currprefs.gfx_width, currprefs.gfx_height, hDC,
			amigawin_rect.left, amigawin_rect.top, SRCCOPY);
	    }
	}
	IDirectDrawSurface_ReleaseDC (lpDDS, hDC);
    }
    if (!fullscreen)
	setmouseactive (0);

    if (title)
	result = (char *)((*pGetOpenFileNameA) (&ofn));
    else {
	GetSettings ();
    }

    if (!fullscreen || !mouseactive)
	SetCursor (NULL);

    if (hBackBM) {
	if (hBackDC) {
	    if (fullscreen && IDirectDrawSurface_GetDC (lpDDS, &hDC) == DD_OK) {
		BitBlt (hDC, 0, 0, currprefs.gfx_width, currprefs.gfx_height, hBackDC, 0, 0, SRCCOPY);
		IDirectDrawSurface_ReleaseDC (lpDDS, hDC);
	    }
	    DeleteObject (hBackDC);
	    hBackDC = NULL;
	}
	DeleteObject (hBackBM);
	hBackBM = NULL;
    }
    notice_screen_contents_lost ();

    if (result)
	return 1;

    return 0;
}

int DisplayGUI( void )
{
    HBITMAP hBackBM2 = NULL;
#ifdef PICASSO96
    BITMAPINFO bminfo = { sizeof( BITMAPINFOHEADER ), current_width, -current_height, 1, 8, BI_RGB, 0, 0, 0, 0, 0, &picasso96_state.CLUT };
#endif
	HDC hDC;
    void *blah;
	RECT rc;
    uae_u8 p96_8bit = 0;

    if (pGetOpenFileNameA)
	{
        if (IDirectDrawSurface_GetDC(lpDDS,&hDC) == DD_OK)
		{
#ifdef PICASSO96
            if( picasso96_state.SwitchState && ( picasso96_state.BytesPerPixel == 1 ) )
            {
                p96_8bit = 1;
                hBackBM2 = CreateDIBSection( hDC, &bminfo, DIB_PAL_COLORS, &blah, NULL, 0 );
            }
            else
#endif
                hBackBM = CreateCompatibleBitmap( hDC, current_width, current_height );

            if( (hBackBM2 || hBackBM ) && ( hBackDC = CreateCompatibleDC( hDC ) ) )
			{
#ifdef PICASSO96
                if( hBackBM2 )
                {
                    SelectObject( hBackDC, hBackBM2 );
                    hBackBM = CreateCompatibleBitmap( hDC, current_width, current_height );
                }
                else
#endif
                    SelectObject( hBackDC, hBackBM );

				if( fullscreen ) 
                    BitBlt( hBackDC, 0, 0, current_width, current_height, hDC, 0, 0, SRCCOPY );
				else
				{
					GetWindowRect(hAmigaWnd,&rc);
					BitBlt(hBackDC,0,0,current_width,current_height,hDC,rc.left,rc.top,SRCCOPY);
				}

                IDirectDrawSurface2_SetPalette( lpDDS, NULL );
                if( hBackBM2 )
                    BitBlt( hDC, 0, 0, current_width, current_height, hBackDC, 0, 0, SRCCOPY );
			}
			IDirectDrawSurface_ReleaseDC( lpDDS, hDC );
		}

		if (!fullscreen) setmouseactive(FALSE);

        GetSettings();
        if( lpDDP )
            IDirectDrawSurface2_SetPalette( lpDDS, lpDDP );
	
		if (!fullscreen || !mouseactive) SetCursor(NULL);

		if( hBackBM || hBackBM2 )
		{
			if( hBackDC )
			{
#ifdef PICASSO96
                if( p96_8bit )
                {
                    picasso96_state.RefreshPending = 1;
                }
                else
#endif
                if( fullscreen && IDirectDrawSurface_GetDC( lpDDS, &hDC ) == DD_OK )
				{
					BitBlt( hDC, 0, 0, current_width, current_height, hBackDC, 0, 0, SRCCOPY );
					IDirectDrawSurface_ReleaseDC( lpDDS, hDC );
				}
				DeleteObject( hBackDC );
				hBackDC = NULL;
			}
            if( hBackBM2 )
                DeleteObject( hBackBM2 );
            if( hBackBM )
            {
			    DeleteObject( hBackBM );
			    hBackBM = NULL;
            }
		}
	}
	return 0;
}

#ifdef __GNUC__
#undef WINAPI
#define WINAPI
#endif

static HINSTANCE hDDraw = NULL, hComDlg32 = NULL, hRichEdit = NULL;

int cleanuplibs(void)
{
    if (hRichEdit)
	FreeLibrary(hRichEdit);
    if (hDDraw)
	FreeLibrary(hDDraw);
    if (hComDlg32)
	FreeLibrary(hComDlg32);
    return 1;
}

/* try to load COMDLG32 and DDRAW, initialize csDraw, try to obtain the system clock frequency
 * from the registry, try to find out if we are running on a Pentium */
int initlibs (void)
{
    /* Make sure we do an InitCommonControls() to get some advanced controls */
    InitCommonControls ();

    if (hComDlg32 = LoadLibrary ("COMDLG32.DLL")) {
	pGetOpenFileNameA = (BOOL (WINAPI *) (LPOPENFILENAME)) GetProcAddress (hComDlg32, "GetOpenFileNameA");
    } else
	/* System administrator? ROFL! -- Bernd */
	MyOutputDebugString ("COMDLG32.DLL not available. Please contact your system administrator.\n");

    /* LoadLibrary the richedit control stuff */
    if ((hRichEdit = LoadLibrary ("RICHED32.DLL")) == NULL) {
	MyOutputDebugString ("RICHED32.DLL not available. Please contact your system administrator.\n");
    }
    if (hDDraw = LoadLibrary ("DDRAW.DLL")) {
#ifdef __GNUC__
	pDirectDrawCreate = (HRESULT (WINAPI *) (GUID FAR *, LPDIRECTDRAW FAR *, IUnknown FAR *)) GetProcAddress (hDDraw, "DirectDrawCreate");
#endif
	InitializeCriticalSection (&csDraw);

	process_desired_pri = IDLE_PRIORITY_CLASS;

	return 1;
    } else
	MyOutputDebugString ("You have to install DirectX on your system before you can use UAE.\nRefer to the documentation for further details.\n");

    return 0;
}

void MyOutputDebugString (char *format,...)
{
    char buffer[512];
    va_list parms;

    va_start (parms, format);
    vsprintf (buffer, format, parms);
    va_end (parms);
    write_log (buffer);
}

void write_log (const char *txt)
{
    DWORD numwritten;
#ifdef _DEBUG
    OutputDebugString( txt );
#endif
    fprintf (stderr, "%s", txt);
    /* if (HasConsole)
	WriteConsole (stdouthandle, txt, strlen (txt), &numwritten, NULL);*/
    WriteFile (debugfile, txt, strlen (txt), &numwritten, NULL);
}

int debuggable (void)
{
    return 1;
}

int needmousehack (void)
{
    return 1;
}

void LED (int on)
{
}


static uae_u16 *ppicasso_format;
static int picasso_modecount;

static HRESULT CALLBACK modesCallback (LPDDSURFACEDESC modeDesc, LPVOID context)
{
    int bpp;

    if (picasso_modecount >= MAX_PICASSO_MODES)
	return DDENUMRET_CANCEL;

    if (modeDesc->dwFlags & (DDSD_PIXELFORMAT | DDSD_WIDTH | DDSD_HEIGHT | DDSD_REFRESHRATE)) {
	bpp = modeDesc->ddpfPixelFormat.dwRGBBitCount;
#ifdef PICASSO96
	switch (bpp) {
	case 8:
	    *ppicasso_format |= RGBFF_CHUNKY;
	    break;
	case 16:
	    /* figure out which actual format it is using based on the RGB masks */

        /* Bernd - this doesn't work for some reason.  Instead, I figure out the modes in doInit(),
         *         where I have the surface's description and pixel formats.  Damned DirectDraw stuff!
         */
	    break;
	case 24:
	    /* figure out which actual format it is using based on the RGB masks */
	    if (modeDesc->ddpfPixelFormat.dwRBitMask == 0x00FF0000)
		*ppicasso_format |= RGBFF_B8G8R8;
	    else
		*ppicasso_format |= RGBFF_R8G8B8;
	    break;
	case 32:
	    /* figure out which actual format it is using based on the RGB masks */
	    if (modeDesc->ddpfPixelFormat.dwRBitMask == 0x00FF0000)
		*ppicasso_format |= RGBFF_B8G8R8A8;
	    else if (modeDesc->ddpfPixelFormat.dwRBitMask == 0x000000FF)
		*ppicasso_format |= RGBFF_R8G8B8A8;
	    else if (modeDesc->ddpfPixelFormat.dwBBitMask == 0xFF000000)
		*ppicasso_format |= RGBFF_A8R8G8B8;
	    else
		*ppicasso_format |= RGBFF_A8B8G8R8;
	    break;
	}
#endif
	DisplayModes[picasso_modecount].res.width = modeDesc->dwWidth;
	DisplayModes[picasso_modecount].res.height = modeDesc->dwHeight;
	DisplayModes[picasso_modecount].depth = bpp/8;
	if (modeDesc->dwRefreshRate)
    {
        sprintf(DisplayModes[picasso_modecount].name,"%dx%d, %d-bit, %d Hz",modeDesc->dwWidth,
            modeDesc->dwHeight, bpp, modeDesc->dwRefreshRate );
	    DisplayModes[picasso_modecount].refresh = modeDesc->dwRefreshRate;
    }
	else
    {
        sprintf(DisplayModes[picasso_modecount].name,"%dx%d, %d-bit",modeDesc->dwWidth,
            modeDesc->dwHeight, bpp );
	    DisplayModes[picasso_modecount].refresh = 75; /* fake a 75-Hz refresh rate... BLAH! */
    }
	picasso_modecount++;
    }
    return DDENUMRET_OK;
}

int DX_FillResolutions (uae_u16 *ppixel_format)
{
    static int from_preInit = 1;
    if( from_preInit )
    {
        picasso_modecount = 0;
        *ppixel_format = 0;
        ppicasso_format = ppixel_format;

        IDirectDraw_EnumDisplayModes (lpDD, 0, NULL, NULL, modesCallback);
        from_preInit = 0;
    }
    return picasso_modecount;
}

#ifdef PICASSO96

void DX_SetPalette (int start, int count)
{
    HRESULT ddrval;

    if (!screen_is_picasso)
	return;

    /* Set our DirectX palette here */
    if (lpDDP && (picasso96_state.BytesPerPixel == 1)) {
	/* For now, until I figure this out, just set the entire range of CLUT values */
	ddrval = IDirectDrawPalette_SetEntries (lpDDP, 0, 0, 256, (LPPALETTEENTRY) &picasso96_state.CLUT);
	if (ddrval != DD_OK)
	    MyOutputDebugString ("DX_SetPalette() failed with %s/%d\n", getddrname (ddrval), ddrval);
    } else
	MyOutputDebugString ("ERROR - DX_SetPalette() doesn't have palette, or isn't Chunky mode.\n");
}

void DX_Invalidate (int first, int last)
{
}

int DX_BitsPerCannon (void)
{
    return 8;
}

int DX_FillRect (uaecptr addr, uae_u16 X, uae_u16 Y, uae_u16 Width, uae_u16 Height, uae_u32 Pen, uae_u8 Bpp)
{
    return 0;
}

void gfx_set_picasso_state (int on)
{
    if (screen_is_picasso == on)
	return;
    screen_is_picasso = on;
    PostMessage (hAmigaWnd, WM_USER + 0x201, 0, 0);
    uae_sem_wait (&picasso_switch_sem);
}

void gfx_set_picasso_modeinfo (int w, int h, int depth)
{
    depth >>= 3;
    if (picasso_vidinfo.width == w
	&& picasso_vidinfo.height == h
	&& picasso_vidinfo.depth == depth)
	return;

    picasso_vidinfo.width = w;
    picasso_vidinfo.height = h;
    picasso_vidinfo.depth = depth;
    picasso_vidinfo.extra_mem = 1;

    if (screen_is_picasso) {
	PostMessage (hAmigaWnd, WM_USER + 0x201, 0, 0);
	uae_sem_wait (&picasso_switch_sem);
    }
}

#endif
