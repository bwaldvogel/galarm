/*
 * written by Benedikt Waldvogel, bene /at/ 0x11.net
 * Oct 2006 - Aug 2007
 *
 * Requires GTK+ 2.14 because of Perl-compatible regular expressions
 * -- Requires GTK+ 2.10 because of GtkStatusIcon
 */
#include "global.h"

/* globals */
static const guint          MAX_SECONDS       = 7 *24 *3600;
static const guint          BLINK_TRESHOLD    = 10;

static gboolean             daemonize         = FALSE;
static gboolean             quiet             = FALSE;
static gboolean             verbose           = FALSE;
static gboolean             stopall           = FALSE;
static gchar              **opt_remaining     = NULL;

/* flag which indicates whether we are in
 * countdown mode or in fixed-endtime mode */
gboolean                    countdownMode     = TRUE;
static gdouble              secondsToCount    = 0.0l;        /* countdown mode     */
static time_t               endtime           = 0;           /* fixed-endtime mode */

static gboolean             timer_paused      = FALSE;
static GtkStatusIcon       *icon;
static GTimer              *timer;
static gchar               *alarm_message     = NULL;
static gchar                DEFAULT_MESSAGE[] = "alarm";

static GError              *error             = NULL;
static NotifyNotification  *notification      = NULL;

