 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Unix file system handler for AmigaDOS
  *
  * Copyright 1996 Ed Hanway
  * Copyright 1996, 1997 Bernd Schmidt
  *
  * Version 0.4: 970308
  *
  * Based on example code (c) 1988 The Software Distillery
  * and published in Transactor for the Amiga, Volume 2, Issues 2-5.
  * (May - August 1989)
  *
  * Known limitations:
  * Does not support ACTION_INHIBIT (big deal).
  * Does not support any 2.0+ packet types (except ACTION_SAME_LOCK)
  * Does not support removable volumes.
  * May not return the correct error code in some cases.
  * Does not check for sane values passed by AmigaDOS.  May crash the emulation
  * if passed garbage values.
  * Could do tighter checks on malloc return values.
  * Will probably fail spectacularly in some cases if the filesystem is
  * modified at the same time by another process while UAE is running.
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "options.h"
#include "uae.h"
#include "memory.h"
#include "custom.h"
#include "readcpu.h"
#include "newcpu.h"
#include "filesys.h"
#include "autoconf.h"
#include "compiler.h"
#include "zfile.h"
#include "threaddep/penguin.h"

/* #define TRACING_ENABLED */
#ifdef TRACING_ENABLED
#define TRACE(x)	do { sprintf x; write_log(warning_buffer); } while(0)
#define DUMPLOCK(u,x)	dumplock(u,x)
#else
#define TRACE(x)
#define DUMPLOCK(u,x)
#endif

#define MAKE_CASE_INSENSITIVE

/* errors */
#define ERROR_NO_FREE_STORE		103
#define ERROR_OBJECT_IN_USE		202
#define ERROR_OBJECT_EXISTS		203
#define ERROR_DIR_NOT_FOUND		204
#define ERROR_OBJECT_NOT_FOUND		205
#define ERROR_ACTION_NOT_KNOWN		209
#define ERROR_INVALID_LOCK		211
#define ERROR_OBJECT_WRONG_TYPE		212
#define ERROR_DISK_WRITE_PROTECTED	214
#define ERROR_DIRECTORY_NOT_EMPTY	216
#define ERROR_DEVICE_NOT_MOUNTED	218
#define ERROR_SEEK_ERROR		219
#define ERROR_DISK_FULL			221
#define ERROR_DELETE_PROTECTED		222
#define ERROR_WRITE_PROTECTED		223
#define ERROR_READ_PROTECTED		224
#define ERROR_NO_MORE_ENTRIES		232
#define ERROR_NOT_IMPLEMENTED		236

#ifndef DONT_HAVE_POSIX
static long dos_errno(void)
{
    int e = errno;

    switch(e) {
     case ENOMEM:	return ERROR_NO_FREE_STORE;
     case EEXIST:	return ERROR_OBJECT_EXISTS;
     case EACCES:	return ERROR_WRITE_PROTECTED;
     case ENOENT:	return ERROR_OBJECT_NOT_FOUND;
     case ENOTDIR:	return ERROR_OBJECT_WRONG_TYPE;
     case ENOSPC:	return ERROR_DISK_FULL;
     case EBUSY:       	return ERROR_OBJECT_IN_USE;
     case EISDIR:	return ERROR_OBJECT_WRONG_TYPE;
#if defined(ETXTBSY)
     case ETXTBSY:	return ERROR_OBJECT_IN_USE;
#endif
#if defined(EROFS)
     case EROFS:       	return ERROR_DISK_WRITE_PROTECTED;
#endif
#if defined(ENOTEMPTY)
#if ENOTEMPTY != EEXIST
     case ENOTEMPTY:	return ERROR_DIRECTORY_NOT_EMPTY;
#endif
#endif

     default:
	TRACE((warning_buffer, "Unimplemented error %s\n", strerror(e)));
	return ERROR_NOT_IMPLEMENTED;
    }
}
#endif

/*
 * This _should_ be no issue for us, but let's rather use a guaranteed
 * thread safe function if we have one.
 * Well, let's use it once it works.
 */
#if 0 && defined HAVE_READDIR_R

static struct dirent *my_readdir (DIR *dirstream, struct dirent *space)
{
    struct dirent *loc;
    if (readdir_r (dirstream, space, &loc) == 0) {
	/* Success */
	return loc;
    }
    return 0;
}

#else
#define my_readdir(dirstream, space) readdir(dirstream)
#endif

#define FS_STARTUP 0
#define FS_GO_DOWN 1

/* AmigaOS "keys" */
struct a_inode {
    struct a_inode *next, *prev;
    struct a_inode *parent;
    struct a_inode *child, *sibling; /* @@@ implement hash tables or something to make this efficient */
    char *aname;
    char *nname;
    int amigaos_mode;
    int shlock;
    uae_u32 uniq;
    unsigned int dir:1;
    unsigned int elock:1;
};

typedef struct {
    char *devname; /* device name, e.g. UAE0: */
    uaecptr devname_amiga;
    uaecptr startup;
    char *volname; /* volume name, e.g. CDROM, WORK, etc. */
    char *rootdir; /* root unix directory */
    int readonly; /* disallow write access? */
    int devno;
    
    struct hardfiledata hf;

    /* Threading stuff */
    smp_comm_pipe *unit_pipe, *back_pipe;
    penguin_id tid;
    struct _unit *volatile self;
} UnitInfo;

#define MAX_UNITS 20
static int num_units = 0, num_filesys_units = 0;
static UnitInfo ui[MAX_UNITS];
uaecptr filesys_initcode;
static uae_u32 fsdevname, hardfileseglist, filesys_configdev;
static uaecptr filesys_parampacket;

char * add_filesys_unit(char *volname, char *rootdir, int readonly,
			int secspertrack, int surfaces, int reserved)
{
    UnitInfo *uip;
    if (num_units >= MAX_UNITS) {
	return "Maximum number of file systems mounted";
    }

    uip = ui + num_units;
    if (volname != 0) {
	num_filesys_units++;
	uip->volname = my_strdup(volname);
	uip->hf.fd = 0;
    } else {
	FILE *hf;

	uip->volname = 0;
	uip->hf.fd = zfile_open (rootdir, "r+b");
	if (uip->hf.fd == 0) {
	    readonly = 1;
	    uip->hf.fd = zfile_open (rootdir, "rb");
	}
	if (uip->hf.fd == 0) {
	    return "Hardfile not found";
	}
	if (secspertrack < 1 || secspertrack > 32767
	    || surfaces < 1 || surfaces > 1023
	    || reserved < 0 || reserved > 1023) {
	    return "Bad hardfile geometry";
	}
	fseek (uip->hf.fd, 0, SEEK_END);
	uip->hf.size = ftell (uip->hf.fd);
	uip->hf.secspertrack = secspertrack;
	uip->hf.surfaces = surfaces;
	uip->hf.reservedblocks = reserved;
	uip->hf.nrcyls = (uip->hf.size >> 9) / (secspertrack * surfaces);
    }
    uip->rootdir = my_strdup (rootdir);
    uip->readonly = readonly;

    num_units++;
    return 0;
}

int kill_filesys_unit (int nr)
{
    if (nr > num_units || nr < 0)
	return -1;

    if (ui[nr].hf.fd != 0)
	fclose (ui[nr].hf.fd);
    num_units--;
    for (; nr < num_units; nr++) {
	ui[nr] = ui[nr+1];
    }
    return 0;
}

struct hardfiledata *get_hardfile_data (int nr)
{
    if (nr < 0 || nr >= num_units || ui[nr].volname != 0)
	return 0;
    return &ui[nr].hf;
}

int sprintf_filesys_unit(char *buffer, int num)
{
    if (num >= num_units)
	return -1;
    
    if (ui[num].volname != 0)
	sprintf (buffer, "(UAE%d:) Filesystem, %s: %s %s", num, ui[num].volname,
		 ui[num].rootdir, ui[num].readonly ? "ro" : "");
    else
	sprintf (buffer, "(UAE%d:) Hardfile, \"%s\", size %d bytes", num,
		 ui[num].rootdir, ui[num].hf.size);
    return 0;
}

void write_filesys_config(FILE *f)
{
    int i;
    for (i = 0; i < num_units; i++) {
	if (ui[i].volname != 0) {
	    fprintf(f, "-%c %s:%s\n", ui[i].readonly ? 'M' : 'm',
		    ui[i].volname, ui[i].rootdir);
	} else {
	    fprintf(f, "-W %d:%d:%d:%s\n", ui[i].hf.secspertrack,
		    ui[i].hf.surfaces, ui[i].hf.reservedblocks,
		    ui[i].rootdir);
	}
    }
}

/* minimal AmigaDOS definitions */

/* field offsets in DosPacket */
#define dp_Type 8
#define dp_Res1	12
#define dp_Res2 16
#define dp_Arg1 20
#define dp_Arg2 24
#define dp_Arg3 28
#define dp_Arg4 32

/* result codes */
#define DOS_TRUE ((unsigned long)-1L)
#define DOS_FALSE (0L)

/* packet types */
#define ACTION_CURRENT_VOLUME	7
#define ACTION_LOCATE_OBJECT	8
#define ACTION_RENAME_DISK	9
#define ACTION_FREE_LOCK	15
#define ACTION_DELETE_OBJECT	16
#define ACTION_RENAME_OBJECT	17
#define ACTION_COPY_DIR		19
#define ACTION_SET_PROTECT	21
#define ACTION_CREATE_DIR	22
#define ACTION_EXAMINE_OBJECT	23
#define ACTION_EXAMINE_NEXT	24
#define ACTION_DISK_INFO	25
#define ACTION_INFO		26
#define ACTION_FLUSH		27
#define ACTION_SET_COMMENT	28
#define ACTION_PARENT		29
#define ACTION_SET_DATE		34
#define ACTION_FIND_WRITE	1004
#define ACTION_FIND_INPUT	1005
#define ACTION_FIND_OUTPUT	1006
#define ACTION_END		1007
#define ACTION_SEEK		1008
#define ACTION_IS_FILESYSTEM	1027
#define ACTION_READ		'R'
#define ACTION_WRITE		'W'

/* 2.0+ packet types */
#define ACTION_INHIBIT       31
#define ACTION_SET_FILE_SIZE 1022
#define ACTION_LOCK_RECORD   2008
#define ACTION_FREE_RECORD   2009
#define ACTION_SAME_LOCK     40
#define ACTION_CHANGE_MODE   1028
#define ACTION_FH_FROM_LOCK  1026
#define ACTION_COPY_DIR_FH   1030
#define ACTION_PARENT_FH     1031
#define ACTION_EXAMINE_FH    1034
#define ACTION_EXAMINE_ALL   1033
#define ACTION_MAKE_LINK     1021
#define ACTION_READ_LINK     1024
#define ACTION_FORMAT        1020
#define ACTION_IS_FILESYSTEM 1027
#define ACTION_ADD_NOTIFY    4097
#define ACTION_REMOVE_NOTIFY 4098

#define DISK_TYPE		0x444f5301 /* DOS\1 */

typedef struct {
    uae_u32 uniq;
    struct a_inode *aino;
    DIR* dir;
} ExamineKey;

typedef struct key {
    struct key *next;
    struct a_inode *aino;
    uae_u32 uniq;
    int fd;
    off_t file_pos;
} Key;

/* Since ACTION_EXAMINE_NEXT is so braindamaged, we have to keep
 * some of these around
 */

#define EXKEYS 100
#define MAX_AINO_HASH 128

/* handler state info */

typedef struct _unit {
    struct _unit *next;

    /* Amiga stuff */
    uaecptr	dosbase;
    uaecptr	volume;
    uaecptr	port;	/* Our port */
    uaecptr	locklist;

    /* Native stuff */
    uae_s32	unit;	/* unit number */
    UnitInfo	ui;	/* unit startup info */
    char	tmpbuf3[256];

    /* Dummy message processing */
    uaecptr	dummy_message;
    volatile unsigned int cmds_sent;
    volatile unsigned int cmds_complete;
    volatile unsigned int cmds_acked;

    /* ExKeys */
    ExamineKey	examine_keys[EXKEYS];
    int		next_exkey;

    /* Keys */
    struct key*	keys;
    uae_u32	key_uniq;
    uae_u32	a_uniq;

    struct a_inode rootnode;
    unsigned long aino_cache_size;
    struct a_inode *aino_hash[MAX_AINO_HASH];
    unsigned long nr_cache_hits;
    unsigned long nr_cache_lookups;

    /* Reset handling */
    uae_sem_t	reset_sync_sem;
    int		reset_state;
} Unit;

