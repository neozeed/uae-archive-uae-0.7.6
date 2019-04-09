 /* 
  * UAE - The Un*x Amiga Emulator
  *
  * AutoConfig (tm) Expansions (ZorroII/III)
  *
  * Copyright 1996,1997 Stefan Reinauer <stepan@linux.de>
  * Copyright 1997 Brian King <Brian_King@Mitel.com>
  *   - added gfxcard code
  *
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "options.h"
#include "uae.h"
#include "memory.h"
#include "autoconf.h"
#include "picasso96.h"

#define MAX_EXPANSION_BOARDS	8

/* ********************************************************** */
/* 00 / 02 */
/* er_Type */

#define Z2_MEM_8MB	0x00 /* Size of Memory Block */
#define Z2_MEM_4MB	0x07
#define Z2_MEM_2MB	0x06
#define Z2_MEM_1MB	0x05
#define Z2_MEM_512KB	0x04
#define Z2_MEM_256KB	0x03
#define Z2_MEM_128KB	0x02
#define Z2_MEM_64KB	0x01
/* extended definitions */
#define Z2_MEM_16MB	0x00
#define Z2_MEM_32MB	0x01
#define Z2_MEM_64MB	0x02
#define Z2_MEM_128MB	0x03
#define Z2_MEM_256MB	0x04
#define Z2_MEM_512MB	0x05
#define Z2_MEM_1GB	0x06

#define chainedconfig	0x08 /* Next config is part of the same card */
#define rom_card	0x10 /* ROM vector is valid */
#define add_memory	0x20 /* Link RAM into free memory list */

#define zorroII		0xc0 /* Type of Expansion Card */
#define zorroIII	0x80

/* ********************************************************** */
/* 04 - 06 & 10-16 */

/* Manufacturer */
#define commodore_g	 513 /* Commodore Braunschweig (Germany) */
#define commodore	 514 /* Commodore West Chester */
#define gvp		2017 /* GVP */
#define ass		2102 /* Advanced Systems & Software */
#define hackers_id	2011 /* Special ID for test cards */

/* Card Type */
#define commodore_a2091	     3 /* A2091 / A590 Card from C= */
#define commodore_a2091_ram 10 /* A2091 / A590 Ram on HD-Card */
#define commodore_a2232	    70 /* A2232 Multiport Expansion */
#define ass_nexus_scsi	     1 /* Nexus SCSI Controller */

#define gvp_series_2_scsi   11
#define gvp_iv_24_gfx	    32

/* ********************************************************** */
/* 08 - 0A  */
/* er_Flags */
#define Z3_MEM_64KB	0x02
#define Z3_MEM_128KB	0x03
#define Z3_MEM_256KB	0x04
#define Z3_MEM_512KB	0x05
#define Z3_MEM_1MB	0x06 /* Zorro III card subsize */
#define Z3_MEM_2MB	0x07
#define Z3_MEM_4MB	0x08
#define Z3_MEM_6MB	0x09
#define Z3_MEM_8MB	0x0a
#define Z3_MEM_10MB	0x0b
#define Z3_MEM_12MB	0x0c
#define Z3_MEM_14MB	0x0d
#define Z3_MEM_16MB	0x00
#define Z3_MEM_AUTO	0x01
#define Z3_MEM_defunct1	0x0e
#define Z3_MEM_defunct2	0x0f

#define force_z3	0x10 /* *MUST* be set if card is Z3 */
#define ext_size	0x20 /* Use extended size table for bits 0-2 of er_Type */
#define no_shutup	0x40 /* Card cannot receive Shut_up_forever */
#define care_addr	0x80 /* Adress HAS to be $200000-$9fffff */

/* ********************************************************** */
/* 40-42 */
/* ec_interrupt (unused) */

#define enable_irq	0x01 /* enable Interrupt */
#define reset_card	0x04 /* Reset of Expansion Card - must be 0 */
#define card_int2	0x10 /* READ ONLY: IRQ 2 active */
#define card_irq6	0x20 /* READ ONLY: IRQ 6 active */
#define card_irq7	0x40 /* READ ONLY: IRQ 7 active */
#define does_irq	0x80 /* READ ONLY: Card currently throws IRQ */

/* ********************************************************** */

/* ROM defines (DiagVec) */

