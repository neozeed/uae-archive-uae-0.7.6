 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Interface to the Tcl/Tk GUI
  *
  * Copyright 1996 Bernd Schmidt
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "options.h"
#include "gui.h"

static void sigchldhandler(int foo)
{
}

int gui_init(void)
{
    return 0;
}

int gui_update(void)
{
    return 0;
}

void gui_exit(void)
{
}

void gui_led(int led, int on)
{
}

void gui_filename(int num, const char *name)
{
}

static void getline(char *p)
{
}

void gui_handle_events(void)
{
}

void gui_changesettings(void)
{
}