typedef uae_u8 *dpacket;
#define PUT_PCK_RES1(p,v) do { do_put_mem_long ((uae_u32 *)((p) + dp_Res1), (v)); } while (0)
#define PUT_PCK_RES2(p,v) do { do_put_mem_long ((uae_u32 *)((p) + dp_Res2), (v)); } while (0)
#define GET_PCK_TYPE(p) ((uae_s32)(do_get_mem_long ((uae_u32 *)((p) + dp_Type))))
#define GET_PCK_RES1(p) ((uae_s32)(do_get_mem_long ((uae_u32 *)((p) + dp_Res1))))
#define GET_PCK_RES2(p) ((uae_s32)(do_get_mem_long ((uae_u32 *)((p) + dp_Res2))))
#define GET_PCK_ARG1(p) ((uae_s32)(do_get_mem_long ((uae_u32 *)((p) + dp_Arg1))))
#define GET_PCK_ARG2(p) ((uae_s32)(do_get_mem_long ((uae_u32 *)((p) + dp_Arg2))))
#define GET_PCK_ARG3(p) ((uae_s32)(do_get_mem_long ((uae_u32 *)((p) + dp_Arg3))))
#define GET_PCK_ARG4(p) ((uae_s32)(do_get_mem_long ((uae_u32 *)((p) + dp_Arg4))))

static char *bstr1 (uaecptr addr)
{
    static char buf[256];
    int i;
    int n = get_byte(addr);
    addr++;

    for(i = 0; i < n; i++, addr++)
	buf[i] = get_byte(addr);
    buf[i] = 0;
    return buf;
}

static char *bstr (Unit *unit, uaecptr addr)
{
    int i;
    int n = get_byte(addr);

    addr++;
    for(i = 0; i < n; i++, addr++)
	unit->tmpbuf3[i] = get_byte(addr);
    unit->tmpbuf3[i] = 0;
    return unit->tmpbuf3;
}

static char *bstr_cut (Unit *unit, uaecptr addr)
{
    char *p = unit->tmpbuf3;
    int i, colon_seen = 0;
    int n = get_byte (addr);

    addr++;
    for(i = 0; i < n; i++, addr++) {
	uae_u8 c = get_byte(addr);
	unit->tmpbuf3[i] = c;
	if (c == '/' || (c == ':' && colon_seen++ == 0))
	    p = unit->tmpbuf3 + i + 1;
    }
    unit->tmpbuf3[i] = 0;
    return p;
}

static Unit *units = 0;
static int unit_num = 0;

static Unit*
find_unit(uaecptr port)
{
    Unit* u;
    for(u = units; u; u = u->next)
	if(u->port == port)
	    break;

    return u;
}

static int get_file_attrs (struct a_inode *aino)
{
    struct stat statbuf;
    if (-1 == stat (aino->nname, &statbuf))
	return dos_errno ();
    aino->dir = S_ISDIR (statbuf.st_mode) ? 1 : 0;
    aino->amigaos_mode =
#ifndef __DOS__
	(S_IXUSR & statbuf.st_mode ? 0 : A_FIBF_EXECUTE) |
#endif
	(S_IWUSR & statbuf.st_mode ? 0 : A_FIBF_WRITE) |
#if 0 /* This makes it impossible to overwrite those. */
	(S_IWUSR & statbuf.st_mode ? 0 : A_FIBF_DELETE) |
#endif
	(S_IRUSR & statbuf.st_mode ? 0 : A_FIBF_READ);
    return 0;
};

static int set_file_attrs (struct a_inode *aino, int mask)
{
    struct stat statbuf;
    int mode;

    if (-1 == stat(aino->nname, &statbuf))
	return ERROR_OBJECT_NOT_FOUND;

    mode = statbuf.st_mode;
#ifdef __unix
    /* Unix dirs behave differently than AmigaOS ones. Just allow anything. */
    if (aino->dir) {
	mask = 0;
    }
#endif
    if (mask & A_FIBF_READ)
	mode &= ~S_IRUSR;
    else
	mode |= S_IRUSR;

    if ((mask & A_FIBF_WRITE) != 0 || (mask & A_FIBF_DELETE) != 0)
	mode &= ~S_IWUSR;
    else
	mode |= S_IWUSR;

    if (mask & A_FIBF_EXECUTE)
	mode &= ~S_IXUSR;
    else
	mode |= S_IXUSR;

    if (-1 == chmod(aino->nname, mode))
	return dos_errno();

    aino->amigaos_mode = mask;

    return 0;
}

static void prepare_for_open (char *name)
{
#if 0
    struct stat statbuf;
    int mode;

    if (-1 == stat (name, &statbuf))
	return;

    mode = statbuf.st_mode;
    mode |= S_IRUSR;
    mode |= S_IWUSR;
    mode |= S_IXUSR;
    chmod (name, mode);
#endif
}

static void de_recycle_aino (Unit *unit, struct a_inode *aino)
{
    if (aino->next == 0 || aino == &unit->rootnode)
	return;
    aino->next->prev = aino->prev;
    aino->prev->next = aino->next;
    aino->next = aino->prev = 0;
    unit->aino_cache_size--;
}

static void recycle_aino (Unit *unit, struct a_inode *aino)
{
    if (aino->dir || aino->shlock > 0 || aino->elock || aino == &unit->rootnode)
	/* Still in use */
	return;

    /* Chain it into circular list. */
    aino->next = unit->rootnode.next;
    aino->prev = &unit->rootnode;
    aino->prev->next = aino;
    aino->next->prev = aino;
    unit->aino_cache_size++;
    if (unit->aino_cache_size > 500) {
	/* Reap a few. */
	int i = 0;
	while (i < 50) {
	    struct a_inode **aip;
	    aip = &unit->rootnode.prev->parent->child;
	    for (;;) {
		aino = *aip;
		if (aino == 0)
		    break;

		if (aino->next == 0)
		    aip = &aino->sibling;
		else {
		    int hash = aino->uniq % MAX_AINO_HASH;
		    if (unit->aino_hash[hash] == aino)
			unit->aino_hash[hash] = 0;

		    aino->next->prev = aino->prev;
		    aino->prev->next = aino->next;
		    *aip = aino->sibling;
		    i++;

		    if (aino->shlock > 0 || aino->elock)
			write_log ("panic: freeing locked a_inode!\n");

		    free (aino->nname);
		    free (aino->aname);
		    free (aino);
		}
	    }
	}
#if 0
	{
	    char buffer[40];
	    sprintf (buffer, "%d ainos reaped.\n", i);
	    write_log (buffer);
	}
#endif
	unit->aino_cache_size -= i;
    }
}

static void update_child_names (Unit *unit, struct a_inode *a, struct a_inode *parent)
{
    int l0 = strlen (parent->nname) + 2;

    while (a != 0) {
	char *name_start;
	char *new_name;

	a->parent = parent;
	name_start = strrchr (a->nname, '/');
	if (name_start == 0) {
	    write_log ("malformed file name");
	}
	name_start++;
	new_name = (char *)xmalloc (strlen (name_start) + l0);
	strcpy (new_name, parent->nname);
	strcat (new_name, "/");
	strcat (new_name, name_start);
	free (a->nname);
	a->nname = new_name;
	if (a->child)
	    update_child_names (unit, a->child, a);
	a = a->sibling;
    }
}

static void move_aino_children (Unit *unit, struct a_inode *from, struct a_inode *to)
{
    to->child = from->child;
    from->child = 0;
    update_child_names (unit, to->child, to);
}

static void delete_aino (Unit *unit, struct a_inode *aino)
{
    struct a_inode **aip;
    int hash;

    TRACE((warning_buffer, "deleting aino %x\n", aino->uniq));

    de_recycle_aino (unit, aino);
    aip = &aino->parent->child;
    while (*aip != aino && *aip != 0)
	aip = &(*aip)->sibling;
    if (*aip != aino) {
	write_log ("Couldn't delete aino.\n");
	return;
    }
    hash = aino->uniq % MAX_AINO_HASH;
    if (unit->aino_hash[hash] == aino)
	unit->aino_hash[hash] = 0;
    *aip = aino->sibling;
    free (aino->nname);
    free (aino->aname);
    free (aino);
}

static struct a_inode *lookup_sub (struct a_inode *dir, uae_u32 uniq)
{
    struct a_inode **cp = &dir->child;
    struct a_inode *c, *retval;

    for (;;) {
	c = *cp;
	if (c == 0)
	    return 0;

	if (c->uniq == uniq) {
	    retval = c;
	    break;
	}
	if (c->dir) {
	    struct a_inode *a = lookup_sub (c, uniq);
	    if (a != 0) {
		retval = a;
		break;
	    }
	}
	cp = &c->sibling;
    }
    *cp = c->sibling;
    c->sibling = dir->child;
    dir->child = c;
    return retval;
}

static struct a_inode *lookup_aino (Unit *unit, uae_u32 uniq)
{
    struct a_inode *a;
    int hash = uniq % MAX_AINO_HASH;

    if (uniq == 0)
	return &unit->rootnode;
    a = unit->aino_hash[hash];
    if (a == 0 || a->uniq != uniq)
	a = lookup_sub (&unit->rootnode, uniq);
    else
	unit->nr_cache_hits++;
    unit->nr_cache_lookups++;
    unit->aino_hash[hash] = a;
    return a;
}

/* This gets called to translate an Amiga name that some program used to
 * a name that we can use on the native filesystem. */
static char *get_nname (Unit *unit, struct a_inode *base, char *rel)
{
    DIR* dir;
    struct dirent* de;
    char *p = 0;

    dir = opendir(base->nname);
    if (dir) {
	struct dirent de_space;
	while((de = my_readdir (dir, &de_space))) {
	    if (strcasecmp(de->d_name, rel) == 0)
		break;
	}
	if (de) {
	    p = (char *)xmalloc (strlen (de->d_name) + strlen (base->nname) + 2);
	    strcpy (p, base->nname);
	    strcat (p, "/");
	    strcat (p, de->d_name);
	}
	closedir(dir);
    }
    return p;
}

/*
 * This gets called if an ACTION_EXAMINE_NEXT happens and we hit an object
 * for which we know the name on the native filesystem, but no corresponding
 * Amiga filesystem name.
 * @@@ For DOS filesystems, it might make sense to declare the new name
 * "weak", so that it can get overriden by a subsequent call to get_nname().
 * That way, if someone does "dir :" and there is a file "foobar.inf", and
 * someone else tries to open "foobar.info", get_nname() could maybe made to
 * figure out that this is supposed to be the file "foobar.inf".
 * DOS sucks...
 */
static char *get_aname (Unit *unit, struct a_inode *base, char *rel)
{
    char *p = 0;

    p = (char *)xmalloc (strlen (rel) + strlen (base->aname) + 2);
    strcpy (p, base->aname);
    strcat (p, "/");
    strcat (p, rel);

    return p;
}

static struct a_inode *new_child_aino (Unit *unit, struct a_inode *base, char *rel, int force)
{
    struct stat statbuf;
    char *nn;
    struct a_inode *aino;

    nn = get_nname (unit, base, rel);
    if (nn == 0 && !force)
	return 0;
    else if (nn != 0)
	force = 0;

    aino = (struct a_inode *)malloc (sizeof (struct a_inode));
    if (aino == 0)
	return 0;
    aino->aname = my_strdup (rel);
    if (nn == 0) {
	nn = (char *)xmalloc (strlen (base->nname) + strlen (rel) + 2);
	strcpy (nn, base->nname);
	strcat (nn, "/");
	strcat (nn, rel);
	free (rel);
    }
    aino->nname = nn;
    aino->uniq = ++unit->a_uniq;
    if (unit->a_uniq == 0xFFFFFFFF) {
	write_log ("Running out of a_inodes (prepare for big trouble)!\n");
    }
    aino->shlock = 0;
    aino->elock = 0;

    /* Update tree structure */
    aino->parent = base;
    aino->child = 0;
    aino->sibling = base->child;
    base->child = aino;
    aino->next = aino->prev = 0;

    if (force == 2) {
	aino->dir = 1;
	aino->amigaos_mode = 0;
    } else if (force == 1) {
	aino->dir = 0;
	aino->amigaos_mode = 0;
    } else {
	get_file_attrs (aino);
    }
    recycle_aino (unit, aino);
    TRACE((warning_buffer, "created aino %x, normal\n", aino->uniq));
    return aino;
}

static struct a_inode *lookup_child_aino (Unit *unit, struct a_inode *base, char *rel, uae_u32 *err)
{
    struct a_inode *c = base->child;
    char *p, *p2;
    int l0 = strlen (rel);

    if (base->dir == 0) {
	*err = ERROR_OBJECT_WRONG_TYPE;
	return 0;
    }

    while (c != 0) {
	int l1 = strlen (c->aname);
	if (l0 <= l1 && strcasecmp (rel, c->aname + l1 - l0) == 0
	    && (l0 == l1 || c->aname[l1-l0-1] == '/'))
	    break;
	c = c->sibling;
    }
    if (c != 0)
	return c;
    c = new_child_aino (unit, base, rel, 0);
    if (c == 0)
	*err = ERROR_OBJECT_NOT_FOUND;
    return c;
}

/* Different version because this one compares with the nname */
static struct a_inode *lookup_child_aino_for_exnext (Unit *unit, struct a_inode *base, char *rel, uae_u32 *err)
{
    struct a_inode *c = base->child;
    char *p, *p2;
    int l0 = strlen (rel);

