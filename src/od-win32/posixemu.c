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
#include "memory.h"
#include "osdep/win32.h"

/* stdioemu, posixemu, mallocemu, and various file system helper routines */
static DWORD lasterror;

static int isillegal (unsigned char *str)
{
    unsigned char a = *str, b = str[1], c = str[2];

    if (a >= 'a' && a <= 'z')
	a &= ~' ';
    if (b >= 'a' && b <= 'z')
	b &= ~' ';
    if (c >= 'a' && c <= 'z')
	c &= ~' ';

    return (a == 'A' && b == 'U' && c == 'X' ||
	    a == 'C' && b == 'O' && c == 'N' ||
	    a == 'P' && b == 'R' && c == 'N' ||
	    a == 'N' && b == 'U' && c == 'L');
}

static int checkspace (char *str, char s, char d)
{
    char *ptr = str;

    while (*ptr && *ptr == s)
	ptr++;

    if (!*ptr || *ptr == '/' || *ptr == '\\') {
	while (str < ptr)
	    *(str++) = d;
	return 0;
    }
    return 1;
}

/* This is sick and incomplete... in the meantime, I have discovered six new illegal file name formats
 * M$ sucks! */
void fname_atow (char *src, char *dst, int size)
{
    char *lastslash = dst, *strt = dst;
    int i, j;

    while (size-- > 0) {
	if (!(*dst = *src++))
	    break;

	if (*dst == '~' || *dst == '|' || *dst == '*' || *dst == '?') {
	    if (size > 2) {
		sprintf (dst, "~%02x", *dst);
		size -= 2;
		dst += 2;
	    }
	} else if (*dst == '/') {
	    if (checkspace (lastslash, ' ', 0xa0) && (dst - lastslash == 3 || (dst - lastslash > 3 && lastslash[3] == '.')) && isillegal (lastslash)) {
		i = dst - lastslash - 3;
		dst++;
		for (j = i + 1; j--; dst--)
		    *dst = dst[-1];
		*(dst++) = 0xa0;
		dst += i;
		size--;
	    } else if (*lastslash == '.' && (dst - lastslash == 1 || lastslash[1] == '.' && dst - lastslash == 2) && size) {
		*(dst++) = 0xa0;
		size--;
	    }
	    *dst = '\\';
	    lastslash = dst + 1;
	}
	dst++;
    }

    if (checkspace (lastslash, ' ', 0xa0) && (dst - lastslash == 3 || (dst - lastslash > 3 && lastslash[3] == '.')) && isillegal (lastslash) && size > 1) {
	i = dst - lastslash - 3;
	dst++;
	for (j = i + 1; j--; dst--)
	    *dst = dst[-1];
	*(dst++) = 0xa0;
    } else if (!strcmp (lastslash, ".") || !strcmp (lastslash, ".."))
	strcat (lastslash, "\xa0");
}

int getdiskfreespace (char *name, int *f_blocks, int *f_bavail, int *f_bsize)
{
    char buf1[1024];
    char buf2[1024];
    DWORD SectorsPerCluster;
    DWORD BytesPerSector;
    DWORD NumberOfFreeClusters;
    DWORD TotalNumberOfClusters;

    fname_atow (name, buf1, sizeof buf1);

    GetFullPathName (buf1, sizeof buf2, buf2, NULL);

    buf2[3] = 0;

    if (!GetDiskFreeSpace (buf2, &SectorsPerCluster, &BytesPerSector, &NumberOfFreeClusters, &TotalNumberOfClusters)) {
	lasterror = GetLastError ();
	return FALSE;
    }
    *f_blocks = TotalNumberOfClusters;
    *f_bavail = NumberOfFreeClusters;
    *f_bsize = SectorsPerCluster * BytesPerSector;

    return TRUE;
}

/* Translate lasterror to valid AmigaDOS error code
 * The mapping is probably not 100% correct yet */
