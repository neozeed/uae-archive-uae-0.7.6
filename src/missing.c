 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Various stuff missing in some OSes.
  *
  * Copyright 1997 Bernd Schmidt
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "options.h"
#include "uae.h"

#ifndef HAVE_STRDUP

char *my_strdup (const char *s)
{
    /* The casts to char * are there to shut up the compiler on HPUX */
    char *x = (char*)xmalloc(strlen((char *)s) + 1);
    strcpy(x, (char *)s);
    return x;
}

#endif

#ifndef HAVE_GETOPT

/* This isn't a complete getopt, but it's good enough for UAE. */
char *optarg;
int optind = 0;

int getopt (int argc, char *const *argv, const char *str)
{
    char *p, c;

    if (optind == 0)
	optind = 1;
    for (;;) {
	while (optind < argc && (argv[optind][0] != '-' || argv[optind][1] == '\0'))
	    optind++;
	if (optind >= argc)
	    return EOF;
	p = strchr (str, c = argv[optind][1]);
	if (p == 0) {
	    sprintf (warning_buffer, "Commandline parsing error - invalid option \"-%c\".\n", argv[optind][1]);
	    write_log (warning_buffer);
	    optind++;
	    continue;
	}
	break;
    }
    if (*(p+1) == ':') {
	if (argv[optind][2] == '\0') {
	    optarg = argv[optind + 1];
	    optind++;
	} else {
	    optarg = argv[optind] + 2;
	}
    }
    optind++;
    return c;
}

#endif

void *xmalloc(size_t n)
{
    void *a = malloc (n);
    if (a == NULL) {
	write_log ("virtual memory exhausted\n");
	abort();
    }
    return a;
}