    *err = 0;
    while (c != 0) {
	int l1 = strlen (c->nname);
	if (l0 <= l1 && strcasecmp (rel, c->nname + l1 - l0) == 0
	    && (l0 == l1 || c->nname[l1-l0-1] == '/'))
	    break;
	c = c->sibling;
    }
    if (c != 0)
	return c;
    c = (struct a_inode *)malloc (sizeof (struct a_inode));
    if (c == 0)
	*err = ERROR_NO_FREE_STORE;
    else {
	c->nname = (char *)xmalloc (strlen (base->nname) + l0 + 2);
	strcpy (c->nname, base->nname);
	strcat (c->nname, "/");
	strcat (c->nname, rel);
	c->aname = get_aname (unit, base, rel);
	c->uniq = ++unit->a_uniq;
	if (unit->a_uniq == 0xFFFFFFFF) {
	    write_log ("Running out of a_inodes (prepare for big trouble)!\n");
	}
	c->shlock = 0;
	c->elock = 0;

	/* Update tree structure */
	c->parent = base;
	c->child = 0;
	c->sibling = base->child;
	base->child = c;
	c->next = c->prev = 0;

	get_file_attrs (c);
	recycle_aino (unit, c);
	TRACE((warning_buffer, "created aino %x, exnext\n", c->uniq));
    }
    return c;
}

static struct a_inode *get_aino(Unit* unit, struct a_inode *base, const char *rel, uae_u32 *err)
{
    char *tmp;
    char *p;
    char *r;
    struct a_inode *curr;
    int i;

    *err = 0;
    TRACE((warning_buffer, "get_path(%s,%s)\n", base->aname, rel));

    /* root-relative path? */
    for(i = 0; rel[i] && rel[i] != '/' && rel[i] != ':'; i++)
	;
    if (':' == rel[i])
	rel += i+1;

    tmp = my_strdup (rel);
    p = tmp;
    curr = base;

    while(*p) {
	/* start with a slash? go up a level. */
	if (*p == '/') {
	    if (curr->parent != 0)
		curr = curr->parent;
	    p++;
	} else {
	    struct a_inode *next;

	    char *component_end;
	    component_end = strchr (p, '/');
	    if (component_end != 0)
		*component_end = '\0';
	    next = lookup_child_aino (unit, curr, p, err);
	    if (next == 0) {
		/* if only last component not found, return parent dir. */
		if (*err != ERROR_OBJECT_NOT_FOUND || component_end != 0)
		    curr = 0;
		/* ? what error is appropriate? */
		break;
	    }
	    curr = next;
	    if (component_end)
		p = component_end+1;
	    else
		break;

	}
    }
    free (tmp);
    return curr;
}

static uae_u32 startup_handler (void)
{
    /* Just got the startup packet. It's in A4. DosBase is in A2,
     * our allocated volume structure is in D6, A5 is a pointer to
     * our port. */
    uaecptr rootnode = get_long(m68k_areg (regs, 2) + 34);
    uaecptr dos_info = get_long(rootnode + 24) << 2;
    uaecptr pkt = m68k_dreg (regs, 3);
    uaecptr arg2 = get_long (pkt + dp_Arg2);
    int i, namelen;
    char* devname = bstr1 (get_long (pkt + dp_Arg1) << 2);
    char* s;
    Unit* unit;
    UnitInfo *uinfo;

    /* find UnitInfo with correct device name */
    s = strchr(devname, ':');
    if(s)
	*s = '\0';

    for(i = 0; i < num_units; i++) {
	/* Hardfile volume name? */
	if (ui[i].volname == 0)
	    continue;

	if (ui[i].startup == arg2)
	    break;
    }

    if(i == num_units || 0 != access(ui[i].rootdir, R_OK)) {
	sprintf(warning_buffer, "Failed attempt to mount device\n", devname);
	write_log (warning_buffer);
	put_long (pkt + dp_Res1, DOS_FALSE);
	put_long (pkt + dp_Res2, ERROR_DEVICE_NOT_MOUNTED);
	return 1;
    }
    uinfo = ui + i;

    unit = (Unit *) xmalloc (sizeof (Unit));
    unit->next = units;
    units = unit;
    uinfo->self = unit;

    unit->volume = 0;
    unit->reset_state = FS_STARTUP;
    unit->port = m68k_areg(regs, 5);
    unit->unit = unit_num++;

    unit->ui.devname = uinfo->devname;
    unit->ui.volname = my_strdup (uinfo->volname); /* might free later for rename */
    unit->ui.rootdir = uinfo->rootdir;
    unit->ui.readonly = uinfo->readonly;
    unit->ui.unit_pipe = uinfo->unit_pipe;
    unit->ui.back_pipe = uinfo->back_pipe;
    unit->cmds_complete = 0;
    unit->cmds_sent = 0;
    unit->cmds_acked = 0;
    for (i = 0; i < EXKEYS; i++) {
	unit->examine_keys[i].aino = 0;
	unit->examine_keys[i].dir = 0;
	unit->examine_keys[i].uniq = 0;
    }
    unit->next_exkey = 1;
    unit->keys = 0;
    unit->a_uniq = unit->key_uniq = 0;

    unit->rootnode.aname = uinfo->volname;
    unit->rootnode.nname = uinfo->rootdir;
    unit->rootnode.sibling = 0;
    unit->rootnode.next = unit->rootnode.prev = &unit->rootnode;
    unit->rootnode.uniq = 0;
    unit->rootnode.parent = 0;
    unit->rootnode.child = 0;
    unit->rootnode.dir = 1;
    unit->rootnode.amigaos_mode = 0;
    unit->rootnode.shlock = 0;
    unit->rootnode.elock = 0;
    unit->aino_cache_size = 0;
    for (i = 0; i < MAX_AINO_HASH; i++)
	unit->aino_hash[i] = 0;

/*    write_comm_pipe_int (unit->ui.unit_pipe, -1, 1);*/

    TRACE((warning_buffer, "**** STARTUP volume %s\n", unit->ui.volname));

    /* fill in our process in the device node */
    put_long ((get_long (pkt + dp_Arg3) << 2) + 8, unit->port);
    unit->dosbase = m68k_areg(regs, 2);

    /* make new volume */
    unit->volume = m68k_areg (regs, 3) + 32;
#ifdef UAE_FILESYS_THREADS
    unit->locklist = m68k_areg (regs, 3) + 8;
#else
    unit->locklist = m68k_areg (regs, 3);
#endif
    unit->dummy_message = m68k_areg (regs, 3) + 12;

    put_long (unit->dummy_message + 10, 0);

    put_long (unit->volume + 4, 2); /* Type = dt_volume */
    put_long (unit->volume + 12, 0); /* Lock */
    put_long (unit->volume + 16, 3800); /* Creation Date */
    put_long (unit->volume + 20, 0);
    put_long (unit->volume + 24, 0);
    put_long (unit->volume + 28, 0); /* lock list */
    put_long (unit->volume + 40, (unit->volume + 44) >> 2); /* Name */
    namelen = strlen (unit->ui.volname);
    put_byte (unit->volume + 44, namelen);
    for(i = 0; i < namelen; i++)
	put_byte (unit->volume + 45 + i, unit->ui.volname[i]);

    /* link into DOS list */
    put_long (unit->volume, get_long(dos_info + 4));
    put_long (dos_info + 4, unit->volume >> 2);

    put_long (unit->volume + 8, unit->port);
    put_long (unit->volume + 32, DISK_TYPE);

    put_long (pkt + dp_Res1, DOS_TRUE);
    return 0;
}

#ifdef HAVE_STATFS
static void
do_info(Unit* unit, dpacket packet, uaecptr info)
{
    struct statfs statbuf;
#if STATFS_NO_ARGS == 2
    if(-1 == statfs(unit->ui.rootdir, &statbuf))
#else
    if(-1 == statfs(unit->ui.rootdir, &statbuf, sizeof(struct statfs), 0))
#endif
    {
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, dos_errno ());
    }

    put_long(info, 0); /* errors */
    put_long(info + 4, unit->unit); /* unit number */
    put_long(info + 8, unit->ui.readonly ? 80 : 82); /* state  */
    put_long(info + 12, statbuf.f_blocks); /* numblocks */
    put_long(info + 16, statbuf.f_blocks - statbuf.STATBUF_BAVAIL); /* inuse */
    put_long(info + 20, statbuf.f_bsize); /* bytesperblock */
    put_long(info + 24, DISK_TYPE); /* disk type */
    put_long(info + 28, unit->volume >> 2); /* volume node */
    put_long(info + 32, 0); /* inuse */
    PUT_PCK_RES1 (packet, DOS_TRUE);
}
#else
static void
do_info(Unit* unit, dpacket packet, uaecptr info)
{
    put_long(info, 0); /* errors */
    put_long(info + 4, unit->unit); /* unit number */
    put_long(info + 8, unit->ui.readonly ? 80 : 82); /* state  */
    put_long(info + 12, 256); /* numblocks */
    put_long(info + 16, 128); /* inuse */
    put_long(info + 20, 512); /* bytesperblock */
    put_long(info + 24, DISK_TYPE); /* disk type */
    put_long(info + 28, unit->volume >> 2); /* volume node */
    put_long(info + 32, 0); /* inuse */
    PUT_PCK_RES1 (packet, DOS_TRUE);
}
#endif

static void
action_disk_info(Unit* unit, dpacket packet)
{
    TRACE((warning_buffer, "ACTION_DISK_INFO\n"));
    do_info(unit, packet, GET_PCK_ARG1 (packet) << 2);
}

static void
action_info(Unit* unit, dpacket packet)
{
    TRACE((warning_buffer, "ACTION_INFO\n"));
    do_info(unit, packet, GET_PCK_ARG2 (packet) << 2);
}

static void free_key (Unit *unit, Key*k)
{
    Key *k1;
    Key *prev = 0;
    for (k1 = unit->keys; k1; k1 = k1->next) {
	if (k == k1) {
	    if(prev)
		prev->next = k->next;
	    else
		unit->keys = k->next;
	    break;
	}
	prev = k1;
    }

    if (k->fd >= 0)
	close(k->fd);

    free(k);
}

static Key *lookup_key(Unit *unit, uae_u32 uniq)
{
    Key *k;
    /* It's hardly worthwhile to optimize this - most of the time there are
     * only one or zero keys. */
    for(k = unit->keys; k; k = k->next) {
	if (uniq == k->uniq)
	    return k;
    }
    write_log ("Error: couldn't find key!\n");
#if 0
    exit(1);
    /* NOTREACHED */
#endif
    /* There isn't much hope we will recover. Unix would kill the process,
     * AmigaOS gets killed by it. */
    write_log ("Better reset that Amiga - the system is messed up.\n");
    return 0;
}

static Key *new_key(Unit *unit)
{
    Key *k = (Key*) xmalloc(sizeof(Key));
    k->uniq = ++unit->key_uniq;
    k->fd = -1;
    k->file_pos = 0;
    k->next = unit->keys;
    unit->keys = k;

    return k;
}

static void
dumplock(Unit *unit, uaecptr lock)
{
    struct a_inode *a;
    TRACE((warning_buffer, "LOCK: 0x%lx", lock));
    if(!lock) {
	TRACE((warning_buffer, "\n"));
	return;
    }
    TRACE((warning_buffer, "{ next=0x%lx, mode=%ld, handler=0x%lx, volume=0x%lx, aino %lx ",
	   get_long(lock) << 2, get_long(lock+8),
	   get_long(lock+12), get_long(lock+16),
	   get_long(lock + 4)));
    a = lookup_aino (unit, get_long (lock + 4));
    if (a == 0) {
	TRACE((warning_buffer, "not found!"));
    } else {
	TRACE((warning_buffer, "%s", a->nname));
    }
    TRACE((warning_buffer, " }\n"));
}

static struct a_inode *find_aino(Unit* unit, uaecptr lock, const char *name, uae_u32 *err)
{
    struct a_inode *a;

    if (lock) {
	struct a_inode *olda = lookup_aino (unit, get_long(lock + 4));
	if (olda == 0) {
	    /* That's the best we can hope to do. */
	    a = get_aino (unit, &unit->rootnode, name, err);
	} else {
	    TRACE((warning_buffer, "aino: 0x%08lx", (unsigned long int)olda->uniq));
	    TRACE((warning_buffer, " \"%s\"\n", olda->nname));
	    a = get_aino (unit, olda, name, err);
	}
    } else {
	a = get_aino (unit, &unit->rootnode, name, err);
    }
    if (a) {
	TRACE((warning_buffer, "aino=\"%s\"\n", a->nname));
    }
    return a;
}

static uaecptr make_lock (Unit* unit, uae_u32 uniq, long mode)
{
    /* allocate lock from the list kept by the assembly code */
    uaecptr lock;

    lock = get_long (unit->locklist);
    put_long (unit->locklist, get_long (lock));
    lock += 4;

    put_long(lock + 4, uniq);
    put_long(lock + 8, mode);
    put_long(lock + 12, unit->port);
    put_long(lock + 16, unit->volume >> 2);

    /* prepend to lock chain */
    put_long(lock, get_long(unit->volume + 28));
    put_long(unit->volume + 28, lock >> 2);

    DUMPLOCK(unit, lock);
    return lock;
}