long dos_errno (void)
{
    int i;

    static DWORD errtbl[][2] =
    {
	{ERROR_FILE_NOT_FOUND, 205},	/* 2 */
	{ERROR_PATH_NOT_FOUND, 205},	/* 3 */
	{ERROR_SHARING_VIOLATION, 202},
	{ERROR_ACCESS_DENIED, 223},	/* 5 */
	{ERROR_ARENA_TRASHED, 103},	/* 7 */
	{ERROR_NOT_ENOUGH_MEMORY, 103},		/* 8 */
	{ERROR_INVALID_BLOCK, 219},	/* 9 */
	{ERROR_INVALID_DRIVE, 204},	/* 15 */
	{ERROR_CURRENT_DIRECTORY, 214},		/* 16 */
	{ERROR_NO_MORE_FILES, 232},	/* 18 */
	{ERROR_LOCK_VIOLATION, 214},	/* 33 */
	{ERROR_BAD_NETPATH, 204},	/* 53 */
	{ERROR_NETWORK_ACCESS_DENIED, 214},	/* 65 */
	{ERROR_BAD_NET_NAME, 204},	/* 67 */
	{ERROR_FILE_EXISTS, 203},	/* 80 */
	{ERROR_CANNOT_MAKE, 214},	/* 82 */
	{ERROR_FAIL_I24, 223},	/* 83 */
	{ERROR_DRIVE_LOCKED, 202},	/* 108 */
	{ERROR_DISK_FULL, 221},		/* 112 */
	{ERROR_NEGATIVE_SEEK, 219},	/* 131 */
	{ERROR_SEEK_ON_DEVICE, 219},	/* 132 */
	{ERROR_DIR_NOT_EMPTY, 216},	/* 145 */
	{ERROR_ALREADY_EXISTS, 203},	/* 183 */
	{ERROR_FILENAME_EXCED_RANGE, 205},	/* 206 */
	{ERROR_NOT_ENOUGH_QUOTA, 221},	/* 1816 */
	{ERROR_DIRECTORY, 212}};

    for (i = sizeof (errtbl) / sizeof (errtbl[0]); i--;) {
	if (errtbl[i][0] == lasterror)
	    return errtbl[i][1];
    }
    return 236;
}

static DWORD getattr (char *name, LPFILETIME lpft, size_t * size)
{
    HANDLE hFind;
    WIN32_FIND_DATA fd;

    if ((hFind = FindFirstFile (name, &fd)) == INVALID_HANDLE_VALUE) {
	lasterror = GetLastError ();

	fd.dwFileAttributes = GetFileAttributes (name);

	return fd.dwFileAttributes;
    }
    FindClose (hFind);

    if (lpft)
	*lpft = fd.ftLastWriteTime;
    if (size)
	*size = fd.nFileSizeLow;

    return fd.dwFileAttributes;
}

int posixemu_access (char *name, int mode)
{
    DWORD attr;
    char buf[1024];

    fname_atow (name, buf, sizeof buf - 4);

    if ((attr = getattr (buf, NULL, NULL)) == ~0)
	return -1;

    if (attr & FILE_ATTRIBUTE_READONLY && (mode & 4)) {
	lasterror = ERROR_ACCESS_DENIED;
	return -1;
    } else
	return 0;
}

int posixemu_open (char *name, int oflag, int dummy)
{
    DWORD fileaccess;
    DWORD filecreate;
    char buf[1024];

    HANDLE hFile;

    switch (oflag & (O_RDONLY | O_WRONLY | O_RDWR)) {
    case O_RDONLY:
	fileaccess = GENERIC_READ;
	break;
    case O_WRONLY:
	fileaccess = GENERIC_WRITE;
	break;
    case O_RDWR:
	fileaccess = GENERIC_READ | GENERIC_WRITE;
	break;
    default:
	return -1;
    }

    switch (oflag & (O_CREAT | O_EXCL | O_TRUNC)) {
    case 0:
    case O_EXCL:
	filecreate = OPEN_EXISTING;
	break;
    case O_CREAT:
	filecreate = OPEN_ALWAYS;
	break;
    case O_CREAT | O_EXCL:
    case O_CREAT | O_TRUNC | O_EXCL:
	filecreate = CREATE_NEW;
	break;
    case O_TRUNC:
    case O_TRUNC | O_EXCL:
	filecreate = TRUNCATE_EXISTING;
	break;
    case O_CREAT | O_TRUNC:
	filecreate = CREATE_ALWAYS;
	break;
    }

    fname_atow (name, buf, sizeof buf);

    if ((hFile = CreateFile (buf,
			     fileaccess,
			     FILE_SHARE_READ | FILE_SHARE_WRITE,
			     NULL,
			     filecreate,
			     FILE_ATTRIBUTE_NORMAL,
			     NULL)) == INVALID_HANDLE_VALUE)
	lasterror = GetLastError ();

    return (int) hFile;
}

void posixemu_close (int hFile)
{
    CloseHandle ((HANDLE) hFile);
}

int w32fopendel (char *name, char *mode, int delflag)
{
    HANDLE hFile;
    hFile = CreateFile (name,
			mode[1] == '+' ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL,
			OPEN_EXISTING,
			delflag ? FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE : FILE_ATTRIBUTE_NORMAL,
			NULL);

    if (hFile == INVALID_HANDLE_VALUE) {
	lasterror = GetLastError ();
	hFile = 0;
    }
    return (int) hFile;
}

int stdioemu_fclose (FILE * fd)
{
    if (fd)
	CloseHandle ((HANDLE) fd);
    return 0;
}

