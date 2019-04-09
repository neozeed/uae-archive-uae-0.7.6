/*
 * UAE - the Un*x Amiga Emulator
 *
 * Yet Another User Interface for the X11 version
 *
 * Copyright 1997, 1998 Bernd Schmidt
 * 
 * The Tk GUI doesn't work.
 * The X Forms Library isn't available as source, and there aren't any
 * binaries compiled against glibc
 *
 * So let's try this...
 */

#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "options.h"
#include "uae.h"
#include "memory.h"
#include "custom.h"
#include "readcpu.h"
#include "gui.h"
#include "newcpu.h"
#include "threaddep/penguin.h"

#include <gtk/gtk.h>

static int gui_active;

static GtkWidget *disk_insert_widget[4], *disk_eject_widget[4], *disk_text_widget[4];
static char *disk_string[4];
static char *new_disk_string[4];

static GtkAdjustment *cpuspeed_adj;
static GtkWidget *cpu_widget[4], *a24m_widget, *ccpu_widget;

static GtkAdjustment *framerate_adj;
static GtkWidget *bimm_widget, *b32_widget;

static GtkWidget *joy_widget[2][6];

static GtkWidget *led_widgets[5];
static GdkColor led_on[5], led_off[5];
static unsigned int prevledstate;

static void save_config (void)
{
    FILE *f;
    char tmp[257];

    /* Backup the options file.  */
    strcpy (tmp, optionsfile);
    strcat (tmp, "~");
    rename (optionsfile, tmp);

    f = fopen (optionsfile, "w");
    if (f == NULL) {
	fprintf(stderr, "Error saving options file!\n");
	return;
    }
    save_options (f);
    fclose (f);
}

static int nr_for_led (GtkWidget *led)
{
    int i;
    i = 0;
    while (led_widgets[i] != led)
	i++;
    return i;
}

static smp_comm_pipe to_gui_pipe, from_gui_pipe;
static uae_sem_t gui_sem, gui_quit_sem;

static volatile int quit_gui = 0, quitted_gui = 0;

static void enable_disk_buttons (int enable)
{
    int i;
    for (i = 0; i < 4; i++) {
	gtk_widget_set_sensitive (disk_insert_widget[i], enable);
	gtk_widget_set_sensitive (disk_eject_widget[i], enable);
    }
}

static void set_cpu_state (void)
{
    gtk_toggle_button_set_state (GTK_TOGGLE_BUTTON (a24m_widget), changed_prefs.address_space_24 != 0);
    gtk_widget_set_sensitive (a24m_widget, changed_prefs.cpu_level > 1);
    gtk_toggle_button_set_state (GTK_TOGGLE_BUTTON (ccpu_widget), changed_prefs.cpu_compatible != 0);
    gtk_widget_set_sensitive (ccpu_widget, changed_prefs.cpu_level == 0);
}

static void set_cpu_widget (void)
{
    int nr = changed_prefs.cpu_level;
    gtk_toggle_button_set_state (GTK_TOGGLE_BUTTON (cpu_widget[nr]), TRUE);
}

static void set_gfx_state (void)
{
    gtk_toggle_button_set_state (GTK_TOGGLE_BUTTON (bimm_widget), currprefs.immediate_blits != 0);
    gtk_toggle_button_set_state (GTK_TOGGLE_BUTTON (b32_widget), currprefs.blits_32bit_enabled != 0);
}

static void set_joy_state (void)
{
    int j0t = changed_prefs.fake_joystick & 255;
    int j1t = (changed_prefs.fake_joystick >> 8) & 255;
    int i;

    if (j0t == j1t) {
	/* Can't happen */
	j0t++;
	j0t %= 6;
    }
    for (i = 0; i < 6; i++) {
	gtk_toggle_button_set_state (GTK_TOGGLE_BUTTON (joy_widget[0][i]), j0t == i);
	gtk_toggle_button_set_state (GTK_TOGGLE_BUTTON (joy_widget[1][i]), j1t == i);
	gtk_widget_set_sensitive (joy_widget[0][i], j1t != i);
	gtk_widget_set_sensitive (joy_widget[1][i], j0t != i);
    }
}