#define rom_4bit	(0x00<<14) /* ROM width */
#define rom_8bit	(0x01<<14)
#define rom_16bit	(0x02<<14)

#define rom_never	(0x00<<12) /* Never run Boot Code */
#define rom_install	(0x01<<12) /* run code at install time */
#define rom_binddrv	(0x02<<12) /* run code with binddrivers */

uaecptr ROM_filesys_resname = 0, ROM_filesys_resid = 0;
uaecptr ROM_filesys_diagentry = 0;
uaecptr ROM_hardfile_resname = 0, ROM_hardfile_resid = 0;
uaecptr ROM_hardfile_init = 0;

/* ********************************************************** */

static void (*card_init[MAX_EXPANSION_BOARDS]) (void);
static void (*card_map[MAX_EXPANSION_BOARDS]) (void);

static int ecard = 0;

/* ********************************************************** */

/* Please note: ZorroIII implementation seems to work different
 * than described in the HRM. This claims that ZorroIII config
 * address is 0xff000000 while the ZorroII config space starts
 * at 0x00e80000. In reality, both, Z2 and Z3 cards are 
 * configured in the ZorroII config space. Kickstart 3.1 doesn't
 * even do a single read or write access to the ZorroIII space.
 * The original Amiga include files tell the same as the HRM.
 * ZorroIII: If you set ext_size in er_Flags and give a Z2-size
 * in er_Type you can very likely add some ZorroII address space
 * to a ZorroIII card on a real Amiga. This is not implemented
 * yet.
 *  -- Stefan
 * 
 * Surprising that 0xFF000000 isn't used. Maybe it depends on the
 * ROM. Anyway, the HRM says that Z3 cards may appear in Z2 config
 * space, so what we are doing here is correct.
 *  -- Bernd
 */

/* Autoconfig address space at 0xE80000 */
static uae_u8 expamem[65536];

static uae_u8 expamem_lo;
static uae_u16 expamem_hi;

/*
 *  Dummy entries to show that there's no card in a slot
 */

static void expamem_map_clear (void)
{
    fprintf (stderr, "expamem_map_clear() got called. Shouldn't happen.\n");
}

static void expamem_init_clear (void)
{
    memset (expamem, 0xff, sizeof expamem);
}

static uae_u32 expamem_lget(uaecptr) REGPARAM;
static uae_u32 expamem_wget(uaecptr) REGPARAM;
static uae_u32 expamem_bget(uaecptr) REGPARAM;
static void expamem_lput(uaecptr, uae_u32) REGPARAM;
static void expamem_wput(uaecptr, uae_u32) REGPARAM;
static void expamem_bput(uaecptr, uae_u32) REGPARAM;

addrbank expamem_bank = {
    expamem_lget, expamem_wget, expamem_bget,
    expamem_lput, expamem_wput, expamem_bput,
    default_xlate, default_check
};

static uae_u32 REGPARAM2 expamem_lget(uaecptr addr)
{
    sprintf (warning_buffer, "warning: READ.L from address $%lx \n", addr);
    write_log (warning_buffer);
    return 0xfffffffful;
}

static uae_u32 REGPARAM2 expamem_wget(uaecptr addr)
{
    sprintf (warning_buffer, "warning: READ.W from address $%lx \n", addr);
    write_log (warning_buffer);
    return 0xffff;
}

static uae_u32 REGPARAM2 expamem_bget(uaecptr addr)
{
    uae_u8 value;
    addr &= 0xFFFF;
    return expamem[addr];
}

static void REGPARAM2 expamem_write (uaecptr addr, uae_u32 value)
{
    addr &= 0xffff;
    if (addr == 00 || addr == 02 || addr == 0x40 || addr == 0x42) {
	expamem[addr] = (value & 0xf0);
	expamem[addr + 2] = (value & 0x0f) << 4;
    } else {
	expamem[addr] = ~(value & 0xf0);
	expamem[addr + 2] = ~((value & 0x0f) << 4);
    }
}

static int REGPARAM2 expamem_type(void)
{
    return ((expamem[0] | expamem[2] >> 4) & 0xc0);
}

static void REGPARAM2 expamem_lput(uaecptr addr, uae_u32 value)
{
    fprintf (stderr, "warning: WRITE.L to address $%lx : value $%lx\n", addr, value);
}

