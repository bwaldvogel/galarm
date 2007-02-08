/*
 * written by Benedikt Waldvogel, bene@0x11.net
 * October/November 2006
 *
 * Requires GTK+ 2.10 because of GtkStatusIcon
 */
#include "global.h"

/* globals {{{ */
static const guint MAX_SECONDS = 24 * 3600;
static const guint BLINK_TRESHOLD = 10;

static gboolean daemonize = FALSE;
static gboolean quiet = FALSE;
static gboolean verbose = FALSE;
static gboolean stopall = FALSE;
static gchar **opt_remaining = NULL;

/* flag which indicates whether we are in
 * countdown mode or in fixed-endtime mode */
gboolean countdownMode = TRUE;
static gdouble secondsToCount = 0.0l; /* countdown mode */
static time_t endtime = 0;            /* fixed-endtime mode */

static gboolean timer_paused = FALSE;
static GtkStatusIcon *icon;
static GTimer *timer;
static gchar *alarm_message = NULL;

static GError *error = NULL;

static GOptionEntry entries[] = {
	{"daemon", 'd', 0, G_OPTION_ARG_NONE, &daemonize, "start as daemon", NULL},
	{"quiet", 'q', 0, G_OPTION_ARG_NONE, &quiet, "do not play sounds", NULL},
	{"verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose, "Be verbose", NULL},
	{"stopall", 0, 0, G_OPTION_ARG_NONE, &stopall, "Stop all running galarm instances", NULL},
	{"stop-all", 0, G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_NONE, &stopall, NULL, NULL},
	{G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &opt_remaining, "Message to display", "TIMEOUT [MESSAGE]"}
};
/* }}} */

static time_t now(void) /* {{{ */
{
	time_t now = time(NULL);
	if (now == ((time_t)-1)) {
		g_printerr("time failed: %s\n", g_strerror(errno));
		exit(EXIT_FAILURE);
	}
	return now;
}
/* }}} */

static void set_endtime(const char* time_str) /* {{{ */
{
	time_t n = now();
	struct tm *temp_end = localtime(&n);

	gchar **splits = g_strsplit(time_str, ":", 3);

	/* HOUR */
	if (splits[0] != NULL) {
		unsigned long hour = strtoul(splits[0], NULL, 10);
		if (hour >= 24 || splits[0][0] == 0) {
			g_printerr("invalid hour: %s\n", g_strerror(ERANGE));
			exit(EXIT_FAILURE);
		}
		temp_end->tm_hour = hour;

		/* MINUTE */
		if (splits[1] != NULL) {
			unsigned long min = strtoul(splits[1], NULL, 10);
			if (min >= 60 || splits[1][0] == 0) {
				g_printerr("invalid minute: %s\n", g_strerror(ERANGE));
				exit(EXIT_FAILURE);
			}
			temp_end->tm_min = min;
			/* SECONDS */
			if (splits[2] != NULL) {
				unsigned long sec = strtoul(splits[2], NULL, 10);
				if (sec >= 60 || splits[2][0] == 0) {
					g_printerr("invalid seconds: %s\n", g_strerror(ERANGE));
					exit(EXIT_FAILURE);
				}
				temp_end->tm_sec = sec;
			} else {
				/* case no seconds provided (@17:23 eg) */
				temp_end->tm_sec = 0;
			}
		} else {
			/* case no minutes and seconds provided (@18 eg) */
			temp_end->tm_min = 0;
			temp_end->tm_sec = 0;
		}

	} else {
		g_printerr("invalid time\n");
		exit(EXIT_FAILURE);
	}
	g_strfreev(splits);
	endtime = mktime(temp_end);
	/* if hour is before now, assume the next day is meant */
	if (endtime <= n)
		endtime += 24 * 3600;
}
/* }}} */

static gint quit(gpointer data) /* {{{ */
{
	gtk_main_quit();
	return FALSE;
}
/* }}} */

static gint play_sound(gpointer data) /* {{{ */
{				
	if (!quiet)
	{
		if (sound_cmd == NULL || g_utf8_collate(sound_cmd, "") == 0) {
			g_printerr("please provide a sound_cmd in the config file.\n");
		} else {

			char *argv[] = { sound_cmd, "alert.wav", NULL };
			g_spawn_async(DATADIR "/sounds/galarm", argv, NULL,
					G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error);
			if (error != NULL) {
				g_printerr("g_spawn_async: %s\n", error->message);
				exit(EXIT_FAILURE);
			}
		}
	} else {
		g_debug("quiet mode - not playing sounds");
	}
	return FALSE;		/* don't continue */
}
/* }}} */

static gint pause_resume(gpointer data) /* {{{ */
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

static gint show_alarm(gpointer data) /* {{{ */
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

static void statusicon_activate(GtkStatusIcon * icon, gpointer data) /* {{{ */
{
}
/* }}} */

static void statusicon_popup(GtkStatusIcon * status_icon, guint button, guint activate_time, gpointer user_data) /* {{{ */
{
	/*** CREATE A MENU ***/
	GtkWidget *menu = gtk_menu_new();
	GtkWidget *quit_item = NULL, *pause_item = NULL;
	quit_item = gtk_menu_item_new_with_label("Quit");
	if (countdownMode) {
		if (timer_paused) {
			pause_item = gtk_menu_item_new_with_label("Continue");
		} else {
			pause_item = gtk_menu_item_new_with_label("Pause");
		}
	}
	if (pause_item != NULL) {
		g_signal_connect(G_OBJECT(pause_item), "activate", G_CALLBACK(pause_resume), NULL);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), pause_item);
		gtk_widget_show(pause_item);
	}

	g_signal_connect(G_OBJECT(quit_item), "activate", G_CALLBACK(quit), NULL);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit_item);
	gtk_widget_show(quit_item);

	gtk_menu_popup(GTK_MENU(menu), NULL, NULL,
			gtk_status_icon_position_menu,
			icon, button, activate_time);
}
/* }}} */