static void draw_led (int nr)
{
    GtkWidget *thing = led_widgets[nr];
    GdkWindow *window = thing->window;
    GdkGC *gc = gdk_gc_new (window);
    GdkColor *col;

    if (gui_ledstate & (1 << nr))
	col = led_on + nr;
    else
	col = led_off + nr;
    gdk_gc_set_foreground (gc, col);
    gdk_draw_rectangle (window, gc, 1, 0, 0, -1, -1);
    gdk_gc_destroy (gc);
}

static int my_idle (void)
{
    unsigned int leds = gui_ledstate;
    int i;

    if (quit_gui) {
	/*printf("Foo...\n");*/
	gtk_main_quit ();
	goto out;
    }
    while (comm_pipe_has_data (&to_gui_pipe)) {
	int cmd = read_comm_pipe_int_blocking (&to_gui_pipe);
	int n;
	/*printf ("cmd %d\n", cmd);*/
	switch (cmd) {
	 case 0:
	    n = read_comm_pipe_int_blocking (&to_gui_pipe);
	    gtk_label_set (GTK_LABEL (disk_text_widget[n]), currprefs.df[n]);
	    break;
	 case 1:
	    /* Initialization.  */
	    set_cpu_widget ();
	    set_cpu_state ();
	    set_gfx_state ();
	    set_joy_state ();
	    gui_active = 1;
	    break;
	}
    }
    
    for (i = 0; i < 5; i++) {
	unsigned int mask = 1 << i;
	int on = leds & mask;

	if (on == (prevledstate & mask))
	    continue;

/*	printf(": %d %d\n", i, on);*/
	draw_led (i);
    }
    prevledstate = leds;
out:
    return 1;
}

static void joy_changed (void)
{
    int j0t = 0, j1t = 0;
    int i;
    if (! gui_active)
	return;

    for (i = 0; i < 6; i++)
	if (GTK_TOGGLE_BUTTON (joy_widget[0][i])->active) {
	    j0t = i;
	    break;
	}
    for (i = 0; i < 6; i++)
	if (GTK_TOGGLE_BUTTON (joy_widget[1][i])->active) {
	    j1t = i;
	    break;
	}
    changed_prefs.fake_joystick = j0t | (j1t << 8);
    set_joy_state ();
}

static void gfx_changed (void)
{
    changed_prefs.framerate = framerate_adj->value;
    changed_prefs.blits_32bit_enabled = GTK_TOGGLE_BUTTON (b32_widget)->active;
    changed_prefs.immediate_blits = GTK_TOGGLE_BUTTON (bimm_widget)->active;
}

static void cpuspeed_changed (void)
{
    changed_prefs.m68k_speed = cpuspeed_adj->value;
}

static void cputype_changed (void)
{
    int i, oldcl;
    if (! gui_active)
	return;

    oldcl = changed_prefs.cpu_level;

    changed_prefs.cpu_level = 0;
    for (i = 0; i < 4; i++)
	if (GTK_TOGGLE_BUTTON (cpu_widget[i])->active) {
	    changed_prefs.cpu_level = i;
	    break;
	}

    changed_prefs.cpu_compatible = GTK_TOGGLE_BUTTON (ccpu_widget)->active;
    changed_prefs.address_space_24 = GTK_TOGGLE_BUTTON (a24m_widget)->active;

    if (changed_prefs.cpu_level != 0)
	changed_prefs.cpu_compatible = 0;
    /* 68000/68010 always have a 24 bit address space.  */
    if (changed_prefs.cpu_level < 2)
	changed_prefs.address_space_24 = 1;
    /* Changing from 68000/68010 to 68020 should set a sane default.  */
    else if (oldcl < 2)
	changed_prefs.address_space_24 = 0;
    
    set_cpu_state ();
}

static void did_reset (void)
{
    if (quit_gui)
	return;
    
    write_comm_pipe_int (&from_gui_pipe, 2, 1);
}

static void did_debug (void)
{
    if (quit_gui)
	return;
    
    write_comm_pipe_int (&from_gui_pipe, 3, 1);
}

static void did_quit (void)
{
    if (quit_gui)
	return;
    
    write_comm_pipe_int (&from_gui_pipe, 4, 1);
}

