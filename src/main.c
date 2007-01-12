/*
 * written by Benedikt Waldvogel, bene@0x11.net
 * October/November 2006
 *
 * Requires GTK+ 2.10 because of GtkStatusIcon
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <unistd.h>   /* fork, chdir */
#include <stdlib.h>   /* exit */
#include <sys/stat.h> /* umask */
#include <errno.h>
#include <time.h>     /* localtime, time, gmtime */
#include <gtk/gtk.h>  /* 2.10 required */
#include <glib.h>

#include "galarm_config.h"

static const guint MAX_SECONDS = 24 * 3600;
static const guint BLINK_TRESHOLD = 10;

static gboolean daemonize = FALSE;
static gboolean quiet = FALSE;
static gboolean verbose = FALSE;
static gboolean stopall = FALSE;
static gchar **opt_remaining = NULL;

static gdouble seconds = 0.0l;
static gboolean timer_paused = FALSE;
static GtkStatusIcon *icon;
static GTimer *timer;
static gchar *alarm_message = NULL;

static GOptionEntry entries[] = {
	{"daemon", 'd', 0, G_OPTION_ARG_NONE, &daemonize, "start as daemon", NULL},
	{"quiet", 'q', 0, G_OPTION_ARG_NONE, &quiet, "do not play sounds", NULL},
	{"verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose, "Be verbose", NULL},
	{"stopall", 0, 0, G_OPTION_ARG_NONE, &stopall, "Stop all running galarm instances", NULL},
	{"stop-all", 0, G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_NONE, &stopall, NULL, NULL},
	{G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &opt_remaining, "Message to display", "TIMEOUT [MESSAGE]"}
};

gint quit(gpointer data)
{				/* {{{ */
	gtk_main_quit();
	return FALSE;
}				/* }}} */

void statusicon_activate(GtkStatusIcon * icon, gpointer data)
{
}

gint pause_resume(gpointer data) /* {{{ */
{
	if (timer_paused) {
		g_timer_continue(timer);
	} else {
		g_timer_stop(timer);
	}
	timer_paused = !timer_paused;

	return FALSE;		/* don't continue */
}
/* }}} */

void statusicon_popup(GtkStatusIcon * status_icon,	/* {{{ */
		      guint button, guint activate_time, gpointer user_data)
{
	/*** CREATE A MENU ***/
	GtkWidget *menu = gtk_menu_new();
	GtkWidget *quit_item, *pause_item;
	quit_item = gtk_menu_item_new_with_label("Quit");
	if (timer_paused) {
		pause_item = gtk_menu_item_new_with_label("Continue");
	} else {
		pause_item = gtk_menu_item_new_with_label("Pause");
	}

	g_signal_connect(G_OBJECT(quit_item), "activate", G_CALLBACK(quit),
			 NULL);
	g_signal_connect(G_OBJECT(pause_item), "activate",
			 G_CALLBACK(pause_resume), NULL);

	gtk_menu_shell_append(GTK_MENU_SHELL(menu), pause_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit_item);
	gtk_widget_show(pause_item);
	gtk_widget_show(quit_item);

	gtk_menu_popup(GTK_MENU(menu), NULL, NULL,
		       gtk_status_icon_position_menu,
		       icon, button, activate_time);
}
/* }}} */

gint play_sound(gpointer data) /* {{{ */
{				
	if (!quiet) {
		g_assert(sound_cmd != NULL);
		char *argv[] = { sound_cmd, "-q", "alert.wav", NULL };
		GError *error = NULL;
		g_spawn_async(DATADIR "/sounds/galarm",
			      argv,
			      NULL,
			      G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error);
		if (error != NULL) {
			g_printerr("g_spawn_async: %s\n", error->message);
			_exit(EXIT_FAILURE);
		}
	} else {
		g_debug("quiet mode - not playing sounds\n");
	}
	return FALSE;		/* don't continue */
}
/* }}} */

gint show_alarm(gpointer data) /* {{{ */
{
	/* window */
	GtkWidget *alarm_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_decorated(GTK_WINDOW(alarm_window), FALSE);
	/* hints: keep above, sticky and don't show in pager */
	gtk_window_set_keep_above(GTK_WINDOW(alarm_window), TRUE);
	gtk_window_stick(GTK_WINDOW(alarm_window));
	gtk_window_set_skip_pager_hint(GTK_WINDOW(alarm_window), TRUE);
	gtk_window_set_accept_focus(GTK_WINDOW(alarm_window), FALSE);

	/* label */
	GtkWidget *header_label = gtk_label_new(NULL);
	gchar *text = g_markup_printf_escaped("<big><b>Alarm</b></big>");
	gtk_label_set_markup(GTK_LABEL(header_label), text);
	g_free(text);
	gtk_label_set_line_wrap(GTK_LABEL(header_label), TRUE);

	GtkWidget *vbox = gtk_vbox_new(FALSE, 5);
	GtkWidget *hbox = gtk_hbox_new(FALSE, 5);

	/* xalign, yalign, xscale, yscale */
	GtkWidget *align = gtk_alignment_new(0.0f, 0.0f, 1.0f, 1.0f);
	gtk_alignment_set_padding(GTK_ALIGNMENT(align), 10, 10, 10, 10);
	GtkWidget *eventbox = gtk_event_box_new();
	g_signal_connect(GTK_OBJECT(eventbox), "button_press_event",
			 G_CALLBACK(quit), NULL);
	GtkWidget *frame = gtk_frame_new(NULL);

	GtkWidget *image =
	    gtk_image_new_from_file(DATADIR "/pixmaps/galarm.png");
	g_assert(image != NULL);

	gtk_box_pack_start(GTK_BOX(vbox), header_label, TRUE, TRUE, 0);

	if (alarm_message != NULL) {
		GtkWidget *message_label = gtk_label_new(alarm_message);
		gtk_box_pack_end(GTK_BOX(vbox), message_label, TRUE, TRUE, 0);
	}

	gtk_box_pack_start(GTK_BOX(hbox), image, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);

	gtk_container_add(GTK_CONTAINER(align), hbox);
	gtk_container_add(GTK_CONTAINER(frame), align);
	gtk_container_add(GTK_CONTAINER(eventbox), frame);
	gtk_container_add(GTK_CONTAINER(alarm_window), eventbox);

	gtk_widget_show_all(alarm_window);
	gtk_idle_add(play_sound, NULL);

	return FALSE;
}
/* }}} */

gint galarm_timer(gpointer data) /* {{{ */
{
	gdouble elapsed = g_timer_elapsed(timer, NULL);
	time_t diff_time = seconds - elapsed;

	g_assert(icon != NULL);

	if (diff_time <= 0) {
		gtk_status_icon_set_tooltip(icon, "alarm!");
		show_alarm(NULL);
		return FALSE;	/* don't continue */
	}
	gtk_timeout_add(1000, galarm_timer, NULL);

	if (diff_time <= BLINK_TRESHOLD) {
		gtk_status_icon_set_blinking(icon, TRUE);
	}

	gchar buffer[1024];
	if (timer_paused) {
		g_snprintf(buffer, 1024, "%s: %02d:%02d:%02d (paused)",
			   (alarm_message != NULL) ? alarm_message : "alarm",
			   (int)(diff_time / 3600) % 24,
			   (int)(diff_time / 60) % 60, (int)diff_time % 60);
	} else {
		time_t end_time = time(NULL) + diff_time;

		gint n = g_snprintf(buffer, sizeof(buffer), "%s: ",
				    (alarm_message !=
				     NULL) ? alarm_message : "alarm");

		n += strftime(buffer + n, sizeof(buffer), "%H:%M:%S",
			      gmtime(&diff_time));
		n += strftime(buffer + n, sizeof(buffer), " (%H:%M:%S)",
			      localtime(&end_time));
	}

	gtk_status_icon_set_tooltip(icon, buffer);

	return FALSE;		/* don't continue */
}
/* }}} */

int main(int argc, char **argv) /* {{{ */
{
	timer = g_timer_new();
	g_assert(timer != NULL);

	GError *error = NULL;
	GOptionContext *context;

	parse_config();

	context = g_option_context_new(NULL);
	g_assert(context != NULL);
	g_option_context_set_summary(context,
				     "Possible Timeout values:\n  5s\t\t5 seconds\n  6.5m\t\t6:30 minutes\n  19\t\t19 minutes\n  7h\t\t7 hours");
	g_option_context_add_main_entries(context, entries, NULL);
	/* adds GTK+ options */
	g_option_context_add_group(context, gtk_get_option_group(TRUE));
	if (g_option_context_parse(context, &argc, &argv, &error) == FALSE) {
		g_printerr("%s\n", error->message);
		_exit(EXIT_FAILURE);
	}
	g_option_context_free(context);

	if (opt_remaining == NULL || opt_remaining[0] == NULL) {
		g_printerr("provide a timeout. see help\n");
		_exit(EXIT_FAILURE);
	}
	gchar *last;
	seconds = g_ascii_strtod(opt_remaining[0], &last);

	switch (*last) {
		case 'h':
			seconds *= 60.0l;
		case 'm':
			seconds *= 60.0l;
		case 's':
			break;
		default:
			/* default case is 'm' */
			seconds *= 60.0l;
	}

	if (seconds < 0.0l || seconds > MAX_SECONDS) {
		g_printerr("Please provide a valid timeout (0..%d seconds)\n",
			   MAX_SECONDS);
		exit(EXIT_FAILURE);
	}

	/* the first argument is the timeout value */
	if (opt_remaining[1] != NULL)
		alarm_message = g_strjoinv(" ", opt_remaining + 1);

	if (daemonize) {
		if (verbose)
			g_printf("going to daemonize.\n");
		if (fork() != 0) {
			exit(EXIT_SUCCESS);
		}
		if (chdir("/") == -1) {
			fprintf(stderr, "chdir / failed: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		}
		umask(0);
        pid_t sid = setsid();
        if (sid < 0) {
			exit(EXIT_FAILURE);
        }
		close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
	}

	gtk_init(&argc, &argv);

	icon = gtk_status_icon_new_from_file(DATADIR "/pixmaps/galarm.png");
	g_signal_connect(G_OBJECT(icon), "activate",
			 G_CALLBACK(statusicon_activate), NULL);
	g_signal_connect(G_OBJECT(icon), "popup-menu",
			 G_CALLBACK(statusicon_popup), NULL);
	gtk_status_icon_set_visible(icon, TRUE);

	galarm_timer(0);
	gtk_main();

	return 0;
}
/* }}} */

/* vim: set foldmethod=marker foldlevel=0 : */
