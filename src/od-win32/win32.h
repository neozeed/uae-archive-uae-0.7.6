/* 
  * UAE - The Un*x Amiga Emulator
  * 
  * Win32-specific header file
  * 
  * (c) 1997 Mathias Ortmann
  */

extern int atime, acount;

#ifndef __GNUC__ // BDK WAS HERE
struct utimbuf 
{
  int actime;
  int modtime; 
};
#endif

#define UAEWINVERSION_BASE "0.6.9h"
#define UAEWINRELEASE "1"
#if CPU_LEVEL >= 3
#define UAEWINCPULEVEL " 020/881"
#endif
#if CPU_LEVEL == 2
#define UAEWINCPULEVEL " 020"
#endif
#if CPU_LEVEL == 1
#define UAEWINCPULEVEL " 010"
#endif
#if CPU_LEVEL == 0
#define UAEWINCPULEVEL ""
#endif
#define UAEWINVERSION UAEWINVERSION_BASE UAEWINCPULEVEL
#define PROGNAME "UAE " UAEWINVERSION " Release " UAEWINRELEASE

#undef S_ISDIR
#define S_ISDIR(a) (a&0x100)

#define DIR struct DIR

#ifndef R_OK
#define R_OK 02
#define W_OK 04
#endif

extern DIR * opendir(char *);
extern struct dirent * readdir(DIR *);
extern void closedir(DIR *);

// win32glue.c functions
extern int my_kbd_handler(int, int, int);
extern void workthread(void);

// win32.c functions
extern char * lockscr(void);
extern void unlockscr(void);
extern int currtime(void);
extern void setup_brkhandler(void);
extern void remove_brkhandler(void);
extern void run_workthread(void);
extern void shutdownmain(void);
extern int getcapslock(void);

extern void clipped_linetoscr(char *, char *, int);

extern int helppressed(void);
extern int shiftpressed(void);
extern int checkkey(int vkey, long lParam);
extern void setmouseactive(int active);

extern int gunzip_hack(const char *src, const char *dst);

extern void begindrawing(void);
extern void enddrawing(void);

extern int requestfname(char *title, char *name);

extern void togglemouse(void);
extern void startsound(void);
extern void stopsound(void);

// globals
extern int bActive;
//extern int draw_all;

//extern int vpos;
//extern unsigned short lof;
extern int capslock;

extern int amiga_fullscreen, customsize;

extern int process_desired_pri;

extern int toggle_sound, bytesinbuf;

#define mode_t int

#ifndef __GNUC__ // BDK WAS HERE
#undef direct
#endif
struct direct
{
	char d_name[1];
};

#ifdef __GNUC__
#define off_t int
#endif

#define USE_ZFILE

int iswindowhidden(void);
void fname_wtoa(unsigned char *);
void fname_atow(char *, char *, int);
void MyOutputDebugString( char *format, ... );
extern int sound_available;
extern int framecnt;
extern int ievent_alive;
extern char prtname[];
extern char VersionStr[256];