static void REGPARAM2 expamem_wput(uaecptr addr, uae_u32 value)
{
    static char buffer[80];

    if (expamem_type() != zorroIII)
	fprintf (stderr, "warning: WRITE.W to address $%lx : value $%x\n", addr, value);
    else {
	switch (addr & 0xff) {
	 case 0x44:
	    if (expamem_type() == zorroIII) {
		expamem_hi = value;
		(*card_map[ecard]) ();
		sprintf (buffer, "   Card %d (Zorro%s) done.\n", ecard + 1, expamem_type() == 0xc0 ? "II" : "III");
		write_log (buffer);
		++ecard;
		if (ecard <= MAX_EXPANSION_BOARDS)
		    (*card_init[ecard]) ();
		else
		    expamem_init_clear ();
	    }
	    break;
	}
    }
}

static void REGPARAM2 expamem_bput(uaecptr addr, uae_u32 value)
{
    static char buffer[80];

    switch (addr & 0xff) {
     case 0x30:
     case 0x32:
	expamem_hi = 0;
	expamem_lo = 0;
	expamem_write (0x48, 0x00);
	break;

     case 0x48:
	if (expamem_type () == zorroII) {
	    expamem_hi = value & 0xFF;
	    (*card_map[ecard]) ();
	    sprintf (buffer, "   Card %d (Zorro%s) done.\n", ecard + 1, expamem_type() == 0xc0 ? "II" : "III");
	    write_log (buffer);
	    ++ecard;
	    if (ecard <= MAX_EXPANSION_BOARDS)
		(*card_init[ecard]) ();
	    else
		expamem_init_clear ();
	} else if (expamem_type() == zorroIII)
	    expamem_lo = value;
	break;

     case 0x4a:
	if (expamem_type () == zorroII)
	    expamem_lo = value;
	break;

     case 0x4c:
	sprintf (buffer, "   Card %d (Zorro %s) had no success.\n", ecard + 1, expamem_type() == 0xc0 ? "II" : "III");
	write_log (buffer);
	++ecard;
	if (ecard <= MAX_EXPANSION_BOARDS)
	    (*card_init[ecard]) ();
	else
	    expamem_init_clear ();
	break;
    }
}

/* ********************************************************** */

/*
 *  Fast Memory
 */

static uae_u32 fastmem_mask;

static uae_u32 fastmem_lget(uaecptr) REGPARAM;
static uae_u32 fastmem_wget(uaecptr) REGPARAM;
static uae_u32 fastmem_bget(uaecptr) REGPARAM;
static void fastmem_lput(uaecptr, uae_u32) REGPARAM;
static void fastmem_wput(uaecptr, uae_u32) REGPARAM;
static void fastmem_bput(uaecptr, uae_u32) REGPARAM;
static int fastmem_check(uaecptr addr, uae_u32 size) REGPARAM;
static uae_u8 *fastmem_xlate(uaecptr addr) REGPARAM;

static uae_u32 fastmem_start; /* Determined by the OS */
static uae_u8 *fastmemory = NULL;

uae_u32 REGPARAM2 fastmem_lget(uaecptr addr)
{
    uae_u8 *m;
    addr -= fastmem_start & fastmem_mask;
    addr &= fastmem_mask;
    m = fastmemory + addr;
    return do_get_mem_long ((uae_u32 *)m);
}

uae_u32 REGPARAM2 fastmem_wget(uaecptr addr)
{
    uae_u8 *m;
    addr -= fastmem_start & fastmem_mask;
    addr &= fastmem_mask;
    m = fastmemory + addr;
    return do_get_mem_word ((uae_u16 *)m);
}

uae_u32 REGPARAM2 fastmem_bget(uaecptr addr)
{
    addr -= fastmem_start & fastmem_mask;
    addr &= fastmem_mask;
    return fastmemory[addr];
}

void REGPARAM2 fastmem_lput(uaecptr addr, uae_u32 l)
{
    uae_u8 *m;
    addr -= fastmem_start & fastmem_mask;
    addr &= fastmem_mask;
    m = fastmemory + addr;
    do_put_mem_long ((uae_u32 *)m, l);
}

void REGPARAM2 fastmem_wput(uaecptr addr, uae_u32 w)
{
    uae_u8 *m;
    addr -= fastmem_start & fastmem_mask;
    addr &= fastmem_mask;
    m = fastmemory + addr;
    do_put_mem_word ((uae_u16 *)m, w);
}

