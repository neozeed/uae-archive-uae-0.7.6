 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Debugger
  *
  * (c) 1995 Bernd Schmidt
  *
  */

#include "sysconfig.h"
#include "sysdeps.h"

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
#include "debug.h"
#include "cia.h"
#include "xwin.h"
#include "gui.h"

static int debugger_active = 0;
static uaecptr skipaddr;
static int do_skip;
int debugging = 0;

void activate_debugger(void)
{
    do_skip = 0;
    if (debugger_active)
	return;
    debugger_active = 1;
    regs.spcflags |= SPCFLAG_BRK;
    debugging = 1;
    use_debugger = 1;
}

int firsthist = 0;
int lasthist = 0;
#ifdef NEED_TO_DEBUG_BADLY
struct regstruct history[MAX_HIST];
union flagu historyf[MAX_HIST];
#else
uaecptr history[MAX_HIST];
#endif

static void ignore_ws(char **c)
{
    while (**c && isspace(**c)) (*c)++;
}

static uae_u32 readhex(char **c)
{
    uae_u32 val = 0;
    char nc;

    ignore_ws(c);

    while (isxdigit(nc = **c)){
	(*c)++;
	val *= 16;
	nc = toupper(nc);
	if (isdigit(nc)) {
	    val += nc - '0';
	} else {
	    val += nc - 'A' + 10;
	}
    }
    return val;
}

static char next_char(char **c)
{
    ignore_ws(c);
    return *(*c)++;
}

static int more_params(char **c)
{
    ignore_ws(c);
    return (**c) != 0;
}

static void dumpmem(uaecptr addr, uaecptr *nxmem, int lines)
{
    broken_in = 0;
    for (;lines-- && !broken_in;){
	int i;
	printf("%08lx ", addr);
	for(i=0; i< 16; i++) {
	    printf("%04x ", get_word(addr)); addr += 2;
	}
	printf("\n");
    }
    *nxmem = addr;
}

static void foundmod (uae_u32 ptr, char *type)
{
    char name[21];
    uae_u8 *ptr2 = chipmemory + ptr;
    int i,length;

    printf ("Found possible %s module at 0x%lx.\n", type, ptr);
    memcpy (name, ptr2, 20);
    name[20] = '\0';

    /* Browse playlist */
    length = 0;
    for (i = 0x3b8; i < 0x438; i++)
	if (ptr2[i] > length)
	    length = ptr2[i];

    length = (length+1)*1024 + 0x43c;

    /* Add sample lengths */
    ptr2 += 0x2A;
    for(i = 0; i < 31; i++, ptr2 += 30)
	length += 2*((ptr2[0]<<8)+ptr2[1]);
    
    printf("Name \"%s\", Length 0x%lx bytes.\n", name, length);
}

static void modulesearch(void)
{
    uae_u8 *p = get_real_address (0);
    uae_u32 ptr;

    for (ptr = 0; ptr < chipmem_size - 40; ptr += 2, p += 2) {
	/* Check for Mahoney & Kaktus */
	/* Anyone got the format of old 15 Sample (SoundTracker)modules? */
	if (ptr >= 0x438 && p[0] == 'M' && p[1] == '.' && p[2] == 'K' && p[3] == '.')
	    foundmod (ptr - 0x438, "ProTracker (31 samples)");

	if (ptr >= 0x438 && p[0] == 'F' && p[1] == 'L' && p[2] == 'T' && p[3] == '4')
	    foundmod (ptr - 0x438, "Startrekker");

	if (strncmp ((char *)p, "SMOD", 4) == 0) {
	    printf ("Found possible FutureComposer 1.3 module at 0x%lx, length unknown.\n", ptr);
	}
	if (strncmp ((char *)p, "FC14", 4) == 0) {
	    printf ("Found possible FutureComposer 1.4 module at 0x%lx, length unknown.\n", ptr);
	}
	if (p[0] == 0x48 && p[1] == 0xe7 && p[4] == 0x61 && p[5] == 0
	    && p[8] == 0x4c && p[9] == 0xdf && p[12] == 0x4e && p[13] == 0x75
	    && p[14] == 0x48 && p[15] == 0xe7 && p[18] == 0x61 && p[19] == 0
	    && p[22] == 0x4c && p[23] == 0xdf && p[26] == 0x4e && p[27] == 0x75) {
	    printf ("Found possible Whittaker module at 0x%lx, length unknown.\n", ptr);
	}
	if (p[4] == 0x41 && p[5] == 0xFA) {
	    int i;

	    for (i = 0; i < 0x240; i += 2)
		if (p[i] == 0xE7 && p[i + 1] == 0x42 && p[i + 2] == 0x41 && p[i + 3] == 0xFA)
		    break;
	    if (i < 0x240) {
		uae_u8 *p2 = p + i + 4;
		for (i = 0; i < 0x30; i += 2)
		    if (p2[i] == 0xD1 && p2[i + 1] == 0xFA) {
			printf ("Found possible MarkII module at %lx, length unknown.\n", ptr);
		    }
	    }
		
	}
    }
}