static void free_lock (Unit* unit, uaecptr lock)
{
    Key *k;
    if(!lock)
	return;

    if (lock == get_long(unit->volume + 28) << 2) {
	put_long(unit->volume + 28, get_long(lock));
    } else {
	uaecptr current = get_long(unit->volume + 28);
	uaecptr next = 0;
	while(current) {
	    next = get_long(current << 2);
	    if(lock == next << 2)
		break;
	    current = next;
	}
	put_long(current << 2, get_long(lock));
    }
    lock -= 4;
    put_long (lock, get_long (unit->locklist));
    put_long (unit->locklist, lock);
}

static void
action_lock(Unit* unit, dpacket packet)
{
    uaecptr lock = GET_PCK_ARG1 (packet) << 2;
    uaecptr name = GET_PCK_ARG2 (packet) << 2;
    long mode = GET_PCK_ARG3 (packet);
    struct a_inode *a;
    uae_u32 err;

    if (mode != -2 && mode != -1) {
	TRACE((warning_buffer, "Bad mode.\n"));
	mode = -2;
    }

    TRACE((warning_buffer, "ACTION_LOCK(0x%lx, \"%s\", %d)\n", lock, bstr (unit, name), mode));
    DUMPLOCK(unit, lock);

    a = find_aino (unit, lock, bstr (unit, name), &err);
    if (err == 0 && (a->elock || (mode != -2 && a->shlock > 0))) {
	err = ERROR_OBJECT_IN_USE;
    }
    /* Lock() doesn't do access checks. */
    if (err != 0) {
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, err);
	return;
    }
    if (mode == -2)
	a->shlock++;
    else
	a->elock = 1;
    de_recycle_aino (unit, a);
    PUT_PCK_RES1 (packet, make_lock (unit, a->uniq, mode) >> 2);
}

static void action_free_lock (Unit* unit, dpacket packet)
{
    uaecptr lock = GET_PCK_ARG1 (packet) << 2;
    struct a_inode *a;
    TRACE((warning_buffer, "ACTION_FREE_LOCK(0x%lx)\n", lock));
    DUMPLOCK(unit, lock);

    a = lookup_aino (unit, get_long (lock + 4));
    if (a == 0) {
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, ERROR_OBJECT_NOT_FOUND);
	return;
    }
    if (a->elock)
	a->elock = 0;
    else
	a->shlock--;
    recycle_aino (unit, a);
    free_lock(unit, lock);

    PUT_PCK_RES1 (packet, DOS_TRUE);
}

static void
action_dup_lock(Unit* unit, dpacket packet)
{
    uaecptr lock = GET_PCK_ARG1 (packet) << 2;
    struct a_inode *a;
    TRACE((warning_buffer, "ACTION_DUP_LOCK(0x%lx)\n", lock));
    DUMPLOCK(unit, lock);

    if(!lock) {
	PUT_PCK_RES1 (packet, 0);
	return;
    }
    a = lookup_aino (unit, get_long (lock + 4));
    if (a == 0) {
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, ERROR_OBJECT_NOT_FOUND);
	return;
    }
    /* DupLock()ing exclusive locks isn't possible, says the Autodoc, but
     * at least the RAM-Handler seems to allow it. Let's see what happens
     * if we don't. */
    if (a->elock) {
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, ERROR_OBJECT_IN_USE);
	return;
    }
    a->shlock++;
    de_recycle_aino (unit, a);
    PUT_PCK_RES1 (packet, make_lock (unit, a->uniq, -2) >> 2);
}

/* convert time_t to/from AmigaDOS time */
const int secs_per_day = 24 * 60 * 60;
const int diff = (8 * 365 + 2) * (24 * 60 * 60);

static void
get_time(time_t t, long* days, long* mins, long* ticks)
{
    /* time_t is secs since 1-1-1970 */
    /* days since 1-1-1978 */
    /* mins since midnight */
    /* ticks past minute @ 50Hz */

    t -= diff;
    *days = t / secs_per_day;
    t -= *days * secs_per_day;
    *mins = t / 60;
    t -= *mins * 60;
    *ticks = t * 50;
}

static time_t
put_time(long days, long mins, long ticks)
{
    time_t t;
    t = ticks / 50;
    t += mins * 60;
    t += days * secs_per_day;
    t += diff;

    return t;
}

static void free_exkey(ExamineKey* ek)
{
    ek->aino = 0;
    ek->uniq = 0;
    if (ek->dir)
	closedir (ek->dir);
}

/* This is so sick... who invented ACTION_EXAMINE_NEXT? What did he THINK??? */
static ExamineKey *new_exkey(Unit *unit, struct a_inode *aino)
{
    uae_u32 uniq;
    uae_u32 oldest = 0xFFFFFFFF;
    ExamineKey *ek, *oldest_ek = 0;
    int i;

    ek = unit->examine_keys;
    for (i = 0; i < EXKEYS; i++, ek++) {
	/* Did we find a free one? */
	if (ek->aino == 0)
	    continue;
	if (ek->uniq < oldest)
	    oldest = (oldest_ek = ek)->uniq;
    }
    ek = unit->examine_keys;
    for (i = 0; i < EXKEYS; i++, ek++) {
	/* Did we find a free one? */
	if (ek->aino == 0)
	    goto found;
    }
    /* This message should usually be harmless. */
    write_log ("Houston, we have a problem.\n");
    free_exkey (oldest_ek);
    ek = oldest_ek;
    found:

    uniq = unit->next_exkey;
    if (uniq == 0xFFFFFFFF) {
	/* Things will probably go wrong, but most likely the Amiga will crash
	 * before this happens because of something else. */
	uniq = 1;
    }
    unit->next_exkey = uniq+1;
    ek->aino = aino;
    ek->dir = 0;
    ek->uniq = uniq;
    return ek;
}

static ExamineKey *lookup_exkey(Unit *unit, uae_u32 uniq)
{
    ExamineKey *ek;
    int i;

    ek = unit->examine_keys;
    for (i = 0; i < EXKEYS; i++, ek++) {
	/* Did we find a free one? */
	if (ek->uniq == uniq)
	    return ek;
    }
    write_log ("Houston, we have a BIG problem.\n");
    return 0;
}

static void
get_fileinfo(Unit *unit, dpacket packet, uaecptr info, struct a_inode *aino)
{
    struct stat statbuf;
    long days, mins, ticks;
    int i, n;
    char *x;
    uae_u32 err;

    /* No error checks - this had better work. */
    stat (aino->nname, &statbuf);

    if (aino->parent == 0) {
	x = unit->ui.volname;
	put_long (info + 4, 1);
	put_long (info + 120, 1);
    } else {
	/* AmigaOS docs say these have to contain the same value. */
	put_long(info + 4, aino->dir ? 2 : -3);
	put_long(info + 120, aino->dir ? 2 : -3);
	x = strrchr (aino->aname, '/');
	if (x)
	    x++;
	else
	    x = aino->aname;
    }
    TRACE((warning_buffer, "name=\"%s\"\n", x));
    n = strlen(x);
    if (n > 106)
	n = 106;
    i = 8;
    put_byte(info + i, n); i++;
    while(n--)
	put_byte(info + i, *x), i++, x++;
    while(i < 108)
	put_byte(info + i, 0), i++;

    put_long (info + 116, aino->amigaos_mode);
    put_long(info + 124, statbuf.st_size);
#ifdef HAVE_ST_BLOCKS
    put_long(info + 128, statbuf.st_blocks);
#else
    put_long(info + 128, statbuf.st_size / 512 + 1);
#endif
    get_time(statbuf.st_mtime, &days, &mins, &ticks);
    put_long(info + 132, days);
    put_long(info + 136, mins);
    put_long(info + 140, ticks);
    put_long(info + 144, 0); /* no comment */
    PUT_PCK_RES1 (packet, DOS_TRUE);
}

static void do_examine(Unit *unit, dpacket packet, ExamineKey* ek, uaecptr info)
{
    struct dirent de_space;
    struct dirent* de;
    struct a_inode *aino;
    uae_u32 err;

    if (!ek->dir) {
	ek->dir = opendir (ek->aino->nname);
    }
    if (!ek->dir) {
	free_exkey (ek);
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, ERROR_NO_MORE_ENTRIES);
	return;
    }

    de = my_readdir (ek->dir, &de_space);

    while (de && (0 == strcmp(".", de->d_name)
		 || 0 == strcmp("..", de->d_name)))
    {
	de = my_readdir (ek->dir, &de_space);
    }

    if (!de) {
	TRACE((warning_buffer, "no more entries\n"));
	free_exkey (ek);
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, ERROR_NO_MORE_ENTRIES);
	return;
    }

    TRACE((warning_buffer, "entry=\"%s\"\n", de->d_name));
    aino = lookup_child_aino_for_exnext (unit, ek->aino, de->d_name, &err);
    if (err != 0) {
	write_log ("Severe problem in ExNext.\n");
	free_exkey (ek);
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, err);
	return;
    }
    get_fileinfo (unit, packet, info, aino);
}

static void action_examine_object (Unit* unit, dpacket packet)
{
    uaecptr lock = GET_PCK_ARG1 (packet) << 2;
    uaecptr info = GET_PCK_ARG2 (packet) << 2;
    ExamineKey* ek;
    struct a_inode *aino = 0;

    TRACE((warning_buffer, "ACTION_EXAMINE_OBJECT(0x%lx,0x%lx)\n", lock, info));
    DUMPLOCK(unit, lock);

    if (lock != 0)
	aino = lookup_aino (unit, get_long(lock + 4));
    if (aino == 0)
	aino = &unit->rootnode;

    get_fileinfo (unit, packet, info, aino);
    if (aino->dir) {
	put_long(info, 0xFFFFFFFF);
    } else
	put_long(info, 0);
}

static void action_examine_next(Unit* unit, dpacket packet)
{
    uaecptr lock = GET_PCK_ARG1 (packet) << 2;
    uaecptr info = GET_PCK_ARG2 (packet) << 2;
    struct a_inode *aino = 0;
    ExamineKey *ek;
    uae_u32 uniq;

    TRACE((warning_buffer, "ACTION_EXAMINE_NEXT(0x%lx,0x%lx)\n", lock, info));
    DUMPLOCK(unit, lock);

    if (lock != 0)
	aino = lookup_aino (unit, get_long(lock + 4));
    if (aino == 0)
	aino = &unit->rootnode;

    uniq = get_long (info);
    if (uniq == 0) {
	write_log ("ExNext called for a file! (Houston?)\n");
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, ERROR_NO_MORE_ENTRIES);
	return;
    } else if (uniq == 0xFFFFFFFF) {
	ek = new_exkey(unit, aino);
    } else
	ek = lookup_exkey (unit, get_long(info));
    if (ek == 0) {
	write_log ("Couldn't find a matching ExKey. Prepare for trouble.\n");
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, ERROR_NO_MORE_ENTRIES);
	return;
    }
    put_long(info, ek->uniq);
    do_examine(unit, packet, ek, info);
}