void REGPARAM2 fastmem_bput(uaecptr addr, uae_u32 b)
{
    addr -= fastmem_start & fastmem_mask;
    addr &= fastmem_mask;
    fastmemory[addr] = b;
}

static int REGPARAM2 fastmem_check(uaecptr addr, uae_u32 size)
{
    addr -= fastmem_start & fastmem_mask;
    addr &= fastmem_mask;
    return (addr + size) < fastmem_size;
}

static uae_u8 REGPARAM2 *fastmem_xlate(uaecptr addr)
{
    addr -= fastmem_start & fastmem_mask;
    addr &= fastmem_mask;
    return fastmemory + addr;
}

addrbank fastmem_bank = {
    fastmem_lget, fastmem_wget, fastmem_bget,
    fastmem_lput, fastmem_wput, fastmem_bput,
    fastmem_xlate, fastmem_check
};


/*
 * Filesystem device ROM
 * This is very simple, the Amiga shouldn't be doing things with it.
 */

static uae_u32 filesys_lget(uaecptr) REGPARAM;
static uae_u32 filesys_wget(uaecptr) REGPARAM;
static uae_u32 filesys_bget(uaecptr) REGPARAM;
static void filesys_lput(uaecptr, uae_u32) REGPARAM;
static void filesys_wput(uaecptr, uae_u32) REGPARAM;
static void filesys_bput(uaecptr, uae_u32) REGPARAM;

static uae_u32 filesys_start; /* Determined by the OS */
uae_u8 filesysory[65536];

uae_u32 REGPARAM2 filesys_lget(uaecptr addr)
{
    uae_u8 *m;
    addr -= filesys_start & 65535;
    addr &= 65535;
    m = filesysory + addr;
    return do_get_mem_long ((uae_u32 *)m);
}

uae_u32 REGPARAM2 filesys_wget(uaecptr addr)
{
    uae_u8 *m;
    addr -= filesys_start & 65535;
    addr &= 65535;
    m = filesysory + addr;
    return do_get_mem_word ((uae_u16 *)m);
}

uae_u32 REGPARAM2 filesys_bget(uaecptr addr)
{
    addr -= filesys_start & 65535;
    addr &= 65535;
    return filesysory[addr];
}

static void REGPARAM2 filesys_lput(uaecptr addr, uae_u32 l)
{
    write_log ("filesys_lput called\n");
}

static void REGPARAM2 filesys_wput(uaecptr addr, uae_u32 w)
{
    write_log ("filesys_wput called\n");
}

static void REGPARAM2 filesys_bput(uaecptr addr, uae_u32 b)
{
    fprintf (stderr, "filesys_bput called. This usually means that you are using\n");
    fprintf (stderr, "Kickstart 1.2. Please give UAE the \"-a\" option next time\n");
    fprintf (stderr, "you start it. If you are _not_ using Kickstart 1.2, then\n");
    fprintf (stderr, "there's a bug somewhere.\n");
    fprintf (stderr, "Exiting...\n");
    uae_quit ();
}

addrbank filesys_bank = {
    filesys_lget, filesys_wget, filesys_bget,
    filesys_lput, filesys_wput, filesys_bput,
    default_xlate, default_check
};

/*
 *  Z3fastmem Memory
 */


static uae_u32 z3fastmem_mask;

static uae_u32 z3fastmem_lget(uaecptr) REGPARAM;
static uae_u32 z3fastmem_wget(uaecptr) REGPARAM;
static uae_u32 z3fastmem_bget(uaecptr) REGPARAM;
static void z3fastmem_lput(uaecptr, uae_u32) REGPARAM;
static void z3fastmem_wput(uaecptr, uae_u32) REGPARAM;
static void z3fastmem_bput(uaecptr, uae_u32) REGPARAM;
static int z3fastmem_check(uaecptr addr, uae_u32 size) REGPARAM;
static uae_u8 *z3fastmem_xlate(uaecptr addr) REGPARAM;

static uae_u32 z3fastmem_start; /* Determined by the OS */
static uae_u8 *z3fastmem = NULL;

uae_u32 REGPARAM2 z3fastmem_lget(uaecptr addr)
{
    uae_u8 *m;
    addr -= z3fastmem_start & z3fastmem_mask;
    addr &= z3fastmem_mask;
    m = z3fastmem + addr;
    return do_get_mem_long ((uae_u32 *)m);
}