void *mallocemu_malloc (int size)
{
    return GlobalAlloc (GPTR, size);
}

void mallocemu_free (void *ptr)
{
    GlobalFree (ptr);
}

static int hextol (char a)
{
    if (a >= '0' && a <= '9')
	return a - '0';
    if (a >= 'a' && a <= 'f')
	return a - 'a' + 10;
    if (a >= 'A' && a <= 'F')
	return a - 'A' + 10;
    return 2;
}

/* Win32 file name restrictions suck... */
void fname_wtoa (unsigned char *ptr)
{
    unsigned char *lastslash = ptr;

    while (*ptr) {
	if (*ptr == '~') {
	    *ptr = hextol (ptr[1]) * 16 + hextol (ptr[2]);
	    strcpy (ptr + 1, ptr + 3);
	} else if (*ptr == '\\') {
	    if (checkspace (lastslash, ' ', 0xa0) && ptr - lastslash > 3 && lastslash[3] == 0xa0 && isillegal (lastslash)) {
		ptr--;
		strcpy (lastslash + 3, lastslash + 4);
	    }
	    *ptr = '/';
	    lastslash = ptr + 1;
	}
	ptr++;
    }

    if (checkspace (lastslash, ' ', 0xa0) && ptr - lastslash > 3 && lastslash[3] == 0xa0 && isillegal (lastslash))
	strcpy (lastslash + 3, lastslash + 4);
}

DIR {
    WIN32_FIND_DATA finddata;
    HANDLE hDir;
    int getnext;
};

DIR *opendir (char *path)
{
    char buf[1024];
    DIR *dir;

    if (!(dir = (DIR *) GlobalAlloc (GPTR, sizeof (DIR)))) {
	lasterror = GetLastError ();
	return 0;
    }
    fname_atow (path, buf, sizeof buf - 4);
    strcat (buf, "\\*");

    if ((dir->hDir = FindFirstFile (buf, &dir->finddata)) == INVALID_HANDLE_VALUE) {
	lasterror = GetLastError ();
	GlobalFree (dir);
	return 0;
    }
    return dir;
}

struct dirent *readdir (DIR * dir)
{
    if (dir->getnext) {
	if (!FindNextFile (dir->hDir, &dir->finddata)) {
	    lasterror = GetLastError ();
	    return 0;
	}
    }
    dir->getnext = TRUE;

    fname_wtoa (dir->finddata.cFileName);
    return (struct dirent *) dir->finddata.cFileName;
}

void closedir (DIR * dir)
{
    FindClose (dir->hDir);
    GlobalFree (dir);
}