static void did_eject (int n)
{ 
    if (quit_gui)
	return;
    
    write_comm_pipe_int (&from_gui_pipe, 0, 0);
    write_comm_pipe_int (&from_gui_pipe, n, 1);
}

static int filesel_active = -1;
static GtkWidget *selector;

static void did_close_insert (GtkObject *o, GtkWidget *w)
{
    filesel_active = -1;
    enable_disk_buttons (1);
    gtk_widget_hide (selector);
}

static void did_cancel_insert (GtkObject *o)
{
    filesel_active = -1;
    enable_disk_buttons (1);
    gtk_widget_hide (selector);
}

static void did_insert_select (GtkObject *o)
{
    char *s = gtk_file_selection_get_filename (GTK_FILE_SELECTION (selector));
    printf ("%s\n", s);
    if (quit_gui)
	return;

    uae_sem_wait (&gui_sem);
    if (new_disk_string[filesel_active] != 0)
	free (new_disk_string[filesel_active]);
    new_disk_string[filesel_active] = strdup (s);
    uae_sem_post (&gui_sem);
    write_comm_pipe_int (&from_gui_pipe, 1, 0);
    write_comm_pipe_int (&from_gui_pipe, filesel_active, 1);
    filesel_active = -1;
    enable_disk_buttons (1);
    gtk_widget_hide (selector);
}

static char fsbuffer[100];

static void did_insert (int n)
{
    if (filesel_active != -1)
	return;
    filesel_active = n;
    enable_disk_buttons (0);

    sprintf (fsbuffer, "Select a disk image file for DF%d", n);
    gtk_window_set_title (GTK_WINDOW (selector), fsbuffer);

    /*printf("%p\n", selector);*/
    gtk_widget_show (selector);
}

static void did_eject_0 (void) { did_eject (0); }
static void did_eject_1 (void) { did_eject (1); }
static void did_eject_2 (void) { did_eject (2); }
static void did_eject_3 (void) { did_eject (3); }

static void did_insert_0 (void) { did_insert (0); }
static void did_insert_1 (void) { did_insert (1); }
static void did_insert_2 (void) { did_insert (2); }
static void did_insert_3 (void) { did_insert (3); }

static gint driveled_event (GtkWidget *thing, GdkEvent *event)
{
    gint x, y;
    GtkStyle *style;
    int lednr = nr_for_led (thing);

    switch (event->type) {
     case GDK_MAP:
	draw_led (lednr);
	break;
     case GDK_EXPOSE:
	draw_led (lednr);
	break;
     default:
	break;
    }

  return 0;
}

static GtkWidget *make_led (int nr)
{
    GtkWidget *subframe, *the_led, *thing;
    GdkColormap *colormap;

    the_led = gtk_vbox_new (FALSE, 0);
    gtk_widget_show (the_led);

    thing = gtk_preview_new (GTK_PREVIEW_COLOR);
    gtk_box_pack_start (GTK_BOX (the_led), thing, TRUE, TRUE, 0);
    gtk_widget_show (thing);

    subframe = gtk_frame_new (NULL);
    gtk_box_pack_start (GTK_BOX (the_led), subframe, TRUE, TRUE, 0);
    gtk_widget_show (subframe);

    thing = gtk_drawing_area_new ();
    gtk_drawing_area_size (GTK_DRAWING_AREA (thing), 20, 5);
    gtk_widget_set_events (thing, GDK_EXPOSURE_MASK);
    gtk_container_add (GTK_CONTAINER (subframe), thing);
    colormap = gtk_widget_get_colormap (thing);
    led_on[nr].red = nr == 0 ? 0xEEEE : 0xCCCC;
    led_on[nr].green = nr == 0 ? 0: 0xFFFF;
    led_on[nr].blue = 0;
    led_on[nr].pixel = 0;
    led_off[nr].red = 0;
    led_off[nr].green = 0;
    led_off[nr].blue = 0;
    led_off[nr].pixel = 0;
    gdk_color_alloc (colormap, led_on + nr);
    gdk_color_alloc (colormap, led_off + nr);
    led_widgets[nr] = thing;
    gtk_signal_connect (GTK_OBJECT (thing), "event",
			(GtkSignalFunc) driveled_event, (gpointer) thing);
    gtk_widget_show (thing);

    thing = gtk_preview_new (GTK_PREVIEW_COLOR);
    gtk_box_pack_start (GTK_BOX (the_led), thing, TRUE, TRUE, 0);
    gtk_widget_show (thing);
    
    return the_led;
}

