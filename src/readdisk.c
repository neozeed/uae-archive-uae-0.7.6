/*
 * readdisk
 *
 * Read files from Amiga disk files
 *
 * Copyright 1996 Bernd Schmidt
 */

#include "sysconfig.h"
#include "sysdeps.h"

#include "uae.h"

char warning_buffer[256];

void write_log (const char *s)
{
    fprintf(stderr, "%s", s);
}

unsigned char filemem[901120];

typedef struct afile
{
    struct afile *sibling;
    unsigned char *data;
    uae_u32 size;
    char name[32];
} afile;

typedef struct directory
{
    struct directory *sibling;
    struct directory *subdirs;
    struct afile *files;
    char name[32];
} directory;

static uae_u32 readlong (unsigned char *buffer, int pos)
{
    return ((*(buffer + pos) << 24) + (*(buffer + pos + 1) << 16)
	     + (*(buffer + pos + 2) << 8) + *(buffer + pos + 3));
}

static afile *read_file (unsigned char *filebuf)
{
    afile *a = (afile *)xmalloc(sizeof(afile));
    int sizeleft;
    unsigned char *datapos;
    uae_u32 numblocks, blockpos;

    /* BCPL strings... Yuk. */
    memset (a->name, 0, 32);
    strncpy (a->name, (const char *)filebuf + 0x1B1, *(filebuf + 0x1B0));
    sizeleft = a->size = readlong (filebuf, 0x144);
    a->data = (unsigned char *)xmalloc(a->size);

    numblocks = readlong (filebuf, 0x8);
    blockpos = 0x134;
    datapos = a->data;
    while (numblocks)
    {
	unsigned char *databuf = filemem + 512*readlong (filebuf, blockpos);
	int readsize = sizeleft > 488 ? 488 : sizeleft;
	memcpy (datapos, databuf + 0x18, readsize);
	datapos += readsize;
	sizeleft -= readsize;

	blockpos -= 4;
	numblocks--;
	if (!numblocks) {
	    uae_u32 nextflb = readlong (filebuf, 0x1F8);
	    if (nextflb) {
		filebuf = filemem + 512*nextflb;
		blockpos = 0x134;
		numblocks = readlong (filebuf, 0x8);
		if (!filebuf) {
		    fprintf(stderr, "Disk structure corrupted. Use DISKDOCTOR to correct it.\n");
		    abort ();
		}
	    }
	}
    }
    return a;
}

static directory *read_dir (unsigned char *dirbuf)
{
    directory *d = (directory *)xmalloc(sizeof(directory));
    uae_u32 hashsize;
    uae_u32 i;

    memset (d->name, 0, 32);
    strncpy (d->name, (const char *)dirbuf + 0x1B1, *(dirbuf + 0x1B0));
    d->sibling = 0;
    d->subdirs = 0;
    d->files = 0;
    hashsize = readlong (dirbuf, 0xc);
    if (!hashsize)
	hashsize = 72;
    if (hashsize != 72)
	fprintf(stderr, "Warning: Hash table with != 72 entries.\n");
    for (i = 0; i < hashsize; i++) {
	uae_u32 subblock = readlong (dirbuf, 0x18 + 4*i);
	while (subblock) {
	    directory *subdir;
	    afile *subfile;
	    unsigned char *subbuf = filemem + 512*subblock;

	    switch (readlong (subbuf, 0x1FC)) {
	     case 0x00000002:
		subdir = read_dir (subbuf);
		subdir->sibling = d->subdirs;
		d->subdirs = subdir;
		break;

	     case 0xFFFFFFFD:
		subfile = read_file (subbuf);
		subfile->sibling = d->files;
		d->files = subfile;
		break;

	     default:
		fprintf(stderr, "Disk structure corrupted. Use DISKDOCTOR to correct it.\n");
		abort ();
	    }
	    subblock = readlong (subbuf, 0x1F0);
	}
    }
    return d;
}

static void writedir(directory *dir)
{
    directory *subdir;
    afile *f;

    if (mkdir (dir->name, 0777) < 0 && errno != EEXIST) {
	fprintf(stderr, "Could not create directory \"%s\". Giving up.\n", dir->name);
	exit (20);
    }
    if (chdir (dir->name) < 0) {
	fprintf(stderr, "Could not enter directory \"%s\". Giving up.\n", dir->name);
	exit (20);
    }
    for (subdir = dir->subdirs; subdir; subdir = subdir->sibling)
	writedir (subdir);
    for (f = dir->files; f; f = f->sibling) {
	int fd = creat (f->name, 0666);
	if (fd < 0) {
	    fprintf(stderr, "Could not create file. Giving up.\n");
	    exit (20);
	}
	write (fd, f->data, f->size);
	close (fd);
    }
    chdir ("..");
}

int main(int argc, char **argv)
{
    directory *root;
    FILE *inf;
    if (argc < 2 || argc > 3) {
	fprintf(stderr, "Usage: readdisk <file> [<destdir>]\n");
	exit (20);
    }
    inf = fopen(argv[1], "rb");
    if (inf == NULL) {
	fprintf(stderr, "can't open file\n");
	exit (20);
    }
    fread(filemem, 1, 901120, inf);

    if (strncmp((const char *)filemem, "DOS\0", 4) != 0) {
	fprintf(stderr, "Not a DOS disk.\n");
	exit (20);
    }
    root = read_dir (filemem + 880*512);

    if (argc == 3)
	if (chdir (argv[2]) < 0) {
	    fprintf(stderr, "Couldn't change to %s. Giving up.\n", argv[2]);
	    exit (20);
	}
    writedir (root);
    return 0;
}