static GOptionEntry entries[] = {
	{"daemon", 'd', 0, G_OPTION_ARG_NONE, &daemonize, "start as daemon", NULL},
	{"quiet", 'q', 0, G_OPTION_ARG_NONE, &quiet, "do not play sounds", NULL},
	{"verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose, "Be verbose", NULL},
	{"stopall", 0, 0, G_OPTION_ARG_NONE, &stopall, "Stop all running galarm instances", NULL},
	{"stop-all", 0, G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_NONE, &stopall, NULL, NULL},
	{G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &opt_remaining, "Message to display", "TIMEOUT [MESSAGE]"}
};

static time_t now(void)
{
	time_t now = time(NULL);
	if (now == ((time_t)-1)) {
		g_printerr("time failed: %s\n", g_strerror(errno));
		exit(EXIT_FAILURE);
	}
	return now;
}

static void set_endtime(const char* time_str)
{
	time_t n = now();

	/* RELATIVE TIME FORMATS
		1.5d   → 36 hours
		1,2m   → 1 minute and 12 seconds
		1h     → 3600 seconds
		1:12m  → the same
		9.5s   → 9 seconds since precision is only one second
		1:30d  → 1 and a half day

	   ABSOLUTE TIME FORMATS
		@12:30 → at half past 12pm
		@9pm   → 21 o'clock
	*/
	GRegex *reltime = g_regex_new("^(?P<Number>(?P<First>\\d{1,5})(?P<Part2>(?P<Format>[:,.])(?P<Second>\\d{1,6}))?)(?P<Qualifier>[dhms]?)$",
			G_REGEX_CASELESS | G_REGEX_OPTIMIZE, 0, &error);
	if (reltime == NULL) {
		g_printerr("g_regex_new: %s\n", error->message);
		error = NULL;
		exit(EXIT_FAILURE);
	}
	GRegex *abstime = g_regex_new("^\\@(?P<Hour>\\d{1,2})(:(?P<Minute>[0-5]?[0-9]))?(:(?P<Second>[0-5]?[0-9]))?(?P<AmPm>[ap]m)?$",
			G_REGEX_CASELESS | G_REGEX_OPTIMIZE, 0, &error);
	if (abstime == NULL) {
		g_printerr("g_regex_new: %s\n", error->message);
		error = NULL;
		exit(EXIT_FAILURE);
	}

	GMatchInfo *matchinfo;

	if (g_regex_match(reltime, time_str, 0, &matchinfo)) {
		gchar  *number     = g_match_info_fetch_named (matchinfo, "Number");
		gchar  *first      = g_match_info_fetch_named (matchinfo, "First");
		gchar  *part2      = g_match_info_fetch_named (matchinfo, "Part2");
		gchar  *format     = g_match_info_fetch_named (matchinfo, "Format");
		gchar  *second     = g_match_info_fetch_named (matchinfo, "Second");
		gchar  *qualifier  = g_match_info_fetch_named (matchinfo, "Qualifier");
		glong   first_num  = atol(first);
		// days, hours, minutes and seconds
		// default is minutes
		switch (qualifier[0]) {
			case 'd':
				secondsToCount = 24*3600;
				break;
			case 'h':
				secondsToCount = 3600;
				break;
			case 's':
				secondsToCount = 1;
				break;
			case 'm':
			case '\0':
			default:
				secondsToCount = 60;
				break;
		}

		if (part2[0]) {
			glong   second_num = atol(second);
			long double f;
			gdouble v;
			switch (format[0]) {
				case ':':
					if (second_num >= 60) {
						g_printerr("provide a valid timeout. see --help\n");
						exit(EXIT_FAILURE);
					}
					f = first_num;
					f += (long double)second_num/60.0;
					secondsToCount *= f;
					break;
				case ',':
				case '.':
					v = g_ascii_strtod(number, NULL);
					g_printf("number: %s → %lf\n", number, v);
					secondsToCount *= v;

					break;
				default:
					// this mustn't happen since we catch the format in the regex
					// and this switch case 'should' be closely tied to the pattern
					g_assert(FALSE);
			}
		} else {
			secondsToCount *= first_num;
		}
		g_free(number), g_free(first), g_free(part2), g_free(format), g_free(second), g_free(qualifier);

	} else if (g_regex_match(abstime, time_str, 0, &matchinfo)) {

		countdownMode = FALSE;

		gchar *hour    = g_match_info_fetch_named (matchinfo, "Hour");
		gchar *minute  = g_match_info_fetch_named (matchinfo, "Minute");
		gchar *seconds = g_match_info_fetch_named (matchinfo, "Second");
		gchar *ampm    = g_match_info_fetch_named (matchinfo, "AmPm");
		glong  h       = atol(hour);

		if (h>=24) {
			g_printerr("provide a valid timeout. see --help\n");
			exit(EXIT_FAILURE);
		}

		if (ampm && g_strncasecmp(ampm, "pm", 2) == 0)
			h+=12;

		struct tm *end = localtime(&n);
		end->tm_hour = h;

		if (minute)
			end->tm_min = atol(minute);
		else
			end->tm_min = 0;

		if (seconds)
			end->tm_sec = atol(seconds);
		else
			end->tm_sec = 0;

		g_free(hour), g_free(minute), g_free(seconds), g_free(ampm);
		/* if hour is before now, assume the next day is meant */
		endtime = mktime(end);

		if (endtime <= n)
			endtime += 24 * 3600;

	} else {
		g_printerr("provide a valid timeout. see --help\n");
		exit(EXIT_FAILURE);
	}

	g_regex_unref(reltime);
	g_regex_unref(abstime);
}

static gint quit(gpointer data)
{
	if (!notify_notification_close(notification, &error)) {
		g_printerr("notify_notification_close: %s\n", error->message);
		error = NULL;
	}
	notification = NULL;
	gtk_main_quit();
	return FALSE;
}

static void interrupt(int a) {
	quit(0);
}

static gint play_sound(gpointer data)
{
	char *argv[] = { sound_cmd, "alert.wav", NULL };
	g_spawn_async(DATADIR "/sounds/galarm", argv, NULL,
			G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error);
	if (error != NULL) {
		g_printerr("g_spawn_async: %s\n", error->message);
		error = NULL;
		exit(EXIT_FAILURE);
	}
	return FALSE;		/* don't continue */
}

static gint pause_resume(gpointer data)
{
	if (timer_paused) {
		g_timer_continue(timer);
	} else {
		g_timer_stop(timer);
	}
	timer_paused = !timer_paused;

	return FALSE;		/* don't continue */
}

static void prepare_notification (void)
{
	if (!notify_init("galarm")) {
		g_printerr("notify_init failed");
		exit(EXIT_FAILURE);
	}

	notification = notify_notification_new ("Alarm", alarm_message, NULL, NULL);
	g_signal_connect (notification, "closed", G_CALLBACK(quit), NULL);
	GdkPixbuf *icon = gdk_pixbuf_new_from_file(DATADIR "/pixmaps/galarm.png", &error);
	if (icon == NULL) {
		g_printerr("gdk_pixbuf_new_from_file: %s\n", error->message);
		error = NULL;
	} else {
		notify_notification_set_icon_from_pixbuf(notification, icon);
	}
	notify_notification_set_urgency (notification, NOTIFY_URGENCY_CRITICAL);
	notify_notification_set_timeout(notification, atol(popup_timeout));
}

static gint show_alarm(gpointer data)
{
	if (!notify_notification_show (notification, &error)) {
		g_printerr("notify_notification_show: %s\n", error->message);
		error = NULL;
	}
	if (!quiet)
		gtk_idle_add(play_sound, NULL);

	return FALSE;
}

static void statusicon_activate(GtkStatusIcon * icon, gpointer data)
{ }

static void statusicon_popup(GtkStatusIcon * status_icon, guint button, guint activate_time, gpointer user_data)
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

/* this function is called periodically */
static gint galarm_timer(gpointer data)
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
	g_string_printf(buffer, "%s: ", alarm_message);

	char timeBuffer[1024];
	if (strftime(timeBuffer, sizeof(timeBuffer), "%H:%M:%S", gmtime(&diff_time)) == 0) {
		g_printerr("strftime failed\n");
		exit(EXIT_FAILURE);
	}
	timeBuffer[ARRAY_SIZE(timeBuffer) - 1] = 0;
	if (diff_time > 24*3600) {
		g_string_append_printf(buffer, "%ldd %s", diff_time/(24*3600), timeBuffer);
	} else {
		g_string_append_printf(buffer, "%s", timeBuffer);
	}

	if (timer_paused) {
		g_assert(countdownMode);
		g_string_append_printf(buffer, " (paused)");
	} else {
		if (countdownMode)
			endtime = now() + diff_time;

		char timeBuffer[1024];
		gint ret = 0;

		if (diff_time < 24*3600)
			ret = strftime(timeBuffer, sizeof(timeBuffer), " (@%H:%M:%S)", localtime(&endtime));
		else
			ret = strftime(timeBuffer, sizeof(timeBuffer), " (@%d.%m. %H:%M:%S)", localtime(&endtime));

		if (ret == 0) {
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

int main(int argc, char **argv)
{

	signal(SIGINT, interrupt);

	timer = g_timer_new();
	g_assert(timer != NULL);

	GError *error = NULL;
	GOptionContext *context;

	parse_config();

	context = g_option_context_new(NULL);
	g_assert(context != NULL);
	g_option_context_set_help_enabled(context, TRUE);
	/* g_option_context_set_summary(context,
			"Possible Timeout values:\n  5s\t\t5 seconds\n  6.5m\t\t6:30 minutes\n  19\t\t19 minutes\n  7h\t\t7 hours\n  @17\t\tat 17:00 o'clock\n  @17:25\tat 17:25 o'clock"); */
	g_option_context_add_main_entries(context, entries, NULL);

	/* adds GTK+ options */
	g_option_context_add_group(context, gtk_get_option_group(TRUE));
	if (g_option_context_parse(context, &argc, &argv, NULL) == FALSE) {
		g_printerr("%s\n", error->message);
		error = NULL;
		exit(EXIT_FAILURE);
	}

	g_option_context_free(context);

	if (opt_remaining == NULL || opt_remaining[0] == NULL) {
		g_printerr("provide a valid timeout. see --help\n");
		exit(EXIT_FAILURE);
	}

	set_endtime(opt_remaining[0]);

	/* the first argument is the timeout value */
	if (opt_remaining[1] != NULL)
		alarm_message = g_strjoinv(" ", opt_remaining + 1);
	if (alarm_message == NULL || alarm_message[0]==0)
		alarm_message = DEFAULT_MESSAGE;

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

	prepare_notification();
	// start the timer loop
	galarm_timer(NULL);
	gtk_main();

	return 0;
}

// vim: set foldmethod=syntax foldlevel=0 :