static void make_floppy_disks (GtkWidget *vbox)
{
    int i;
    /* Now, the floppy disk control.  */
    for (i = 0; i < 4; i++) {
	GtkWidget *thing, *subthing, *subframe, *buttonbox;
	char buf[5];
	sprintf (buf, "DF%d:", i);

	/* Each line: a frame with a hbox in it. The hbox contains
	 * the LED, the name of the drive and the diskfile in it, an
	 * Insert button, and an Eject button.  */
	buttonbox = gtk_frame_new (buf);
	thing = gtk_hbox_new (FALSE, 4);
	gtk_container_add (GTK_CONTAINER (buttonbox), thing);
        gtk_box_pack_start (GTK_BOX (vbox), buttonbox, FALSE, TRUE, 0);
	gtk_widget_show (buttonbox);
	gtk_widget_show (thing);
	buttonbox = thing;

	subthing = make_led (i + 1);
	gtk_box_pack_start (GTK_BOX (buttonbox), subthing, FALSE, TRUE, 0);

	/* Make the "DF0:xxx" label.  */
	subframe = gtk_frame_new (NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (subframe), GTK_SHADOW_ETCHED_OUT);
	gtk_box_pack_start (GTK_BOX (buttonbox), subframe, FALSE, TRUE, 0);
	gtk_widget_show (subframe);

	subthing = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (subthing);
	gtk_container_add (GTK_CONTAINER (subframe), subthing);

	thing = gtk_label_new ("");
	disk_text_widget[i] = thing;
	gtk_widget_show (thing);
	gtk_box_pack_start (GTK_BOX (subthing), thing, FALSE, TRUE, 0);

	gtk_widget_set_usize (subthing,
			      gdk_string_measure (subframe->style->font,
						  "This is a very long name for a disk file, should be enough") + 7, 0);
	gtk_container_disable_resize (GTK_CONTAINER (subthing));

	/* Now, the buttons.  */
	thing = gtk_button_new_with_label ("Eject");
	gtk_box_pack_start (GTK_BOX (buttonbox), thing, FALSE, TRUE, 0);
	gtk_widget_show (thing);
	disk_eject_widget[i] = thing;
	gtk_signal_connect (GTK_OBJECT (thing), "clicked",
			    (GtkSignalFunc) (i == 0 ? did_eject_0 
					     : i == 1 ? did_eject_1 
					     : i == 2 ? did_eject_2 : did_eject_3),
			    NULL);

	thing = gtk_button_new_with_label ("Insert");
	gtk_box_pack_start (GTK_BOX (buttonbox), thing, FALSE, TRUE, 0);
	gtk_widget_show (thing);
	disk_insert_widget[i] = thing;
	gtk_signal_connect (GTK_OBJECT (thing), "clicked",
			    (GtkSignalFunc) (i == 0 ? did_insert_0 
					     : i == 1 ? did_insert_1 
					     : i == 2 ? did_insert_2 : did_insert_3),
			    NULL);
    }
}