static void do_find (Unit* unit, dpacket packet, int mode, int create, int fallback)
{
    uaecptr fh = GET_PCK_ARG1 (packet) << 2;
    uaecptr lock = GET_PCK_ARG2 (packet) << 2;
    uaecptr name = GET_PCK_ARG3 (packet) << 2;
    struct a_inode *aino;
    Key *k;
    struct stat st;
    int fd;
    int doserr;
    uae_u32 err;
    mode_t openmode;
    int aino_created = 0;

    TRACE((warning_buffer, "ACTION_FIND_*(0x%lx,0x%lx,\"%s\",%d)\n", fh, lock, bstr (unit, name), mode));
    DUMPLOCK(unit, lock);

    aino = find_aino (unit, lock, bstr (unit, name), &err);

    if (aino == 0 || (err != 0 && err != ERROR_OBJECT_NOT_FOUND)) {
	/* Whatever it is, we can't handle it. */
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, err);
	return;
    }
    if (err == 0) {
	/* Object exists. */
	if (aino->dir) {
	    PUT_PCK_RES1 (packet, DOS_FALSE);
	    PUT_PCK_RES2 (packet, ERROR_OBJECT_WRONG_TYPE);
	    return;
	}
	if (aino->elock || (create == 2 && aino->shlock > 0)) {
	    PUT_PCK_RES1 (packet, DOS_FALSE);
	    PUT_PCK_RES2 (packet, ERROR_OBJECT_IN_USE);
	    return;
	}
	if (create == 2 && (aino->amigaos_mode & A_FIBF_DELETE) != 0) {
	    PUT_PCK_RES1 (packet, DOS_FALSE);
	    PUT_PCK_RES2 (packet, ERROR_DELETE_PROTECTED);
	    return;
	}
	if (create != 2) {
	    if ((((mode & aino->amigaos_mode) & A_FIBF_WRITE) != 0 || unit->ui.readonly)
		&& fallback)
	    {
		mode &= ~A_FIBF_WRITE;
	    }
	    /* Kick 1.3 doesn't check read and write access bits - maybe it would be
	     * simpler just not to do that either. */
	    if ((mode & A_FIBF_WRITE) != 0 && unit->ui.readonly) {
		PUT_PCK_RES1 (packet, DOS_FALSE);
		PUT_PCK_RES2 (packet, ERROR_DISK_WRITE_PROTECTED);
		return;
	    }
	    if (((mode & aino->amigaos_mode) & A_FIBF_WRITE) != 0
		|| mode == 0)
	    {
		PUT_PCK_RES1 (packet, DOS_FALSE);
		PUT_PCK_RES2 (packet, ERROR_WRITE_PROTECTED);
		return;
	    }
	    if (((mode & aino->amigaos_mode) & A_FIBF_READ) != 0) {
		PUT_PCK_RES1 (packet, DOS_FALSE);
		PUT_PCK_RES2 (packet, ERROR_READ_PROTECTED);
		return;
	    }
	}
    } else if (create == 0) {
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, err);
	return;
    } else {
	/* Object does not exist. aino points to containing directory. */
	aino = new_child_aino (unit, aino, my_strdup (bstr_cut (unit, name)), 1);
	if (aino == 0) {
	    PUT_PCK_RES1 (packet, DOS_FALSE);
	    PUT_PCK_RES2 (packet, ERROR_DISK_FULL); /* best we can do */
	    return;
	}
	aino_created = 1;
    }

    prepare_for_open (aino->nname);

    openmode = (((mode & A_FIBF_READ) == 0 ? O_WRONLY
		 : (mode & A_FIBF_WRITE) == 0 ? O_RDONLY
		 : O_RDWR)
		| (create ? O_CREAT : 0)
		| (create == 2 ? O_TRUNC : 0));

    fd = open (aino->nname, openmode | O_BINARY, 0777);

    if (fd < 0) {
	if (aino_created)
	    delete_aino (unit, aino);
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, dos_errno());
	return;
    }
    k = new_key (unit);
    k->fd = fd;
    k->aino = aino;

    put_long (fh+36, k->uniq);
    if (create == 2)
	aino->elock = 1;
    else
	aino->shlock++;
    de_recycle_aino (unit, aino);
    PUT_PCK_RES1 (packet, DOS_TRUE);
}

static void
action_find_input(Unit* unit, dpacket packet)
{
    do_find(unit, packet, A_FIBF_READ|A_FIBF_WRITE, 0, 1);
}

static void
action_find_output(Unit* unit, dpacket packet)
{
    if (unit->ui.readonly) {
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, ERROR_DISK_WRITE_PROTECTED);
	return;
    }
    do_find(unit, packet, A_FIBF_READ|A_FIBF_WRITE, 2, 0);
}

static void
action_find_write(Unit* unit, dpacket packet)
{
    if (unit->ui.readonly) {
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, ERROR_DISK_WRITE_PROTECTED);
	return;
    }
    do_find(unit, packet, A_FIBF_READ|A_FIBF_WRITE, 1, 0);
}

static void
action_end(Unit* unit, dpacket packet)
{
    Key *k;
    TRACE((warning_buffer, "ACTION_END(0x%lx)\n", GET_PCK_ARG1 (packet)));

    k = lookup_key (unit, GET_PCK_ARG1 (packet));
    if (k != 0) {
	if (k->aino->elock)
	    k->aino->elock = 0;
	else
	    k->aino->shlock--;
	recycle_aino (unit, k->aino);
	free_key (unit, k);
    }
    PUT_PCK_RES1 (packet, DOS_TRUE);
    PUT_PCK_RES2 (packet, 0);
}

static void
action_read(Unit* unit, dpacket packet)
{
    Key *k = lookup_key (unit, GET_PCK_ARG1 (packet));
    uaecptr addr = GET_PCK_ARG2 (packet);
    long size = (uae_s32)GET_PCK_ARG3 (packet);
    int actual;

    if (k == 0) {
	PUT_PCK_RES1 (packet, DOS_FALSE);
	/* PUT_PCK_RES2 (packet, EINVAL); */
	return;
    }
    TRACE((warning_buffer, "ACTION_READ(%s,0x%lx,%ld)\n",k->aino->nname,addr,size));
#ifdef RELY_ON_LOADSEG_DETECTION
    /* HACK HACK HACK HACK
     * Try to detect a LoadSeg() */
    if (k->file_pos == 0 && size >= 4) {
	unsigned char buf[4];
	off_t currpos = lseek(k->fd, 0, SEEK_CUR);
	read(k->fd, buf, 4);
	lseek(k->fd, currpos, SEEK_SET);
	if (buf[0] == 0 && buf[1] == 0 && buf[2] == 3 && buf[3] == 0xF3)
	    possible_loadseg();
    }
#endif
    if (valid_address (addr, size)) {
	uae_u8 *realpt;
	realpt = get_real_address (addr);
	actual = read(k->fd, realpt, size);

	if (actual == 0) {
	    PUT_PCK_RES1 (packet, 0);
	    PUT_PCK_RES2 (packet, 0);
	} else if (actual < 0) {
	    PUT_PCK_RES1 (packet, 0);
	    PUT_PCK_RES2 (packet, dos_errno());
	} else {
	    PUT_PCK_RES1 (packet, actual);
	    k->file_pos += actual;
	}
    } else {
	char *buf;
	sprintf (warning_buffer, "unixfs warning: Bad pointer passed for read: %08x\n", addr);
	write_log (warning_buffer);
	/* ugh this is inefficient but easy */
	buf = (char *)malloc(size);
	if(!buf) {
	    PUT_PCK_RES1 (packet, -1);
	    PUT_PCK_RES2 (packet, ERROR_NO_FREE_STORE);
	    return;
	}
	actual = read(k->fd, buf, size);

	if (actual < 0) {
	    PUT_PCK_RES1 (packet, 0);
	    PUT_PCK_RES2 (packet, dos_errno());
	} else {
	    int i;
	    PUT_PCK_RES1 (packet, actual);
	    for(i = 0; i < actual; i++)
		put_byte(addr + i, buf[i]);
	    k->file_pos += actual;
	}
	free(buf);
    }
}

static void
action_write(Unit* unit, dpacket packet)
{
    Key *k = lookup_key (unit, GET_PCK_ARG1 (packet));
    uaecptr addr = GET_PCK_ARG2 (packet);
    long size = GET_PCK_ARG3 (packet);
    char *buf;
    int i;

    if (k == 0) {
	PUT_PCK_RES1 (packet, DOS_FALSE);
	/* PUT_PCK_RES2 (packet, EINVAL); */
	return;
    }

    TRACE((warning_buffer, "ACTION_WRITE(%s,0x%lx,%ld)\n",k->aino->nname,addr,size));

    if (unit->ui.readonly) {
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, ERROR_DISK_WRITE_PROTECTED);
	return;
    }

    /* ugh this is inefficient but easy */
    buf = (char *)malloc(size);
    if (!buf) {
	PUT_PCK_RES1 (packet, -1);
	PUT_PCK_RES2 (packet, ERROR_NO_FREE_STORE);
	return;
    }

    for (i = 0; i < size; i++)
	buf[i] = get_byte(addr + i);

    PUT_PCK_RES1 (packet, write(k->fd, buf, size));
    if (GET_PCK_RES1 (packet) != size)
	PUT_PCK_RES2 (packet, dos_errno ());
    if (GET_PCK_RES1 (packet) >= 0)
	k->file_pos += GET_PCK_RES1 (packet);

    free(buf);
}

static void
action_seek(Unit* unit, dpacket packet)
{
    Key* k = lookup_key (unit, GET_PCK_ARG1 (packet));
    long pos = (uae_s32)GET_PCK_ARG2 (packet);
    long mode = (uae_s32)GET_PCK_ARG3 (packet);
    off_t res;
    long old;
    int whence = SEEK_CUR;

    if (k == 0) {
	PUT_PCK_RES1 (packet, -1);
	PUT_PCK_RES2 (packet, ERROR_INVALID_LOCK);
	return;
    }

    if (mode > 0) whence = SEEK_END;
    if (mode < 0) whence = SEEK_SET;

    TRACE((warning_buffer, "ACTION_SEEK(%s,%d,%d)\n", k->aino->nname, pos, mode));

    old = lseek (k->fd, 0, SEEK_CUR);
    res = lseek (k->fd, pos, whence);

    if (-1 == res) {
	PUT_PCK_RES1 (packet, res);
	PUT_PCK_RES2 (packet, ERROR_SEEK_ERROR);
    } else
	PUT_PCK_RES1 (packet, old);
    k->file_pos = res;
}

static void
action_set_protect(Unit* unit, dpacket packet)
{
    uaecptr lock = GET_PCK_ARG2 (packet) << 2;
    uaecptr name = GET_PCK_ARG3 (packet) << 2;
    uae_u32 mask = GET_PCK_ARG4 (packet);
    mode_t mode;
    struct a_inode *a;
    uae_u32 err;

    TRACE((warning_buffer, "ACTION_SET_PROTECT(0x%lx,\"%s\",0x%lx)\n", lock, bstr (unit, name), mask));

    if (unit->ui.readonly) {
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, ERROR_DISK_WRITE_PROTECTED);
	return;
    }

    a = find_aino (unit, lock, bstr (unit, name), &err);
    if (err != 0) {
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, err);
	return;
    }

    err = set_file_attrs (a, mask);
    if (err != 0) {
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, err);
    } else {
	PUT_PCK_RES1 (packet, DOS_TRUE);
    }
}

static void
action_same_lock(Unit* unit, dpacket packet)
{
    uaecptr lock1 = GET_PCK_ARG1 (packet) << 2;
    uaecptr lock2 = GET_PCK_ARG2 (packet) << 2;

    TRACE((warning_buffer, "ACTION_SAME_LOCK(0x%lx,0x%lx)\n",lock1,lock2));
    DUMPLOCK(unit, lock1); DUMPLOCK(unit, lock2);

    if (!lock1 || !lock2) {
	PUT_PCK_RES1 (packet, lock1 == lock2 ? DOS_TRUE : DOS_FALSE);
    } else {
	PUT_PCK_RES1 (packet, get_long (lock1 + 4) == get_long (lock2 + 4) ? DOS_TRUE : DOS_FALSE);
    }
}

static void
action_parent(Unit* unit, dpacket packet)
{
    uaecptr lock = GET_PCK_ARG1 (packet) << 2;
    struct a_inode *a, *olda;

    TRACE((warning_buffer, "ACTION_PARENT(0x%lx)\n",lock));

    if (!lock) {
	PUT_PCK_RES1 (packet, 0);
	PUT_PCK_RES2 (packet, 0);
	return;
    }

    olda = lookup_aino (unit, get_long(lock + 4));
    if (olda == 0) {
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, ERROR_INVALID_LOCK);
	return;
    }

    if (olda->parent == 0) {
	PUT_PCK_RES1 (packet, 0);
	PUT_PCK_RES2 (packet, 0);
	return;
    }
    if (olda->parent->elock) {
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, ERROR_OBJECT_IN_USE);
	return;
    }
    olda->parent->shlock++;
    de_recycle_aino (unit, olda->parent);
    PUT_PCK_RES1 (packet, make_lock (unit, olda->parent->uniq, -2) >> 2);
}

static void
action_create_dir(Unit* unit, dpacket packet)
{
    uaecptr lock = GET_PCK_ARG1 (packet) << 2;
    uaecptr name = GET_PCK_ARG2 (packet) << 2;
    struct a_inode *aino;
    uae_u32 err;

    TRACE((warning_buffer, "ACTION_CREATE_DIR(0x%lx,\"%s\")\n", lock, bstr (unit, name)));

    if (unit->ui.readonly) {
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, ERROR_DISK_WRITE_PROTECTED);
	return;
    }

    aino = find_aino (unit, lock, bstr (unit, name), &err);
    if (aino == 0 || (err != 0 && err != ERROR_OBJECT_NOT_FOUND)) {
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, err);
	return;
    }
    if (err == 0) {
	/* Object exists. */
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, ERROR_OBJECT_EXISTS);
	return;
    }
    /* Object does not exist. aino points to containing directory. */
    aino = new_child_aino (unit, aino, my_strdup (bstr_cut (unit, name)), 2);
    if (aino == 0) {
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, ERROR_DISK_FULL); /* best we can do */
	return;
    }

    if (mkdir (aino->nname, 0777) == -1) {
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, dos_errno());
	return;
    }
    aino->shlock = 1;
    de_recycle_aino (unit, aino);
    PUT_PCK_RES1 (packet, make_lock (unit, aino->uniq, -2) >> 2);
}