uae_u32 REGPARAM2 z3fastmem_wget(uaecptr addr)
{
    uae_u8 *m;
    addr -= z3fastmem_start & z3fastmem_mask;
    addr &= z3fastmem_mask;
    m = z3fastmem + addr;
    return do_get_mem_word ((uae_u16 *)m);
}

uae_u32 REGPARAM2 z3fastmem_bget(uaecptr addr)
{
    addr -= z3fastmem_start & z3fastmem_mask;
    addr &= z3fastmem_mask;
    return z3fastmem[addr];
}

void REGPARAM2 z3fastmem_lput(uaecptr addr, uae_u32 l)
{
    uae_u8 *m;
    addr -= z3fastmem_start & z3fastmem_mask;
    addr &= z3fastmem_mask;
    m = z3fastmem + addr;
    do_put_mem_long ((uae_u32 *)m, l);
}

void REGPARAM2 z3fastmem_wput(uaecptr addr, uae_u32 w)
{
    uae_u8 *m;
    addr -= z3fastmem_start & z3fastmem_mask;
    addr &= z3fastmem_mask;
    m = z3fastmem + addr;
    do_put_mem_word ((uae_u16 *)m, w);
}

void REGPARAM2 z3fastmem_bput(uaecptr addr, uae_u32 b)
{
    addr -= z3fastmem_start & z3fastmem_mask;
    addr &= z3fastmem_mask;
    z3fastmem[addr] = b;
}

static int REGPARAM2 z3fastmem_check(uaecptr addr, uae_u32 size)
{
    addr -= z3fastmem_start & z3fastmem_mask;
    addr &= z3fastmem_mask;
    return (addr + size) < z3fastmem_size;
}

static uae_u8 REGPARAM2 *z3fastmem_xlate(uaecptr addr)
{
    addr -= z3fastmem_start & z3fastmem_mask;
    addr &= z3fastmem_mask;
    return z3fastmem + addr;
}

addrbank z3fastmem_bank = {
    z3fastmem_lget, z3fastmem_wget, z3fastmem_bget,
    z3fastmem_lput, z3fastmem_wput, z3fastmem_bput,
    z3fastmem_xlate, z3fastmem_check
};

/* Z3-based UAEGFX-card */
uae_u32 gfxmem_mask; /* for memory.c */
uae_u8 *gfxmemory;
uae_u32 gfxmem_start;

/* ********************************************************** */

/*
 *     Expansion Card (ZORRO II) for 1/2/4/8 MB of Fast Memory
 */

static void expamem_map_fastcard (void)
{
    char buffer[80];
    fastmem_start = ((expamem_hi | (expamem_lo >> 4)) << 16);
    map_banks (&fastmem_bank, fastmem_start >> 16, fastmem_size >> 16);
    sprintf (buffer, "Fastcard: mapped @$%lx: %dMB fast memory\n", fastmem_start, fastmem_size >> 20);
    write_log (buffer);
}

static void expamem_init_fastcard (void)
{
    expamem_init_clear();
    if (fastmem_size == 0x100000)
	expamem_write (0x00, Z2_MEM_1MB + add_memory + zorroII);
    else if (fastmem_size == 0x200000)
	expamem_write (0x00, Z2_MEM_2MB + add_memory + zorroII);
    else if (fastmem_size == 0x400000)
	expamem_write (0x00, Z2_MEM_4MB + add_memory + zorroII);
    else if (fastmem_size == 0x800000)
	expamem_write (0x00, Z2_MEM_8MB + add_memory + zorroII);

    expamem_write (0x08, care_addr);

    expamem_write (0x04, 1);

    expamem_write (0x10, hackers_id >> 8);
    expamem_write (0x14, hackers_id & 0xff);

    expamem_write (0x18, 0x00); /* ser.no. Byte 0 */
    expamem_write (0x1c, 0x00); /* ser.no. Byte 1 */
    expamem_write (0x20, 0x00); /* ser.no. Byte 2 */
    expamem_write (0x24, 0x01); /* ser.no. Byte 3 */

    expamem_write (0x28, 0x00); /* Rom-Offset hi */
    expamem_write (0x2c, 0x00); /* ROM-Offset lo */

    expamem_write (0x40, 0x00); /* Ctrl/Statusreg.*/
}