static void make_cpu_widgets (GtkWidget *hbox)
{
    int i;
    GtkWidget *vbox, *hbox2, *frame;
    GtkWidget *thing;

    frame = gtk_frame_new ("CPU type");
    gtk_box_pack_start (GTK_BOX (hbox), frame, FALSE, TRUE, 0);
    gtk_widget_show (frame);
    vbox = gtk_vbox_new (FALSE, 4);
    gtk_widget_show (vbox);
    gtk_container_add (GTK_CONTAINER (frame), vbox);
    
    thing = gtk_radio_button_new_with_label (0, "68000");
    gtk_box_pack_start (GTK_BOX (vbox), thing, FALSE, TRUE, 0);
    gtk_widget_show (thing);
    cpu_widget[0] = thing;
    thing = gtk_radio_button_new_with_label (gtk_radio_button_group (GTK_RADIO_BUTTON (thing)), "68010");
    gtk_box_pack_start (GTK_BOX (vbox), thing, FALSE, TRUE, 0);
    gtk_widget_show (thing);
    cpu_widget[1] = thing;
    thing = gtk_radio_button_new_with_label (gtk_radio_button_group (GTK_RADIO_BUTTON (thing)), "68020");
    gtk_box_pack_start (GTK_BOX (vbox), thing, FALSE, TRUE, 0);
    gtk_widget_show (thing);
    cpu_widget[2] = thing;
    thing = gtk_radio_button_new_with_label (gtk_radio_button_group (GTK_RADIO_BUTTON (thing)), "68020/68881");
    gtk_box_pack_start (GTK_BOX (vbox), thing, FALSE, TRUE, 0);
    gtk_widget_show (thing);
    cpu_widget[3] = thing;

    for (i = 0; i < 4; i++)
	gtk_signal_connect (GTK_OBJECT (cpu_widget[i]), "clicked",
			    (GtkSignalFunc) cputype_changed, NULL);
    
    frame = gtk_frame_new ("CPU flags");
    gtk_box_pack_start (GTK_BOX (hbox), frame, FALSE, TRUE, 0);
    gtk_widget_show (frame);
    vbox = gtk_vbox_new (FALSE, 4);
    gtk_widget_show (vbox);
    gtk_container_add (GTK_CONTAINER (frame), vbox);

    hbox2 = gtk_hbox_new (FALSE, 4);
    gtk_widget_show (hbox2);
    gtk_box_pack_start (GTK_BOX (vbox), hbox2, FALSE, TRUE, 0);

    thing = gtk_label_new ("Cycles per instruction:");
    gtk_box_pack_start (GTK_BOX (hbox2), thing, FALSE, TRUE, 0);
    gtk_widget_show (thing);

    cpuspeed_adj = GTK_ADJUSTMENT (gtk_adjustment_new (currprefs.m68k_speed, 1.0, 10.0, 1.0, 1.0, 1.0));
    gtk_signal_connect (GTK_OBJECT (cpuspeed_adj), "value_changed",
			GTK_SIGNAL_FUNC (cpuspeed_changed), NULL);

    thing = gtk_hscale_new (cpuspeed_adj);
    gtk_range_set_update_policy (GTK_RANGE (thing), GTK_UPDATE_DELAYED);
    gtk_scale_set_digits (GTK_SCALE (thing), 0);
    gtk_scale_set_value_pos (GTK_SCALE (thing), GTK_POS_RIGHT);
    gtk_box_pack_start (GTK_BOX (hbox2), thing, FALSE, TRUE, 0);
    gtk_widget_show (thing);

    a24m_widget = gtk_check_button_new_with_label ("24 bit address space");
    gtk_box_pack_start (GTK_BOX (vbox), a24m_widget, FALSE, TRUE, 0);
    gtk_widget_show (a24m_widget);
    ccpu_widget = gtk_check_button_new_with_label ("Slow but compatible");
    gtk_box_pack_start (GTK_BOX (vbox), ccpu_widget, FALSE, TRUE, 0);
    gtk_widget_show (ccpu_widget);

    gtk_signal_connect (GTK_OBJECT (ccpu_widget), "clicked",
			(GtkSignalFunc) cputype_changed, NULL);
    gtk_signal_connect (GTK_OBJECT (a24m_widget), "clicked",
			(GtkSignalFunc) cputype_changed, NULL);    
}

