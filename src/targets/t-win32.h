 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Target specific stuff, Win32 version
  *
  * Copyright 1997 Mathias Ortmann
  */

#define NO_MAIN_IN_MAIN_C
#define UNSUPPORTED_OPTION_D
#define UNSUPPORTED_OPTION_l
#define UNSUPPORTED_OPTION_o

#define TARGET_SPECIAL_OPTIONS \
	{ "W", "  -W           : Open UAE display as a desktop window\n" }, \
	{ "P:","  -P n         : Set UAE base priority\n" }, \
	{ "Q:","  -Q n         : Set system clock frequency in kHz\n" \
				  "                 (use -Q -1 on pre-Pentium class CPUs)\n" }, \

#define OPTIONSFILENAME "uae.rc"
#define OPTIONS_IN_HOME

#define DEFPRTNAME "LPT1"
#define DEFSERNAME "COM1"