static void
action_examine_fh(Unit* unit, dpacket packet)
{
    Key *k;
    ExamineKey* ek;
    struct a_inode *aino = 0;
    uaecptr info = GET_PCK_ARG2 (packet) << 2;

    TRACE((warning_buffer, "ACTION_EXAMINE_FH(0x%lx,0x%lx)\n", GET_PCK_ARG1 (packet), GET_PCK_ARG2 (packet) ));

    k = lookup_key (unit, GET_PCK_ARG1 (packet));
    if (k != 0)
	aino = k->aino;
    if (aino == 0)
	aino = &unit->rootnode;

    get_fileinfo (unit, packet, info, aino);
    if (aino->dir)
	put_long(info, 0xFFFFFFFF);
    else
	put_long(info, 0);
}

/* For a nice example of just how contradictory documentation can be, see the
 * Autodoc for DOS:SetFileSize and the Packets.txt description of this packet...
 * This implementation tries to mimic the behaviour of the Kick 3.1 ramdisk
 * (which seems to match the Autodoc description). */
static void
action_set_file_size(Unit* unit, dpacket packet)
{
    int error = 0;
    char chunk = 0;
    Key *k, *k1;
    off_t offset = GET_PCK_ARG2 (packet);
    long mode = (uae_s32)GET_PCK_ARG3 (packet);
    int whence = SEEK_CUR;
    
    if (mode > 0) whence = SEEK_END;
    if (mode < 0) whence = SEEK_SET;

    TRACE((warning_buffer, "ACTION_SET_FILE_SIZE(0x%lx, %d, 0x%x)\n", GET_PCK_ARG1 (packet), offset, mode));

    k = lookup_key (unit, GET_PCK_ARG1 (packet));
    if (k == 0) {
	PUT_PCK_RES1 (packet, DOS_TRUE);
	PUT_PCK_RES2 (packet, ERROR_OBJECT_NOT_FOUND);
	return;
    }

    /* If any open files have file pointers beyond this size, truncate only
     * so far that these pointers do not become invalid.  */
    for (k1 = unit->keys; k1; k1 = k1->next) {
	if (k != k1 && k->aino == k1->aino) {
	    if (k1->file_pos > offset)
		offset = k1->file_pos;
	}
    }

    /* Write one then truncate: that should give the right size in all cases.  */
    offset = lseek (k->fd, offset, whence);
    write (k->fd, /* whatever */(char *)&k1, 1);
    if (k->file_pos > offset)
	k->file_pos = offset;
    lseek (k->fd, k->file_pos, SEEK_SET);

    if (truncate (k->aino->nname, k->file_pos) == -1) {
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, dos_errno ());
	return;
    }

    PUT_PCK_RES1 (packet, offset);
    PUT_PCK_RES2 (packet, 0);
}

static void
action_delete_object(Unit* unit, dpacket packet)
{
    uaecptr lock = GET_PCK_ARG1 (packet) << 2;
    uaecptr name = GET_PCK_ARG2 (packet) << 2;
    struct a_inode *a;
    uae_u32 err;

    TRACE((warning_buffer, "ACTION_DELETE_OBJECT(0x%lx,\"%s\")\n", lock, bstr (unit, name)));

    if (unit->ui.readonly) {
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, ERROR_DISK_WRITE_PROTECTED);
	return;
    }

    a = find_aino (unit, lock, bstr (unit, name), &err);

    if (err != 0) {
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, err);
	return;
    }
    if (a->shlock > 0 || a->elock) {
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, ERROR_OBJECT_IN_USE);
	return;
    }
    if (a->dir) {
	if (rmdir (a->nname) == -1) {
	    PUT_PCK_RES1 (packet, DOS_FALSE);
	    PUT_PCK_RES2 (packet, dos_errno());
	    return;
	}
    } else {
	if (unlink (a->nname) == -1) {
	    PUT_PCK_RES1 (packet, DOS_FALSE);
	    PUT_PCK_RES2 (packet, dos_errno());
	    return;
	}
    }
    if (a->child != 0) {
	write_log ("Serious error in action_delete_object.\n");
    } else {
	delete_aino (unit, a);
    }
    PUT_PCK_RES1 (packet, DOS_TRUE);
}

static void
action_set_date(Unit* unit, dpacket packet)
{
    uaecptr lock = GET_PCK_ARG2 (packet) << 2;
    uaecptr name = GET_PCK_ARG3 (packet) << 2;
    uaecptr date = GET_PCK_ARG4 (packet);
    struct a_inode *a;
    struct utimbuf ut;
    uae_u32 err;

    TRACE((warning_buffer, "ACTION_SET_DATE(0x%lx,\"%s\")\n", lock, bstr (unit, name)));

    if(unit->ui.readonly) {
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, ERROR_DISK_WRITE_PROTECTED);
	return;
    }

    ut.actime = ut.modtime = put_time(get_long(date),get_long(date+4),get_long(date+8));
    a = find_aino (unit, lock, bstr (unit, name), &err);
    if (err == 0 && utime (a->nname, &ut) == -1)
	err = dos_errno ();
    if (err != 0) {
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, err);
    } else
	PUT_PCK_RES1 (packet, DOS_TRUE);
}

static void
action_rename_object(Unit* unit, dpacket packet)
{
    uaecptr lock1 = GET_PCK_ARG1 (packet) << 2;
    uaecptr name1 = GET_PCK_ARG2 (packet) << 2;
    uaecptr lock2 = GET_PCK_ARG3 (packet) << 2;
    uaecptr name2 = GET_PCK_ARG4 (packet) << 2;
    struct a_inode *a1, *a2;
    uae_u32 err1, err2;

    TRACE((warning_buffer, "ACTION_RENAME_OBJECT(0x%lx,\"%s\",", lock1, bstr (unit, name1)));
    TRACE((warning_buffer, "0x%lx,\"%s\")\n", lock2, bstr (unit, name2)));

    if(unit->ui.readonly) {
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, ERROR_DISK_WRITE_PROTECTED);
	return;
    }

    a1 = find_aino (unit, lock1, bstr (unit, name1), &err1);
    if (err1 != 0) {
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, err1);
	return;
    }
    a2 = find_aino (unit, lock2, bstr (unit, name2), &err2);
    if (a2 == 0 || err2 != ERROR_OBJECT_NOT_FOUND) {
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, err2 == 0 ? ERROR_OBJECT_EXISTS : err2);
	return;
    }
    /* Object does not exist. aino points to containing directory. */
    a2 = new_child_aino (unit, a2, my_strdup (bstr_cut (unit, name2)), a1->dir ? 2 : 1);
    if (a2 == 0) {
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, ERROR_DISK_FULL); /* best we can do */
	return;
    }

    /* @@@ what should we do if there are locks on a1? */
    if(-1 == rename(a1->nname, a2->nname)) {
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, dos_errno());
	return;
    }
    move_aino_children (unit, a1, a2);
    delete_aino (unit, a1);
    PUT_PCK_RES1 (packet, DOS_TRUE);
}

static void
action_current_volume(Unit* unit, dpacket packet)
{
    PUT_PCK_RES1 (packet, unit->volume >> 2);
}

static void
action_rename_disk(Unit* unit, dpacket packet)
{
    uaecptr name = GET_PCK_ARG1 (packet) << 2;
    int i;
    int namelen;

    TRACE((warning_buffer, "ACTION_RENAME_DISK(\"%s\")\n", bstr (unit, name)));

    if(unit->ui.readonly) {
	PUT_PCK_RES1 (packet, DOS_FALSE);
	PUT_PCK_RES2 (packet, ERROR_DISK_WRITE_PROTECTED);
	return;
    }

    /* get volume name */
    namelen = get_byte (name); name++;
    free (unit->ui.volname);
    unit->ui.volname = (char *) xmalloc (namelen + 1);
    for(i = 0; i < namelen; i++, name++)
	unit->ui.volname[i] = get_byte (name);
    unit->ui.volname[i] = 0;

    put_byte (unit->volume + 44, namelen);
    for(i = 0; i < namelen; i++)
	put_byte (unit->volume + 45 + i, unit->ui.volname[i]);

    PUT_PCK_RES1 (packet, DOS_TRUE);
}

static void
action_is_filesystem(Unit* unit, dpacket packet)
{
    PUT_PCK_RES1 (packet, DOS_TRUE);
}

static void
action_flush(Unit* unit, dpacket packet)
{
    /* sync(); */ /* pretty drastic, eh */
    PUT_PCK_RES1 (packet, DOS_TRUE);
}

/* We don't want multiple interrupts to be active at the same time. I don't
 * know whether AmigaOS takes care of that, but this does. */
static uae_sem_t singlethread_int_sem;

static uae_u32 exter_int_helper (void)
{
    static int unit_no;

    switch (m68k_dreg (regs, 0)) {
     case 0:
	/* Determine whether a given EXTER interrupt is for us. */
	if (uae_int_requested) {
	    if (uae_sem_trywait (&singlethread_int_sem) != 0)
		/* Pretend it isn't for us. We might get it again later. */
		return 0;
	    /* Clear the interrupt flag _before_ we do any processing.
	     * That way, we can get too many interrupts, but never not
	     * enough. */
	    uae_int_requested = 0;
	    unit_no = 0;
	    return 1;
	}
	return 0;
     case 1:
	/* Release a message_lock. This is called as soon as the message is
	 * received by the assembly code. We use the opportunity to check
	 * whether we have some locks that we can give back to the assembler
	 * code.
	 * Note that this is called from the main loop, unlike the other cases
	 * in this switch statement which are called from the interrupt handler.
	 */
	{
	    Unit *unit = find_unit (m68k_areg (regs, 5));
	    unit->cmds_complete = unit->cmds_acked;
	    while (comm_pipe_has_data (unit->ui.back_pipe)) {
		uaecptr locks, lockend;
		locks = read_comm_pipe_int_blocking (unit->ui.back_pipe);
		lockend = locks;
		while (get_long (lockend) != 0)
		    lockend = get_long (lockend);
		put_long (lockend, get_long (m68k_areg (regs, 3)));
		put_long (m68k_areg (regs, 3), locks);
	    }
	}
	break;
     case 2:
	/* Find some unit that needs a message sent, and return its port,
	 * or zero if all are done.
	 * Take care not to dereference self for units that didn't have their
	 * startup packet sent. */
	for (;;) {
	    if (unit_no >= num_units)
		return 0;
	    if (ui[unit_no].self != 0
		&& ui[unit_no].self->cmds_acked == ui[unit_no].self->cmds_complete
		&& ui[unit_no].self->cmds_acked != ui[unit_no].self->cmds_sent)
		break;
	    unit_no++;
	}
	ui[unit_no].self->cmds_acked = ui[unit_no].self->cmds_sent;
	return ui[unit_no++].self->port;

     case 3:
	/* Look up a message for a port. */
	{
	    Unit *unit = find_unit (m68k_areg (regs, 2));
	    return unit->dummy_message;
	}
	break;
     case 4:
	/* Exit the interrupt, and release the single-threading lock. */
	uae_sem_post (&singlethread_int_sem);
	break;

     default:
	write_log ("Shouldn't happen in exter_int_helper.\n");
	break;
    }
    return 0;
}

static int handle_packet (Unit *unit, dpacket pck)
{
    uae_s32 type = GET_PCK_TYPE (pck);
    PUT_PCK_RES2 (pck, 0);
    switch (type) {
     case ACTION_LOCATE_OBJECT: action_lock(unit, pck); break;
     case ACTION_FREE_LOCK: action_free_lock(unit, pck); break;
     case ACTION_COPY_DIR: action_dup_lock(unit, pck); break;
     case ACTION_DISK_INFO: action_disk_info(unit, pck); break;
     case ACTION_INFO: action_info(unit, pck); break;
     case ACTION_EXAMINE_OBJECT: action_examine_object(unit, pck); break;
     case ACTION_EXAMINE_NEXT: action_examine_next(unit, pck); break;
     case ACTION_FIND_INPUT: action_find_input(unit, pck); break;
     case ACTION_FIND_WRITE: action_find_write(unit, pck); break;
     case ACTION_FIND_OUTPUT: action_find_output(unit, pck); break;
     case ACTION_END: action_end(unit, pck); break;
     case ACTION_READ: action_read(unit, pck); break;
     case ACTION_WRITE: action_write(unit, pck); break;
     case ACTION_SEEK: action_seek(unit, pck); break;
     case ACTION_SET_PROTECT: action_set_protect(unit, pck); break;
     case ACTION_SAME_LOCK: action_same_lock(unit, pck); break;
     case ACTION_PARENT: action_parent(unit, pck); break;
     case ACTION_CREATE_DIR: action_create_dir(unit, pck); break;
     case ACTION_DELETE_OBJECT: action_delete_object(unit, pck); break;
     case ACTION_RENAME_OBJECT: action_rename_object(unit, pck); break;
     case ACTION_SET_DATE: action_set_date(unit, pck); break;
     case ACTION_CURRENT_VOLUME: action_current_volume(unit, pck); break;
     case ACTION_RENAME_DISK: action_rename_disk(unit, pck); break;
     case ACTION_IS_FILESYSTEM: action_is_filesystem(unit, pck); break;
     case ACTION_FLUSH: action_flush(unit, pck); break;

     /* 2.0+ packet types */
     case ACTION_SET_FILE_SIZE: action_set_file_size(unit, pck); break;
     case ACTION_EXAMINE_FH: action_examine_fh(unit, pck); break;

     /* unsupported packets */
     case ACTION_LOCK_RECORD:
     case ACTION_FREE_RECORD:
     case ACTION_CHANGE_MODE:
     case ACTION_FH_FROM_LOCK:
     case ACTION_COPY_DIR_FH:
     case ACTION_PARENT_FH:
     case ACTION_EXAMINE_ALL:
     case ACTION_MAKE_LINK:
     case ACTION_READ_LINK:
     case ACTION_FORMAT:
     case ACTION_ADD_NOTIFY:
     case ACTION_REMOVE_NOTIFY:
     default:
	TRACE((warning_buffer, "*** UNSUPPORTED PACKET %ld\n", type));
	return 0;
    }
    return 1;
}