static void make_gfx_widgets (GtkWidget *hbox)
{
    int i;
    GtkWidget *vbox, *hbox2, *frame;
    GtkWidget *thing;

#if 0
    frame = gtk_frame_new ("CPU type");
    gtk_box_pack_start (GTK_BOX (hbox), frame, FALSE, TRUE, 0);
    gtk_widget_show (frame);
#endif
    vbox = gtk_vbox_new (FALSE, 4);
    gtk_widget_show (vbox);
    gtk_box_pack_start (GTK_BOX (hbox), vbox, FALSE, TRUE, 0);
#if 0
    gtk_container_add (GTK_CONTAINER (frame), vbox);
#endif
    
    hbox2 = gtk_hbox_new (FALSE, 4);
    gtk_widget_show (hbox2);
    gtk_box_pack_start (GTK_BOX (vbox), hbox2, FALSE, TRUE, 0);

    thing = gtk_label_new ("Framerate:");
    gtk_box_pack_start (GTK_BOX (hbox2), thing, FALSE, TRUE, 0);
    gtk_widget_show (thing);

    framerate_adj = GTK_ADJUSTMENT (gtk_adjustment_new (currprefs.framerate, 1.0, 21.0, 1.0, 1.0, 1.0));
    gtk_signal_connect (GTK_OBJECT (framerate_adj), "value_changed",
			GTK_SIGNAL_FUNC (gfx_changed), NULL);

    thing = gtk_hscale_new (framerate_adj);
    gtk_range_set_update_policy (GTK_RANGE (thing), GTK_UPDATE_DELAYED);
    gtk_scale_set_digits (GTK_SCALE (thing), 0);
    gtk_scale_set_value_pos (GTK_SCALE (thing), GTK_POS_RIGHT);
    gtk_box_pack_start (GTK_BOX (hbox2), thing, FALSE, TRUE, 0);
    gtk_widget_show (thing);

    b32_widget = gtk_check_button_new_with_label ("32 bit blitter");
    gtk_box_pack_start (GTK_BOX (vbox), b32_widget, FALSE, TRUE, 0);
    gtk_widget_show (b32_widget);
    bimm_widget = gtk_check_button_new_with_label ("Immediate blits");
    gtk_box_pack_start (GTK_BOX (vbox), bimm_widget, FALSE, TRUE, 0);
    gtk_widget_show (bimm_widget);

    gtk_signal_connect (GTK_OBJECT (bimm_widget), "clicked",
			(GtkSignalFunc) gfx_changed, NULL);
    gtk_signal_connect (GTK_OBJECT (b32_widget), "clicked",
			(GtkSignalFunc) gfx_changed, NULL);    
}

static void make_joy_widgets (GtkWidget *hbox)
{
    int i;

    for (i = 0; i < 2; i++) {
	GtkWidget *vbox, *frame;
	GtkWidget *thing;
	char buffer[20];
	int j;

	sprintf (buffer, "Port %d", i);
	frame = gtk_frame_new (buffer);
	gtk_box_pack_start (GTK_BOX (hbox), frame, FALSE, TRUE, 0);
	gtk_widget_show (frame);
	vbox = gtk_vbox_new (FALSE, 4);
	gtk_widget_show (vbox);
	gtk_container_add (GTK_CONTAINER (frame), vbox);
    
	thing = gtk_radio_button_new_with_label (0, "Joystick 0");
	gtk_box_pack_start (GTK_BOX (vbox), thing, FALSE, TRUE, 0);
	gtk_widget_show (thing);
	joy_widget[i][0] = thing;
	thing = gtk_radio_button_new_with_label (gtk_radio_button_group (GTK_RADIO_BUTTON (thing)), "Joystick 1");
	gtk_box_pack_start (GTK_BOX (vbox), thing, FALSE, TRUE, 0);
	gtk_widget_show (thing);
	joy_widget[i][1] = thing;
	thing = gtk_radio_button_new_with_label (gtk_radio_button_group (GTK_RADIO_BUTTON (thing)), "Mouse");
	gtk_box_pack_start (GTK_BOX (vbox), thing, FALSE, TRUE, 0);
	gtk_widget_show (thing);
	joy_widget[i][2] = thing;
	thing = gtk_radio_button_new_with_label (gtk_radio_button_group (GTK_RADIO_BUTTON (thing)), "Numeric pad");
	gtk_box_pack_start (GTK_BOX (vbox), thing, FALSE, TRUE, 0);
	gtk_widget_show (thing);
	joy_widget[i][3] = thing;
	thing = gtk_radio_button_new_with_label (gtk_radio_button_group (GTK_RADIO_BUTTON (thing)), "Cursor keys/Right Ctrl");
	gtk_box_pack_start (GTK_BOX (vbox), thing, FALSE, TRUE, 0);
	gtk_widget_show (thing);
	joy_widget[i][4] = thing;
	thing = gtk_radio_button_new_with_label (gtk_radio_button_group (GTK_RADIO_BUTTON (thing)), "T/F/H/B/Left Alt");
	gtk_box_pack_start (GTK_BOX (vbox), thing, FALSE, TRUE, 0);
	gtk_widget_show (thing);
	joy_widget[i][5] = thing;

	for (j = 0; j < 6; j++)
	    gtk_signal_connect (GTK_OBJECT (joy_widget[i][j]), "clicked",
				(GtkSignalFunc) joy_changed, NULL);
    }
}