/* ********************************************************** */

/* 
 * Filesystem device
 */

static void expamem_map_filesys (void)
{
    uaecptr a;

    filesys_start = ((expamem_hi | (expamem_lo >> 4)) << 16);
    map_banks (&filesys_bank, filesys_start >> 16, 1);
    write_log ("Filesystem: mapped memory.\n");
    /* 68k code needs to know this. */
    a = here ();
    org (0xF0FFFC);
    dl (filesys_start + 0x2000);
    org (a);
}

static void expamem_init_filesys (void)
{
    uae_u8 diagarea[] = { 0x90, 0x00, 0x01, 0x0C, 0x01, 0x00, 0x01, 0x06 };

    expamem_init_clear();
    expamem_write (0x00, Z2_MEM_64KB | rom_card | zorroII);

    expamem_write (0x08, no_shutup);

    expamem_write (0x04, 2);
    expamem_write (0x10, hackers_id >> 8);
    expamem_write (0x14, hackers_id & 0xff);

    expamem_write (0x18, 0x00); /* ser.no. Byte 0 */
    expamem_write (0x1c, 0x00); /* ser.no. Byte 1 */
    expamem_write (0x20, 0x00); /* ser.no. Byte 2 */
    expamem_write (0x24, 0x01); /* ser.no. Byte 3 */

    expamem_write (0x28, 0x10); /* Rom-Offset hi */
    expamem_write (0x2c, 0x00); /* ROM-Offset lo */

    expamem_write (0x40, 0x00); /* Ctrl/Statusreg.*/

    /* Build a DiagArea */
    memcpy (expamem + 0x1000, diagarea, sizeof diagarea);

    /* Call DiagEntry */
    do_put_mem_word ((uae_u16 *)(expamem + 0x1100), 0x4EF9); /* JMP */
    do_put_mem_long ((uae_u32 *)(expamem + 0x1102), ROM_filesys_diagentry);

    /* What comes next is a plain bootblock */
    do_put_mem_word ((uae_u16 *)(expamem + 0x1106), 0x4EF9); /* JMP */
    do_put_mem_long ((uae_u32 *)(expamem + 0x1108), EXPANSION_bootcode);

    memcpy (filesysory, expamem, 0x3000);
}

/*
 * Zorro III expansion memory
 */

static void expamem_map_z3fastmem (void)
{
    char buffer[80];
    z3fastmem_start = ((expamem_hi | (expamem_lo >> 4)) << 16);
    map_banks (&z3fastmem_bank, z3fastmem_start >> 16, z3fastmem_size >> 16);

    sprintf (buffer, "Fastmem (32bit): mapped @$%lx: %d MB Zorro III fast memory \n",
	     z3fastmem_start, z3fastmem_size / 0x100000);
    write_log (buffer);
}

static void expamem_init_z3fastmem (void)
{
    int code = (z3fastmem_size == 0x100000 ? Z2_MEM_1MB
		: z3fastmem_size == 0x200000 ? Z2_MEM_2MB
		: z3fastmem_size == 0x400000 ? Z2_MEM_4MB
		: z3fastmem_size == 0x800000 ? Z2_MEM_8MB
		: z3fastmem_size == 0x1000000 ? Z2_MEM_16MB
		: z3fastmem_size == 0x2000000 ? Z2_MEM_32MB
		: z3fastmem_size == 0x4000000 ? Z2_MEM_64MB
		: z3fastmem_size == 0x8000000 ? Z2_MEM_128MB
		: z3fastmem_size == 0x10000000 ? Z2_MEM_256MB
		: z3fastmem_size == 0x20000000 ? Z2_MEM_512MB
		: Z2_MEM_1GB);
    expamem_init_clear();
    expamem_write (0x00, add_memory | zorroIII | code);

    expamem_write (0x08, no_shutup | force_z3 | (z3fastmem_size > 0x800000 ? ext_size : Z3_MEM_AUTO));

    expamem_write (0x04, 3);

    expamem_write (0x10, hackers_id >> 8);
    expamem_write (0x14, hackers_id & 0xff);

    expamem_write (0x18, 0x00); /* ser.no. Byte 0 */
    expamem_write (0x1c, 0x00); /* ser.no. Byte 1 */
    expamem_write (0x20, 0x00); /* ser.no. Byte 2 */
    expamem_write (0x24, 0x01); /* ser.no. Byte 3 */

    expamem_write (0x28, 0x00); /* Rom-Offset hi */
    expamem_write (0x2c, 0x00); /* ROM-Offset lo */

    expamem_write (0x40, 0x00); /* Ctrl/Statusreg.*/

}