#ifdef UAE_FILESYS_THREADS
static void *filesys_penguin (void *unit_v)
{
    UnitInfo *ui = (UnitInfo *)unit_v;
    for (;;) {
	uae_u8 *pck;
	uae_u8 *msg;
	uae_u32 morelocks;
	int i;

	pck = (uae_u8 *)read_comm_pipe_pvoid_blocking (ui->unit_pipe);
	msg = (uae_u8 *)read_comm_pipe_pvoid_blocking (ui->unit_pipe);
	morelocks = (uae_u32)read_comm_pipe_int_blocking (ui->unit_pipe);

	if (ui->self->reset_state == FS_GO_DOWN) {
	    if (pck != 0)
		continue;
	    /* Death message received. */
	    uae_sem_post (&ui->self->reset_sync_sem);
	    /* No more messages should arrive until we restart. */
	    continue;
	}

	put_long (get_long (morelocks), get_long (ui->self->locklist));
	put_long (ui->self->locklist, morelocks);
	if (! handle_packet (ui->self, pck)) {
	    PUT_PCK_RES1 (pck, DOS_FALSE);
	    PUT_PCK_RES2 (pck, ERROR_ACTION_NOT_KNOWN);
	}
	/* Mark the packet as processed for the list scan in the assembly code. */
	do_put_mem_long ((uae_u32 *)(msg + 4), -1);
	/* Acquire the message lock, so that we know we can safely send the
	 * message. */
	ui->self->cmds_sent++;
	/* The message is sent by our interrupt handler, so make sure an interrupt
	 * happens. */
	uae_int_requested = 1;
	/* Send back the locks. */
	if (get_long (ui->self->locklist) != 0)
	    write_comm_pipe_int (ui->back_pipe, (int)(get_long(ui->self->locklist)), 0);
	put_long (ui->self->locklist, 0);
    }
    return 0;
}
#endif

/* Talk about spaghetti code... */
static uae_u32 filesys_handler(void)
{
    Unit *unit = find_unit (m68k_areg (regs, 5));
    uaecptr packet_addr = m68k_dreg (regs, 3);
    uaecptr message_addr = m68k_areg (regs, 4);
    uae_u8 *pck;
    uae_u8 *msg;
    if (! valid_address (packet_addr, 36) || ! valid_address (message_addr, 14)) {
	write_log ("Bad address passed for packet.\n");
	goto error2;
    }
    pck = get_real_address (packet_addr);
    msg = get_real_address (message_addr);

    if (unit->reset_state == FS_GO_DOWN)
	/* You might as well queue it, if you live long enough */
	return 1;

    do_put_mem_long ((uae_u32 *)(msg + 4), -1);
    if (!unit || !unit->volume) {
	write_log ("Filesystem was not initialized.\n");
	goto error;
    }
#ifdef UAE_FILESYS_THREADS
    {
	/* Get two more locks and hand them over to the other thread. */
	uae_u32 morelocks;
	morelocks = get_long (m68k_areg (regs, 3));
	put_long (m68k_areg (regs, 3), get_long (get_long (morelocks)));
	put_long (get_long (morelocks), 0);

	/* The packet wasn't processed yet. */
	do_put_mem_long ((uae_u32 *)(msg + 4), 0);
	write_comm_pipe_pvoid (unit->ui.unit_pipe, (void *)pck, 0);
	write_comm_pipe_pvoid (unit->ui.unit_pipe, (void *)msg, 0);
	write_comm_pipe_int (unit->ui.unit_pipe, (int)morelocks, 1);
	/* Don't reply yet. */
	return 1;
    }
#endif

    if (! handle_packet (unit, pck)) {
	error:
	PUT_PCK_RES1 (pck, DOS_FALSE);
	PUT_PCK_RES2 (pck, ERROR_ACTION_NOT_KNOWN);
    }
    TRACE((warning_buffer, "reply: %8lx, %ld\n", GET_PCK_RES1 (pck), GET_PCK_RES2 (pck)));

    error2:

    return 0;
}

void filesys_reset (void)
{
    Unit *u, *u1;
    int i;

    for (i = 0; i < num_units; i++) {
	ui[i].self = 0;
    }

    for(u = units; u; u = u1) {
	u1 = u->next;
	free (u);
    }
    unit_num = 0;
    units = 0;
}

void filesys_prepare_reset (void)
{
    Unit *u, *u1;
    int i;

#ifdef UAE_FILESYS_THREADS
    for (i = 0; i < num_units; i++) {
	if (ui[i].self != 0) {
	    uae_sem_init (&ui[i].self->reset_sync_sem, 0, 0);
	    ui[i].self->reset_state = FS_GO_DOWN;
	    /* send death message */
	    write_comm_pipe_int (ui[i].unit_pipe, 0, 0);
	    write_comm_pipe_int (ui[i].unit_pipe, 0, 0);
	    write_comm_pipe_int (ui[i].unit_pipe, 0, 1);
	    uae_sem_wait (&ui[i].self->reset_sync_sem);
	}
    }
#endif
    u = units;
    while (u != 0) {
	while (u->rootnode.next != &u->rootnode) {
	    struct a_inode *a = u->rootnode.next;
	    u->rootnode.next = a->next;
	    free (a->nname);
	    free (a->aname);
	    free (a);
	}
	u = u->next;
    }
}

static uae_u32 filesys_diagentry(void)
{
    uaecptr resaddr = m68k_areg(regs, 2) + 0x10;

    TRACE ((warning_buffer, "filesystem: diagentry called\n"));

    filesys_configdev = m68k_areg(regs, 3);

    do_put_mem_long ((uae_u32 *)(filesysory + 0x2100), EXPANSION_explibname);
    do_put_mem_long ((uae_u32 *)(filesysory + 0x2104), filesys_configdev);
    do_put_mem_long ((uae_u32 *)(filesysory + 0x2108), EXPANSION_doslibname);
    do_put_mem_long ((uae_u32 *)(filesysory + 0x210c), num_units);

    uae_sem_init (&singlethread_int_sem, 0, 1);
    if (ROM_hardfile_resid != 0) {
	/* Build a struct Resident. This will set up and initialize
	 * the uae.device */
	put_word(resaddr + 0x0, 0x4AFC);
	put_long(resaddr + 0x2, resaddr);
	put_long(resaddr + 0x6, resaddr + 0x1A); /* Continue scan here */
	put_word(resaddr + 0xA, 0x8101); /* RTF_AUTOINIT|RTF_COLDSTART; Version 1 */
	put_word(resaddr + 0xC, 0x0305); /* NT_DEVICE; pri 05 */
	put_long(resaddr + 0xE, ROM_hardfile_resname);
	put_long(resaddr + 0x12, ROM_hardfile_resid);
	put_long(resaddr + 0x16, ROM_hardfile_init); /* calls filesys_init */
    }
    resaddr += 0x1A;

    filesys_reset ();
    /* The good thing about this function is that it always gets called
     * when we boot. So we could put all sorts of stuff that wants to be done
     * here. */

    return 1;
}

/* Remember a pointer AmigaOS gave us so we can later use it to identify
 * which unit a given startup message belongs to.  */
static uae_u32 filesys_dev_remember(void)
{
    int unit_no = m68k_dreg (regs, 6);
    uaecptr devicenode = m68k_areg (regs, 3);

    ui[unit_no].startup = get_long (devicenode + 28);
    return devicenode;
}

/* Fill in per-unit fields of a parampacket */
static uae_u32 filesys_dev_storeinfo(void)
{
    int unit_no = m68k_dreg (regs, 6);
    uaecptr parmpacket = m68k_areg (regs, 0);
    int is_hardfile = ui[unit_no].volname == 0;

    put_long (parmpacket, ui[unit_no].devname_amiga);
    put_long (parmpacket + 4, is_hardfile ? ROM_hardfile_resname : fsdevname);
    put_long (parmpacket + 8, ui[unit_no].devno);
    put_long (parmpacket + 12, 0); /* Device flags */
    put_long (parmpacket + 16, 16); /* Env. size */
    put_long (parmpacket + 20, 128); /* 512 bytes per block */
    put_long (parmpacket + 24, 0); /* unused */
    put_long (parmpacket + 28, ui[unit_no].hf.surfaces); /* heads */
    put_long (parmpacket + 32, 0); /* unused */
    put_long (parmpacket + 36, ui[unit_no].hf.secspertrack); /* sectors per track */
    put_long (parmpacket + 40, ui[unit_no].hf.reservedblocks); /* reserved blocks */
    put_long (parmpacket + 44, 0); /* unused */
    put_long (parmpacket + 48, 0); /* interleave */
    put_long (parmpacket + 52, 0); /* lowCyl */
    put_long (parmpacket + 56, ui[unit_no].hf.nrcyls - 1); /* hiCyl */
    put_long (parmpacket + 60, 0); /* Number of buffers */
    put_long (parmpacket + 64, 0); /* Buffer mem type */
    put_long (parmpacket + 68, 0x7FFFFFFF); /* largest transfer */
    put_long (parmpacket + 72, ~1); /* addMask (?) */
    put_long (parmpacket + 76, (uae_u32)-1); /* bootPri */
    put_long (parmpacket + 80, 0x444f5300); /* DOS\0 */
    put_long (parmpacket + 84, 0); /* pad */

    return is_hardfile;
}

/* Called exactly once, at the time we know that we are actually going
 * to run the emulator. */
void filesys_start_threads (void)
{
    int i;
    for (i = 0; i < num_units; i++) {
	int is_hardfile = ui[i].volname == 0;
	ui[i].unit_pipe = 0;
#ifdef UAE_FILESYS_THREADS
	if (!is_hardfile) {
	    ui[i].unit_pipe = (smp_comm_pipe *)xmalloc (sizeof (smp_comm_pipe));
	    ui[i].back_pipe = (smp_comm_pipe *)xmalloc (sizeof (smp_comm_pipe));
	    init_comm_pipe (ui[i].unit_pipe, 50, 3);
	    init_comm_pipe (ui[i].back_pipe, 50, 1);
	    start_penguin (filesys_penguin, (void *)(ui + i), &ui[i].tid);
	}
#endif
    }
}

void filesys_install (void)
{
    int i;
    uaecptr loop;

    TRACE ((warning_buffer, "Installing filesystem\n"));

    ROM_filesys_resname = ds("UAEunixfs.resource");
    ROM_filesys_resid = ds("UAE unixfs 0.4");

    fsdevname = ds("uae.device"); /* does not really exist */

    for(i = 0; i < num_units; i++) {
	ui[i].devno = get_new_device(&ui[i].devname, &ui[i].devname_amiga);
    }

    ROM_filesys_diagentry = here();
    calltrap (deftrap(filesys_diagentry)); dw(RTS);

    loop = here ();
    /* Special trap for the assembly make_dev routine */
    org(0xF0FF20);
    calltrap (deftrap (filesys_dev_remember));
    dw(RTS);

    org(0xF0FF28);
    calltrap (deftrap (filesys_dev_storeinfo));
    dw(RTS);

    org(0xF0FF30);
    calltrap (deftrap (filesys_handler));
    dw(RTS);

    org(0xF0FF40);
    calltrap (deftrap (startup_handler));
    dw(RTS);

    org(0xF0FF50);
    calltrap (deftrap (exter_int_helper));
    dw(RTS);

    org(0xF0FF70);
    calltrap (deftrap (mousehack_helper));
    dw(RTS);

    org (loop);
}