static void *gtk_penguin (void *dummy)
{
    GtkWidget *window, *notebook;
    GtkWidget *buttonbox, *vbox, *vbox2, *hbox;
    GtkWidget *thing;

    int i;

    int argc = 1;
    char *a[] = {"UAE"};
    char **argv = a;

    gtk_init (&argc, &argv);
    gtk_rc_parse ("uaegtkrc");

    window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name (window, "UAE control");

    vbox = gtk_vbox_new (FALSE, 4);
    gtk_container_add (GTK_CONTAINER (window), vbox);

    /* Make a box with the Reset/Debug/Quit buttons and place it at the top
     * of our vbox.  */
    buttonbox = gtk_hbox_new (FALSE, 4);
    gtk_box_pack_start (GTK_BOX (vbox), buttonbox, FALSE, TRUE, 0);
    gtk_widget_show (buttonbox);
    thing = gtk_button_new_with_label ("Reset");
    gtk_signal_connect (GTK_OBJECT (thing), "clicked", (GtkSignalFunc) did_reset, NULL);
    gtk_box_pack_start (GTK_BOX (buttonbox), thing, FALSE, TRUE, 0);
    gtk_widget_show (thing);
    thing = gtk_button_new_with_label ("Debug");
    gtk_signal_connect (GTK_OBJECT (thing), "clicked", (GtkSignalFunc) did_debug, NULL);
    gtk_box_pack_start (GTK_BOX (buttonbox), thing, FALSE, TRUE, 0);
    gtk_widget_show (thing);
    thing = gtk_button_new_with_label ("Quit");
    gtk_signal_connect (GTK_OBJECT (thing), "clicked", (GtkSignalFunc) did_quit, NULL);
    gtk_box_pack_start (GTK_BOX (buttonbox), thing, FALSE, TRUE, 0);
    gtk_widget_show (thing);
    /* An empty but expanded vbox to make the LED appear at the right border.  */
    thing = gtk_vbox_new (FALSE, 0);
    gtk_box_pack_start (GTK_BOX (buttonbox), thing, TRUE, TRUE, 0);
    gtk_widget_show (thing);
    thing = make_led (0);
    gtk_box_pack_start (GTK_BOX (buttonbox), thing, FALSE, TRUE, 0);
    gtk_widget_show (thing);

    /* The next box contains the "Save configuration" button.  */
    buttonbox = gtk_hbox_new (FALSE, 4);
    gtk_box_pack_start (GTK_BOX (vbox), buttonbox, FALSE, TRUE, 0);
    gtk_widget_show (buttonbox);

    thing = gtk_button_new_with_label ("Save config");
    gtk_signal_connect (GTK_OBJECT (thing), "clicked", (GtkSignalFunc) save_config, NULL);
    gtk_box_pack_start (GTK_BOX (buttonbox), thing, FALSE, TRUE, 0);
    gtk_widget_show (thing);

    /* Place a separator below those three buttons.  */
    thing = gtk_hseparator_new ();
    gtk_box_pack_start (GTK_BOX (vbox), thing, FALSE, TRUE, 0);
    gtk_widget_show (thing);

    notebook = gtk_notebook_new ();
    gtk_box_pack_start (GTK_BOX (vbox), notebook, FALSE, TRUE, 0);
    gtk_widget_show (notebook);

    thing = gtk_vbox_new (FALSE, 4);
    gtk_widget_show (thing);
    make_floppy_disks (thing);
    gtk_notebook_append_page (GTK_NOTEBOOK (notebook), thing, gtk_label_new ("Floppy disks"));

    thing = gtk_hbox_new (FALSE, 4);
    gtk_widget_show (thing);
    make_cpu_widgets (thing);
    gtk_notebook_append_page (GTK_NOTEBOOK (notebook), thing, gtk_label_new ("CPU emulation"));
    
    thing = gtk_hbox_new (FALSE, 4);
    gtk_widget_show (thing);
    make_gfx_widgets (thing);
    gtk_notebook_append_page (GTK_NOTEBOOK (notebook), thing, gtk_label_new ("Graphics"));

    thing = gtk_hbox_new (FALSE, 4);
    gtk_widget_show (thing);
    make_joy_widgets (thing);
    gtk_notebook_append_page (GTK_NOTEBOOK (notebook), thing, gtk_label_new ("Game ports"));

    enable_disk_buttons (1);

    gtk_widget_show (vbox);
    gtk_widget_show (window);

    /* We're going to need that later.  */
    selector = gtk_file_selection_new ("");
    gtk_signal_connect (GTK_OBJECT (selector), "destroy",
			(GtkSignalFunc) did_close_insert, selector);

    gtk_signal_connect_object (GTK_OBJECT (GTK_FILE_SELECTION (selector)->ok_button),
			       "clicked", (GtkSignalFunc) did_insert_select,
			       GTK_OBJECT (selector));
    gtk_signal_connect_object (GTK_OBJECT (GTK_FILE_SELECTION (selector)->cancel_button),
			       "clicked", (GtkSignalFunc) did_cancel_insert,
			       GTK_OBJECT (selector));
    filesel_active = -1;

    gtk_timeout_add (1000, (GtkFunction)my_idle, 0);
    gtk_main ();
    
    quitted_gui = 1;
    uae_sem_post (&gui_quit_sem);
    return 0;
}