#ifdef PICASSO96
/*
 *  Fake Graphics Card (ZORRO III) - BDK
 */

static void expamem_map_gfxcard (void)
{
    char buffer[80];
    gfxmem_start = ((expamem_hi | (expamem_lo >> 4)) << 16);
    map_banks (&gfxmem_bank, gfxmem_start >> 16, gfxmem_size >> 16);
    sprintf (buffer, "UAEGFX-card: mapped @$%lx \n", gfxmem_start);
    write_log (buffer);
}

static void expamem_init_gfxcard (void)
{
    expamem_init_clear();
    expamem_write (0x00, zorroIII);

    switch (gfxmem_size) {
     case 0x00100000:
	expamem_write (0x08, no_shutup | force_z3 | Z3_MEM_1MB);
	break;
     case 0x00200000:
	expamem_write (0x08, no_shutup | force_z3 | Z3_MEM_2MB);
	break;
     case 0x00400000:
	expamem_write (0x08, no_shutup | force_z3 | Z3_MEM_4MB);
	break;
     case 0x00800000:
	expamem_write (0x08, no_shutup | force_z3 | Z3_MEM_8MB);
	break;
    }

    expamem_write (0x04, 96);

    expamem_write (0x10, hackers_id >> 8);
    expamem_write (0x14, hackers_id & 0xff);

    expamem_write (0x18, 0x00); /* ser.no. Byte 0 */
    expamem_write (0x1c, 0x00); /* ser.no. Byte 1 */
    expamem_write (0x20, 0x00); /* ser.no. Byte 2 */
    expamem_write (0x24, 0x01); /* ser.no. Byte 3 */

    expamem_write (0x28, 0x00); /* Rom-Offset hi */
    expamem_write (0x2c, 0x00); /* ROM-Offset lo */

    expamem_write (0x40, 0x00); /* Ctrl/Statusreg.*/
}
#endif

void expamem_reset()
{
    int cardno = 0;
    ecard = 0;

    if (fastmemory != NULL) {
	card_init[cardno] = expamem_init_fastcard;
	card_map[cardno++] = expamem_map_fastcard;
    }
    if (z3fastmem != NULL) {
	card_init[cardno] = expamem_init_z3fastmem;
	card_map[cardno++] = expamem_map_z3fastmem;
    }
#ifdef PICASSO96
    if (gfxmemory != NULL) {
	card_init[cardno] = expamem_init_gfxcard;
	card_map[cardno++] = expamem_map_gfxcard;
    }
#endif
    if (currprefs.automount_uaedev && !ersatzkickfile) {
	card_init[cardno] = expamem_init_filesys;
	card_map[cardno++] = expamem_map_filesys;
    }
    while (cardno < MAX_EXPANSION_BOARDS) {
	card_init[cardno] = expamem_init_clear;
	card_map[cardno++] = expamem_map_clear;
    }

    (*card_init[0]) ();
}

void expansion_init(void)
{
    if (fastmem_size > 0) {
	do {
	    fastmem_mask = fastmem_size - 1;
	    fastmemory = (uae_u8 *)malloc(fastmem_size);
	    if (!fastmemory)
		fastmem_size >>= 1;
	} while (!fastmemory && fastmem_size >= 1024 * 1024);
	if (fastmemory == NULL) {
	    write_log ("Out of memory for fastmem card.\n");
	}
    }
    if (z3fastmem_size > 0) {
	z3fastmem_mask = z3fastmem_size - 1;
	z3fastmem = (uae_u8 *)malloc(z3fastmem_size);
	if (z3fastmem == NULL) {
	    write_log ("Out of memory for Fastmem.(32bit)\n");
	}
    }
    if (gfxmem_size > 0) {
	gfxmem_mask = gfxmem_size - 1;
	gfxmemory = (uae_u8 *)malloc(gfxmem_size);
	if (!gfxmemory) {
	    write_log ("Out of memory for UAEGFX-card.\n");
	}
    }
}

