 /* 
  * UAE - The Un*x Amiga Emulator
  * 
  * Joystick emulation for Linux
  * 
  * Copyright 1997 Bernd Schmidt
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "options.h"
#include "memory.h"
#include "custom.h"
#include "joystick.h"

int nr_joysticks;

static int js0, js1;

/* There are too many different versions of <linux/joystick.h>.  Rather
 * than trying to work correctly with all of them, we duplicate the
 * necessary definitions here.  */
struct JS_DATA_TYPE
{
    int buttons;
    int x;
    int y;
};

static struct JS_DATA_TYPE jscal;

void read_joystick(int nr, unsigned int *dir, int *button)
{
    static int minx = MAXINT, maxx = MININT,
               miny = MAXINT, maxy = MININT;
    int left = 0, right = 0, top = 0, bot = 0;
    struct JS_DATA_TYPE buffer;
    int len;
    int fd = nr == 0 ? js0 : js1;

    *dir = 0;
    *button = 0;
    if (nr >= nr_joysticks)
    	return;
    
    len = read(fd, &buffer, sizeof(buffer));
    if (len != sizeof(buffer)) 
    	return;
    
    if (buffer.x < minx) minx = buffer.x;
    if (buffer.y < miny) miny = buffer.y;
    if (buffer.x > maxx) maxx = buffer.x;
    if (buffer.y > maxy) maxy = buffer.y;
    
    if (buffer.x < (minx + (maxx-minx)/3))
    	left = 1;
    else if (buffer.x > (minx + 2*(maxx-minx)/3))
    	right = 1;

    if (buffer.y < (miny + (maxy-miny)/3))
    	top = 1;
    else if (buffer.y > (miny + 2*(maxy-miny)/3))
    	bot = 1;
    	
    if (left) top = !top;
    if (right) bot = !bot;
    *dir = bot | (right << 1) | (top << 8) | (left << 9);
    *button = buffer.buttons & 3;
}

void init_joystick(void)
{
    nr_joysticks = 0;
    js0 = -1; js1 = -1;
    js0 = open("/dev/js0", O_RDONLY);
    if (js0 < 0) {
	write_log ("No joysticks found\n");
    	return;
    }
    nr_joysticks = 1;
    js1 = open("/dev/js1", O_RDONLY);
    if (js1 < 0) {
	write_log ("Found one joystick\n");
    	return;
    }
    write_log ("Found two joysticks\n");
    nr_joysticks = 2;
}

void close_joystick(void)
{
    if (js0 >= 0)
	close(js0);
    if (js1 >= 0)
	close(js1);
}