int setfiletime (char *name, unsigned int days, int minute, int tick)
{
    FILETIME LocalFileTime, FileTime;
    HANDLE hFile;
    int success;
    char buf[1024];

    fname_atow (name, buf, sizeof buf);

    if ((hFile = CreateFile (buf, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE) {
	lasterror = GetLastError ();
	return 0;
    }
    *(__int64 *) & LocalFileTime = (((__int64) (377 * 365 + 91 + days) * (__int64) 1440 + (__int64) minute) * (__int64) (60 * 50) + (__int64) tick) * (__int64) 200000;

    if (!LocalFileTimeToFileTime (&LocalFileTime, &FileTime))
	FileTime = LocalFileTime;

    if (!(success = SetFileTime (hFile, &FileTime, &FileTime, &FileTime)))
	lasterror = GetLastError ();
    CloseHandle (hFile);

    return success;
}

int posixemu_read (int hFile, char *ptr, int size)
{
    DWORD actual;

    if (!ReadFile ((HANDLE) hFile, ptr, size, &actual, NULL))
	lasterror = GetLastError ();

    return actual;
}

int posixemu_write (int hFile, char *ptr, int size)
{
    DWORD actual;

    if (!WriteFile ((HANDLE) hFile, ptr, size, &actual, NULL))
	lasterror = GetLastError ();

    return actual;
}

int posixemu_seek (int hFile, int pos, int type)
{
    int result;

    switch (type) {
    case SEEK_SET:
	type = FILE_BEGIN;
	break;
    case SEEK_CUR:
	type = FILE_CURRENT;
	break;
    case SEEK_END:
	type = FILE_END;
    }

    if ((result = SetFilePointer ((HANDLE) hFile, pos, NULL, type)) == ~0)
	lasterror = GetLastError ();

    return result;
}

int stdioemu_fread (char *buf, int l1, int l2, FILE * hFile)
{
    return posixemu_read ((int) hFile, buf, l1 * l2);
}

int stdioemu_fwrite (char *buf, int l1, int l2, FILE * hFile)
{
    return posixemu_write ((int) hFile, buf, l1 * l2);
}

int stdioemu_fseek (FILE * hFile, int pos, int type)
{
    return posixemu_seek ((int) hFile, pos, type);
}

int stdioemu_ftell (FILE * hFile)
{
    return SetFilePointer ((HANDLE) hFile, 0, 0, FILE_CURRENT);
}

int posixemu_stat (char *name, struct stat *statbuf)
{
    char buf[1024];
    DWORD attr;
    HANDLE hFile;
    FILETIME ft, lft;

    fname_atow (name, buf, sizeof buf);

    if ((attr = getattr (buf, &ft, &statbuf->st_size)) == ~0) {
	lasterror = GetLastError ();
	return -1;
    } else {
	statbuf->st_mode = (attr & FILE_ATTRIBUTE_READONLY) ? 5 : 0;
	if (attr & FILE_ATTRIBUTE_ARCHIVE)
	    statbuf->st_mode |= 0x10;
	if (attr & FILE_ATTRIBUTE_DIRECTORY)
	    statbuf->st_mode |= 0x100;

	FileTimeToLocalFileTime (&ft, &lft);
	statbuf->st_mtime = (*(__int64 *) & lft - ((__int64) (369 * 365 + 89) * (__int64) (24 * 60 * 60) * (__int64) 10000000)) / (__int64) 10000000;
    }

    return 0;
}

int posixemu_mkdir (char *name, int mode)
{
    char buf[1024];

    fname_atow (name, buf, sizeof buf);

    if (CreateDirectory (buf, NULL))
	return 0;

    lasterror = GetLastError ();

    return -1;
}

int posixemu_unlink (char *name)
{
    char buf[1024];

    fname_atow (name, buf, sizeof buf);
    if (DeleteFile (buf))
	return 0;

    lasterror = GetLastError ();

    return -1;
}

int posixemu_rmdir (char *name)
{
    char buf[1024];

    fname_atow (name, buf, sizeof buf);
    if (RemoveDirectory (buf))
	return 0;

    lasterror = GetLastError ();

    return -1;
}

int posixemu_rename (char *name1, char *name2)
{
    char buf1[1024];
    char buf2[1024];

    fname_atow (name1, buf1, sizeof buf1);
    fname_atow (name2, buf2, sizeof buf2);
    if (MoveFile (name1, name2))
	return 0;

    lasterror = GetLastError ();

    return -1;
}

int posixemu_chmod (char *name, int mode)
{
    char buf[1024];
    DWORD attr = FILE_ATTRIBUTE_NORMAL;

    fname_atow (name, buf, sizeof buf);

    if (mode & 1)
	attr |= FILE_ATTRIBUTE_READONLY;
    if (mode & 0x10)
	attr |= FILE_ATTRIBUTE_ARCHIVE;

    if (SetFileAttributes (buf, attr))
	return 1;

    lasterror = GetLastError ();

    return 0;
}

void posixemu_tmpnam (char *name)
{
    char buf[MAX_PATH];

    GetTempPath (MAX_PATH, buf);
    GetTempFileName (buf, "uae", 0, name);
}

/* pthread Win32 emulation */
void sem_init (HANDLE * event, int manual_reset, int initial_state)
{
    *event = CreateEvent (NULL, manual_reset, initial_state, NULL);
}

void sem_wait (HANDLE * event)
{
    WaitForSingleObject (*event, INFINITE);
}

void sem_post (HANDLE * event)
{
    SetEvent (*event);
}

int sem_trywait (HANDLE * event)
{
    return WaitForSingleObject (*event, 0) == WAIT_OBJECT_0;
}

/* Mega-klduge to prevent problems with Watcom's habit of passing
 * arguments in registers... */
static HANDLE thread_sem;
static void *(*thread_startfunc) (void *);
#ifndef __GNUC__
static void * __stdcall thread_starter (void *arg)
#else
static void * thread_starter( void *arg )
#endif
{
    void *(*func) (void *) = thread_startfunc;
    SetEvent (thread_sem);
    return (*func) (arg);
}

/* this creates high priority threads by default to speed up the file system (kludge, will be
 * replaced by a set_penguin_priority() routine soon) */
int start_penguin (void *(*f) (void *), void *arg, DWORD * foo)
{
    static int have_event = 0;
    HANDLE hThread;

    if (! have_event) {
	thread_sem = CreateEvent (NULL, 0, 0, NULL);
	have_event = 1;
    }
    thread_startfunc = f;
    hThread = CreateThread (NULL, 0, (LPTHREAD_START_ROUTINE) thread_starter, arg, 0, foo);
    SetThreadPriority (hThread, THREAD_PRIORITY_HIGHEST);
    WaitForSingleObject (thread_sem, INFINITE);
}

