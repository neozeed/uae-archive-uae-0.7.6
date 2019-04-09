 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Target specific stuff, X11 version
  *
  * Copyright 1997 Bernd Schmidt
  */

#ifndef USE_DGA_EXTENSION
#if SHM_SUPPORT_LINKS == 1
#define TARGET_SPECIAL_OPTIONS \
    { "x",	  "  -x           : Hide X11 mouse cursor\n" }, \
    { "T",	  "  -T           : Use MIT-SHM extension\n" }, \
    { "L",	  "  -L           : Use low bandwidth mode\n" },
#else
#define TARGET_SPECIAL_OPTIONS \
    { "x",	  "  -x           : Hide X11 mouse cursor\n" }, \
    { "L",	  "  -L           : Use low bandwidth mode\n" },
#endif

#endif /* not DGA */

#if !defined USING_TCL_GUI && !defined USING_FORMS_GUI && !defined USING_GTK_GUI
#define UNSUPPORTED_OPTION_G
#endif

#define OPTIONSFILENAME ".uaerc"
#define OPTIONS_IN_HOME

#define DEFPRTNAME "lpr"
#define DEFSERNAME "/dev/ttyS1"

#define PICASSO96_SUPPORTED