void debug(void)
{
    char input[80],c;
    uaecptr nextpc,nxdis,nxmem;

    bogusframe = 1;

    if (do_skip && (m68k_getpc() != skipaddr/* || regs.a[0] != 0x1e558*/)) {
	regs.spcflags |= SPCFLAG_BRK;
	return;
    }
    do_skip = 0;

#ifdef NEED_TO_DEBUG_BADLY
    history[lasthist] = regs;
    historyf[lasthist] = regflags;
#else
    history[lasthist] = m68k_getpc();
#endif
    if (++lasthist == MAX_HIST) lasthist = 0;
    if (lasthist == firsthist) {
	if (++firsthist == MAX_HIST) firsthist = 0;
    }

    m68k_dumpstate(&nextpc);
    nxdis = nextpc; nxmem = 0;

    for(;;){
	char cmd,*inptr;

	printf(">");
	fflush (stdout);
	if (fgets(input, 80, stdin) == 0)
	    return;
	inptr = input;
	cmd = next_char(&inptr);
	switch(cmd){
	 case 'c': dumpcia(); dumpcustom(); break;
	 case 'r': m68k_dumpstate(&nextpc); break;
	 case 'M': modulesearch(); break;
	 case 'S':
	    {
		uae_u8 *memp;
		uae_u32 src, len;
		char *name;
		FILE *fp;

		if (!more_params (&inptr))
		    goto S_argh;

		name = inptr;
		while (*inptr != '\0' && !isspace (*inptr))
		    inptr++;
		if (!isspace (*inptr))
		    goto S_argh;

		*inptr = '\0';
		inptr++;
		if (!more_params (&inptr))
		    goto S_argh;
		src = readhex (&inptr);
		if (!more_params (&inptr))
		    goto S_argh;
		len = readhex (&inptr);
		if (! valid_address (src, len)) {
		    printf ("Invalid memory block\n");
		    break;
		}
		memp = get_real_address (src);
		fp = fopen (name, "w");
		if (fp == NULL) {
		    printf ("Couldn't open file\n");
		    break;
		}
		if (fwrite (memp, 1, len, fp) != len) {
		    printf ("Error writing file\n");
		}
		fclose (fp);
		break;

		S_argh:
		printf ("S command needs more arguments!\n");
		break;
	    }
	 case 'd':
	    {
		uae_u32 daddr;
		int count;

		if (more_params(&inptr))
		    daddr = readhex(&inptr);
		else
		    daddr = nxdis;
		if (more_params(&inptr))
		    count = readhex(&inptr);
		else
		    count = 10;
		m68k_disasm(daddr, &nxdis, count);
	    }
	    break;
	 case 't': regs.spcflags |= SPCFLAG_BRK; return;
	 case 'z':
	    skipaddr = nextpc;
	    do_skip = 1;
	    regs.spcflags |= SPCFLAG_BRK;
	    return;

	 case 'f':
	    skipaddr = readhex(&inptr);
	    do_skip = 1;
	    regs.spcflags |= SPCFLAG_BRK;
	    return;

	 case 'q': uae_quit();
	    debugger_active = 0;
	    debugging = 0;
	    return;

	 case 'g':
	    if (more_params (&inptr))
		m68k_setpc (readhex (&inptr));
	    fill_prefetch_0 ();
	    debugger_active = 0;
	    debugging = 0;
	    return;

	 case 'H':
	    {
		int count;
		int temp;
#ifdef NEED_TO_DEBUG_BADLY
		struct regstruct save_regs = regs;
		union flagu save_flags = regflags;
#endif

		if (more_params(&inptr))
		    count = readhex(&inptr);
		else
		    count = 10;
		if (count < 0)
		    break;
		temp = lasthist;
		while (count-- > 0 && temp != firsthist) {
		    if (temp == 0) temp = MAX_HIST-1; else temp--;
		}
		while (temp != lasthist) {
#ifdef NEED_TO_DEBUG_BADLY
		    regs = history[temp];
		    regflags = historyf[temp];
		    m68k_dumpstate(NULL);
#else
		    m68k_disasm(history[temp], NULL, 1);
#endif
		    if (++temp == MAX_HIST) temp = 0;
		}
#ifdef NEED_TO_DEBUG_BADLY
		regs = save_regs;
		regflags = save_flags;
#endif
	    }
	    break;
	 case 'm':
	    {
		uae_u32 maddr; int lines;
		if (more_params(&inptr))
		    maddr = readhex(&inptr);
		else
		    maddr = nxmem;
		if (more_params(&inptr))
		    lines = readhex(&inptr);
		else
		    lines = 16;
		dumpmem(maddr, &nxmem, lines);
	    }
	    break;
	  case 'h':
	  case '?':
	    {
		printf ("          HELP for UAE Debugger\n");
		printf ("         -----------------------\n\n");
		printf ("  g: <address>          Start execution at the current address or <address>\n");
		printf ("  c:                    Dump state of the CIA and custom chips\n");
		printf ("  r:                    Dump state of the CPU\n");
		printf ("  m <address> <lines>:  Memory dump starting at <address>\n");
		printf ("  d <address> <lines>:  Disassembly starting at <address>\n");
		printf ("  t:                    Step one instruction\n");
		printf ("  z:                    Step through one instruction - useful for JSR, DBRA etc\n");
		printf ("  f <address>:          Step forward until PC == <address>\n");
		printf ("  H <count>:            Show PC history <count> instructions\n");
		printf ("  M:                    Search for *Tracker sound modules\n");
		printf ("  S <file> <addr> <n>:  Save a block of Amiga memory\n");
		printf ("  h,?:                  Show this help page\n");
		printf ("  q:                    Quit the emulator. You don't want to use this command.\n\n");
	    }
	    break;

	}
    }
}