void gui_changesettings(void)
{
    
}

int gui_init (void)
{
    penguin_id tid;

    gui_active = 0;
    
    init_comm_pipe (&to_gui_pipe, 20, 1);
    init_comm_pipe (&from_gui_pipe, 20, 1);
    uae_sem_init (&gui_sem, 0, 1);
    uae_sem_init (&gui_quit_sem, 0, 0);
    start_penguin (gtk_penguin, NULL, &tid);
    return 1;
}

int gui_update (void)
{
    if (no_gui)
	return 0;

    write_comm_pipe_int (&to_gui_pipe, 1, 1);    
    return 0;
}

void gui_exit (void)
{
    if (no_gui)
	return;

    quit_gui = 1;
    uae_sem_wait (&gui_quit_sem);
}

void gui_led(int num, int on)
{
    if (no_gui)
	return;

/*    if (num == 0)
	return;
    printf("LED %d %d\n", num, on);
    write_comm_pipe_int (&to_gui_pipe, 1, 0);
    write_comm_pipe_int (&to_gui_pipe, num == 0 ? 4 : num - 1, 0);
    write_comm_pipe_int (&to_gui_pipe, on, 1);
    printf("#LED %d %d\n", num, on);*/
}

void gui_filename(int num, const char *name)
{
    if (no_gui)
	return;

    write_comm_pipe_int (&to_gui_pipe, 0, 0);
    write_comm_pipe_int (&to_gui_pipe, num, 1);

/*    gui_update ();*/
}

void gui_handle_events(void)
{
    if (no_gui)
	return;

    while (comm_pipe_has_data (&from_gui_pipe)) {
	int cmd = read_comm_pipe_int_blocking (&from_gui_pipe);
	int n;
	switch (cmd) {
	 case 0:
	    n = read_comm_pipe_int_blocking (&from_gui_pipe);
	    changed_prefs.df[n][0] = '\0';
	    break;
	 case 1:
	    n = read_comm_pipe_int_blocking (&from_gui_pipe);
	    uae_sem_wait (&gui_sem);
	    strncpy (changed_prefs.df[n], new_disk_string[n], 255);
	    changed_prefs.df[n][255] = '\0';
	    uae_sem_post (&gui_sem);
	    break;
	 case 2:
	    uae_reset ();
	    break;
	 case 3:
	    activate_debugger ();
	    break;
	 case 4:
	    uae_quit ();
	    break;
	}
    }
    
}