static gint galarm_timer(gpointer data) /* {{{ */
{
	time_t diff_time = -1; /* default: invalid value */

	if (countdownMode) {
		gdouble elapsed = g_timer_elapsed(timer, NULL);
		diff_time = secondsToCount - elapsed;
	} else {
		diff_time = endtime - now();
	}

	if (diff_time <= 0) {
		gtk_status_icon_set_tooltip(icon, "alarm!");
		show_alarm(NULL);
		return FALSE;	/* don't continue */
	}

	gtk_timeout_add(1000, galarm_timer, NULL);

	if (diff_time <= BLINK_TRESHOLD) {
		gtk_status_icon_set_blinking(icon, TRUE);
	}

	GString *buffer = g_string_sized_new(50);
	g_string_printf(buffer, "%s: ", (alarm_message != NULL) ? alarm_message : "alarm");

	char timeBuffer[1024];
	if (strftime(timeBuffer, sizeof(timeBuffer), "%H:%M:%S", gmtime(&diff_time)) == 0) {
		g_printerr("strftime failed\n");
		exit(EXIT_FAILURE);
	}
	timeBuffer[ARRAY_SIZE(timeBuffer) - 1] = 0;
	g_string_append_printf(buffer, "%s", timeBuffer);

	if (timer_paused) {
		g_assert(countdownMode);
		g_string_append_printf(buffer, " (paused)");
	} else {
		if (countdownMode)
			endtime = now() + diff_time;

		char timeBuffer[1024];
		if (strftime(timeBuffer, sizeof(timeBuffer), " (@%H:%M:%S)", localtime(&endtime)) == 0) {
			g_printerr("strftime failed\n");
			exit(EXIT_FAILURE);
		}
		timeBuffer[ARRAY_SIZE(timeBuffer) - 1] = 0;
		g_string_append_printf(buffer, "%s", timeBuffer);
	}

	gtk_status_icon_set_tooltip(icon, buffer->str);
	g_string_free(buffer, TRUE);

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
			"Possible Timeout values:\n  5s\t\t5 seconds\n  6.5m\t\t6:30 minutes\n  19\t\t19 minutes\n  7h\t\t7 hours\n  @17\t\tat 17:00 o'clock\n  @17:25\tat 17:25 o'clock");
	g_option_context_add_main_entries(context, entries, NULL);
	/* adds GTK+ options */
	g_option_context_add_group(context, gtk_get_option_group(TRUE));
	if (g_option_context_parse(context, &argc, &argv, &error) == FALSE) {
		g_printerr("%s\n", error->message);
		exit(EXIT_FAILURE);
	}
	g_option_context_free(context);

	if (opt_remaining == NULL || opt_remaining[0] == NULL) {
		g_printerr("provide a timeout. see --help\n");
		exit(EXIT_FAILURE);
	}
	gchar *last;

	if (opt_remaining[0][0] == '@')
	{
		countdownMode = FALSE;
		if (g_utf8_strlen(opt_remaining[0], -1) < 2) {
			g_printerr("Please provide a valid endtime. see --help\n");
			exit(EXIT_FAILURE);
		}
		set_endtime(&opt_remaining[0][1]);
	} else {
		countdownMode = TRUE;
		secondsToCount = g_ascii_strtod(opt_remaining[0], &last);
		if (secondsToCount == HUGE_VAL || secondsToCount == -HUGE_VAL) {
			g_printerr("strtod failed: %s\n", g_strerror(errno));
			exit(EXIT_FAILURE);
		}

		switch (*last) {
			case 'h':
				secondsToCount *= 60.0l;
			case 'm':
				secondsToCount *= 60.0l;
			case 's':
				break;
			default:
				/* default case is 'm' */
				secondsToCount *= 60.0l;
		}
	}

	if (secondsToCount < 0.0l || secondsToCount > MAX_SECONDS) {
		g_printerr("Please provide a valid timeout (0..%d seconds)\n",
				MAX_SECONDS);
		exit(EXIT_FAILURE);
	}

	/* the first argument is the timeout value */
	if (opt_remaining[1] != NULL)
		alarm_message = g_strjoinv(" ", opt_remaining + 1);

	if (daemonize) {
		if (fork() != 0) {
			exit(EXIT_SUCCESS);
		}
		if (chdir("/") == -1) {
			g_printerr("chdir / failed: %s\n", g_strerror(errno));
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