void filesys_install_code (void)
{
    align(4);

    /* The last offset comes from the code itself, look for it near the top. */
    filesys_initcode = here() + 8 + 0x6c;
    EXPANSION_bootcode = here () + 8 + 0x14;
    /* Ouch. Make sure this is _always_ a multiple of two bytes. */
 db(0x00); db(0x00); db(0x00); db(0x10); db(0x00); db(0x00); db(0x00); db(0x00);
 db(0x60); db(0x00); db(0x01); db(0xc0); db(0x00); db(0x00); db(0x01); db(0x2c);
 db(0x00); db(0x00); db(0x00); db(0x6c); db(0x00); db(0x00); db(0x00); db(0x28);
 db(0x00); db(0x00); db(0x00); db(0x14); db(0x43); db(0xfa); db(0x02); db(0xf9);
 db(0x4e); db(0xae); db(0xff); db(0xa0); db(0x20); db(0x40); db(0x20); db(0x28);
 db(0x00); db(0x16); db(0x20); db(0x40); db(0x4e); db(0x90); db(0x4e); db(0x75);
 db(0x48); db(0xe7); db(0x00); db(0x20); db(0x70); db(0x00); db(0x4e); db(0xb9);
 db(0x00); db(0xf0); db(0xff); db(0x50); db(0x4a); db(0x80); db(0x67); db(0x2e);
 db(0x2c); db(0x78); db(0x00); db(0x04); db(0x70); db(0x02); db(0x4e); db(0xb9);
 db(0x00); db(0xf0); db(0xff); db(0x50); db(0x4a); db(0x80); db(0x67); db(0x14);
 db(0x24); db(0x40); db(0x70); db(0x03); db(0x4e); db(0xb9); db(0x00); db(0xf0);
 db(0xff); db(0x50); db(0x20); db(0x4a); db(0x22); db(0x40); db(0x4e); db(0xae);
 db(0xfe); db(0x92); db(0x60); db(0xe0); db(0x70); db(0x04); db(0x4e); db(0xb9);
 db(0x00); db(0xf0); db(0xff); db(0x50); db(0x70); db(0x01); db(0x4c); db(0xdf);
 db(0x04); db(0x00); db(0x4e); db(0x75); db(0x48); db(0xe7); db(0xff); db(0xfe);
 db(0x2c); db(0x78); db(0x00); db(0x04); db(0x2a); db(0x79); db(0x00); db(0xf0);
 db(0xff); db(0xfc); db(0x43); db(0xfa); db(0x02); db(0x9f); db(0x70); db(0x24);
 db(0x7a); db(0x00); db(0x4e); db(0xae); db(0xfd); db(0xd8); db(0x4a); db(0x80);
 db(0x66); db(0x0c); db(0x43); db(0xfa); db(0x02); db(0x8f); db(0x70); db(0x00);
 db(0x7a); db(0x01); db(0x4e); db(0xae); db(0xfd); db(0xd8); db(0x28); db(0x40);
 db(0x70); db(0x58); db(0x72); db(0x01); db(0x4e); db(0xae); db(0xff); db(0x3a);
 db(0x26); db(0x40); db(0x7e); db(0x54); db(0x27); db(0xb5); db(0x78); db(0x00);
 db(0x78); db(0x00); db(0x59); db(0x87); db(0x64); db(0xf6); db(0x7c); db(0x00);
 db(0xbc); db(0xad); db(0x01); db(0x0c); db(0x64); db(0x12); db(0x20); db(0x4b);
 db(0x48); db(0xe7); db(0x02); db(0x10); db(0x7e); db(0x01); db(0x61); db(0x6c);
 db(0x4c); db(0xdf); db(0x08); db(0x40); db(0x52); db(0x86); db(0x60); db(0xe8);
 db(0x2c); db(0x78); db(0x00); db(0x04); db(0x22); db(0x4c); db(0x4e); db(0xae);
 db(0xfe); db(0x62); db(0x61); db(0x28); db(0x2c); db(0x78); db(0x00); db(0x04);
 db(0x4e); db(0xb9); db(0x00); db(0xf0); db(0xff); db(0x80); db(0x72); db(0x03);
 db(0x74); db(0xf6); db(0x20); db(0x7c); db(0x00); db(0x20); db(0x00); db(0x00);
 db(0x90); db(0x88); db(0x65); db(0x0a); db(0x67); db(0x08); db(0x78); db(0x00);
 db(0x22); db(0x44); db(0x4e); db(0xae); db(0xfd); db(0x96); db(0x4c); db(0xdf);
 db(0x7f); db(0xff); db(0x4e); db(0x75); db(0x2c); db(0x78); db(0x00); db(0x04);
 db(0x70); db(0x1a); db(0x22); db(0x3c); db(0x00); db(0x01); db(0x00); db(0x01);
 db(0x4e); db(0xae); db(0xff); db(0x3a); db(0x22); db(0x40); db(0x41); db(0xfa);
 db(0x01); db(0xf0); db(0x23); db(0x48); db(0x00); db(0x0a); db(0x41); db(0xfa);
 db(0xff); db(0x10); db(0x23); db(0x48); db(0x00); db(0x0e); db(0x41); db(0xfa);
 db(0xff); db(0x08); db(0x23); db(0x48); db(0x00); db(0x12); db(0x70); db(0x0d);
 db(0x4e); db(0xee); db(0xff); db(0x58); db(0x2a); db(0x79); db(0x00); db(0xf0);
 db(0xff); db(0xfc); db(0x4e); db(0xb9); db(0x00); db(0xf0); db(0xff); db(0x28);
 db(0x26); db(0x00); db(0xc0); db(0x85); db(0x66); db(0xbc); db(0x2c); db(0x4c);
 db(0x4e); db(0xae); db(0xff); db(0x70); db(0x26); db(0x40); db(0x4e); db(0xb9);
 db(0x00); db(0xf0); db(0xff); db(0x20); db(0x70); db(0x00); db(0x27); db(0x40);
 db(0x00); db(0x08); db(0x27); db(0x40); db(0x00); db(0x10); db(0x27); db(0x40);
 db(0x00); db(0x20); db(0x4a); db(0x83); db(0x66); db(0x1c); db(0x27); db(0x7c);
 db(0x00); db(0x00); db(0x0f); db(0xa0); db(0x00); db(0x14); db(0x43); db(0xfa);
 db(0xfe); db(0x94); db(0x20); db(0x09); db(0xe4); db(0x88); db(0x27); db(0x40);
 db(0x00); db(0x20); db(0x27); db(0x7c); db(0xff); db(0xff); db(0xff); db(0xff);
 db(0x00); db(0x24); db(0x4a); db(0x87); db(0x67); db(0x36); db(0x2c); db(0x78);
 db(0x00); db(0x04); db(0x70); db(0x14); db(0x72); db(0x00); db(0x4e); db(0xae);
 db(0xff); db(0x3a); db(0x22); db(0x40); db(0x70); db(0x00); db(0x22); db(0x80);
 db(0x23); db(0x40); db(0x00); db(0x04); db(0x33); db(0x40); db(0x00); db(0x0e);
 db(0x30); db(0x3c); db(0x10); db(0xff); db(0x90); db(0x06); db(0x33); db(0x40);
 db(0x00); db(0x08); db(0x23); db(0x6d); db(0x01); db(0x04); db(0x00); db(0x0a);
 db(0x23); db(0x4b); db(0x00); db(0x10); db(0x41); db(0xec); db(0x00); db(0x4a);
 db(0x4e); db(0xee); db(0xfe); db(0xf2); db(0x20); db(0x4b); db(0x72); db(0x00);
 db(0x22); db(0x41); db(0x70); db(0xff); db(0x2c); db(0x4c); db(0x4e); db(0xee);
 db(0xff); db(0x6a); db(0x2c); db(0x78); db(0x00); db(0x04); db(0x70); db(0x00);
 db(0x22); db(0x40); db(0x4e); db(0xae); db(0xfe); db(0xda); db(0x20); db(0x40);
 db(0x4b); db(0xe8); db(0x00); db(0x5c); db(0x43); db(0xfa); db(0x01); db(0x39);
 db(0x70); db(0x00); db(0x4e); db(0xae); db(0xfd); db(0xd8); db(0x24); db(0x40);
 db(0x20); db(0x3c); db(0x00); db(0x00); db(0x00); db(0x9d); db(0x22); db(0x3c);
 db(0x00); db(0x01); db(0x00); db(0x01); db(0x4e); db(0xae); db(0xff); db(0x3a);
 db(0x26); db(0x40); db(0x7c); db(0x00); db(0x26); db(0x86); db(0x27); db(0x46);
 db(0x00); db(0x04); db(0x27); db(0x46); db(0x00); db(0x08); db(0x7a); db(0x00);
 db(0x20); db(0x4d); db(0x4e); db(0xae); db(0xfe); db(0x80); db(0x20); db(0x4d);
 db(0x4e); db(0xae); db(0xfe); db(0x8c); db(0x28); db(0x40); db(0x26); db(0x2c);
 db(0x00); db(0x0a); db(0x70); db(0x00); db(0x4e); db(0xb9); db(0x00); db(0xf0);
 db(0xff); db(0x40); db(0x60); db(0x74); db(0x20); db(0x4d); db(0x4e); db(0xae);
 db(0xfe); db(0x80); db(0x20); db(0x4d); db(0x4e); db(0xae); db(0xfe); db(0x8c);
 db(0x28); db(0x40); db(0x26); db(0x2c); db(0x00); db(0x0a); db(0x66); db(0x38);
 db(0x70); db(0x01); db(0x4e); db(0xb9); db(0x00); db(0xf0); db(0xff); db(0x50);
 db(0x45); db(0xeb); db(0x00); db(0x04); db(0x20); db(0x52); db(0x20); db(0x08);
 db(0x67); db(0xda); db(0x22); db(0x50); db(0x20); db(0x40); db(0x20); db(0x28);
 db(0x00); db(0x04); db(0x6a); db(0x16); db(0x48); db(0xe7); db(0x00); db(0xc0);
 db(0x28); db(0x68); db(0x00); db(0x0a); db(0x61); db(0x40); db(0x53); db(0x85);
 db(0x4c); db(0xdf); db(0x03); db(0x00); db(0x24); db(0x89); db(0x20); db(0x49);
 db(0x60); db(0xdc); db(0x24); db(0x48); db(0x20); db(0x49); db(0x60); db(0xd6);
 db(0xba); db(0xbc); db(0x00); db(0x00); db(0x00); db(0x14); db(0x65); db(0x08);
 db(0x70); db(0x01); db(0x29); db(0x40); db(0x00); db(0x04); db(0x60); db(0x0e);
 db(0x61); db(0x2a); db(0x4e); db(0xb9); db(0x00); db(0xf0); db(0xff); db(0x30);
 db(0x4a); db(0x80); db(0x67); db(0x0c); db(0x52); db(0x85); db(0x28); db(0xab);
 db(0x00); db(0x04); db(0x27); db(0x4c); db(0x00); db(0x04); db(0x60); db(0x8c);
 db(0x28); db(0x43); db(0x61); db(0x02); db(0x60); db(0x86); db(0x22); db(0x54);
 db(0x20); db(0x6c); db(0x00); db(0x04); db(0x29); db(0x4d); db(0x00); db(0x04);
 db(0x4e); db(0xee); db(0xfe); db(0x92); db(0x2f); db(0x05); db(0x7a); db(0xfc);
 db(0x24); db(0x53); db(0x2e); db(0x0a); db(0x22); db(0x0a); db(0x67); db(0x0a);
 db(0x52); db(0x85); db(0x67); db(0x1e); db(0x22); db(0x4a); db(0x24); db(0x52);
 db(0x60); db(0xf2); db(0x52); db(0x85); db(0x67); db(0x3c); db(0x24); db(0x47);
 db(0x70); db(0x18); db(0x72); db(0x01); db(0x4e); db(0xae); db(0xff); db(0x3a);
 db(0x52); db(0x46); db(0x24); db(0x40); db(0x24); db(0x87); db(0x2e); db(0x0a);
 db(0x60); db(0xe8); db(0x20); db(0x12); db(0x67); db(0x24); db(0x20); db(0x40);
 db(0x20); db(0x10); db(0x67); db(0x1e); db(0x20); db(0x40); db(0x20); db(0x10);
 db(0x67); db(0x18); db(0x70); db(0x00); db(0x22); db(0x80); db(0x22); db(0x4a);
 db(0x24); db(0x51); db(0x70); db(0x18); db(0x4e); db(0xae); db(0xff); db(0x2e);
 db(0xdc); db(0xbc); db(0x00); db(0x01); db(0x00); db(0x00); db(0x20); db(0x0a);
 db(0x66); db(0xec); db(0x26); db(0x87); db(0x2a); db(0x1f); db(0x4e); db(0x75);
 db(0x55); db(0x41); db(0x45); db(0x20); db(0x66); db(0x69); db(0x6c); db(0x65);
 db(0x73); db(0x79); db(0x73); db(0x74); db(0x65); db(0x6d); db(0x00); db(0x64);
 db(0x6f); db(0x73); db(0x2e); db(0x6c); db(0x69); db(0x62); db(0x72); db(0x61);
 db(0x72); db(0x79); db(0x00); db(0x65); db(0x78); db(0x70); db(0x61); db(0x6e);
 db(0x73); db(0x69); db(0x6f); db(0x6e); db(0x2e); db(0x6c); db(0x69); db(0x62);
 db(0x72); db(0x61); db(0x72); db(0x79); db(0x00); db(0x00);
}
