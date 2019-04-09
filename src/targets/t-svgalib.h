 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Target specific stuff, SVGAlib version
  *
  * Copyright 1997 Bernd Schmidt
  */

#define TARGET_SPECIAL_OPTIONS \
    { "x",	  "  -x           : Don't use linear frame buffer even if it is available\n" },

#define UNSUPPORTED_OPTION_l

#ifndef USING_CURSES_UI
#define UNSUPPORTED_OPTION_G
#endif

#define OPTIONSFILENAME ".uaerc"
#define OPTIONS_IN_HOME

#define DEFPRTNAME "lpr"
#define DEFSERNAME "/dev/ttyS1"

#define PICASSO96_SUPPORTED
