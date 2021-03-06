/*
 * written by Benedikt Waldvogel, bene /at/ 0x11.net
 * 2006 - 2018
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#define APPLICATION_NAME     "galarm"
#define APPLICATION_VERSION  "dev"
#define SHARE_INSTALL_PREFIX "/usr/local/share"
#endif

#include <unistd.h>      /* fork, chdir */
#include <stdlib.h>      /* exit */
#include <math.h>        /* fabs */
#include <sys/stat.h>    /* umask */
#include <errno.h>
#include <signal.h>
#include <time.h>        /* localtime, time, gmtime */
#include <fcntl.h>       /* creat - g_creat needs it! (?) */

#include <gtk/gtk.h>     /* 2.12 required */
#include <glib.h>        /* 2.14 required */
#include <glib/gstdio.h> /* g_creat */
#include <libnotify/notify.h>

#ifdef HAVE_CANBERRA
#  include <canberra.h>
#endif

#ifndef EXIT_SUCCESS
#  define EXIT_SUCCESS 0
#endif
#ifndef EXIT_FAILURE
#  define EXIT_FAILURE 1
#endif

#ifndef DEBUG
#  undef g_debug
#  define g_debug(...)
#endif

/* http://stackoverflow.com/questions/285591/using-the-gcc-unused-attribute-with-objective-c/285785#285785 */
#ifndef UNUSED
#  define UNUSED(x) (void)x
#endif

/* globals */
static const guint          BLINK_TRESHOLD    = 10;

static gboolean             daemonize         = FALSE;
static gboolean             quiet             = FALSE;
static gboolean             verbose           = FALSE;
static gboolean             version           = FALSE;
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

static long                popup_timeout      = NOTIFY_EXPIRES_NEVER;

static GError              *error             = NULL;
static NotifyNotification  *notification      = NULL;

#ifdef HAVE_CANBERRA
static ca_context          *canberra          = NULL;
#endif

static GOptionEntry entries[] = {
    {"daemon", 'd', 0, G_OPTION_ARG_NONE, &daemonize, "start as daemon", NULL},
    {"quiet", 'q', 0, G_OPTION_ARG_NONE, &quiet, "do not play sounds", NULL},
    {"verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose, "be verbose", NULL},
    {"version", 0, 0, G_OPTION_ARG_NONE, &version, "print application version and exit", NULL},
    {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &opt_remaining, "Message to display", "TIMEOUT [MESSAGE]"},
    {0, 0, 0, 0, 0, 0, 0}
};

static void create_rc(gchar *filename)
{
    g_assert(filename != NULL);
    int rcfile = g_creat(filename, 0644);
    if (rcfile == -1) {
        g_printerr("g_creat failed: %s\n", g_strerror(errno));
        return;
    }
    gchar template[] = "# galarm config\n[Main]\n# popup_timeout=5.0\n# vim: set ft=config :";
    if (!g_file_set_contents(filename, template, -1, &error)) {
        g_assert(error != NULL);
        g_printerr("%s\n", error->message);
        exit(EXIT_FAILURE);
    }

    g_debug("%s created", filename);
}

static void parse_config(void)
{
    GKeyFile *key_file = g_key_file_new();
    gchar *filename = g_strconcat(g_get_home_dir(), "/.galarmrc", NULL);
    GError *error = NULL;

    if (!g_file_test (filename, G_FILE_TEST_EXISTS)) {
        create_rc(filename);
    }

    if (g_key_file_load_from_file
            (key_file, filename, G_KEY_FILE_KEEP_COMMENTS, &error) == FALSE) {
        g_debug("loading configuration from %s failed: %s", filename,
                error->message);
        exit(EXIT_FAILURE);
    }

    g_assert(error == NULL);

    gchar *timeout = g_key_file_get_value(key_file,
            g_key_file_get_start_group(key_file),
            "popup_timeout", &error);

    if (timeout != NULL && g_utf8_collate(timeout, "")) {
        popup_timeout = atof(timeout) * 1000;
        g_debug("popup timeout configured to %ld", popup_timeout);
    }

    g_free(filename);
}

static time_t now(void)
{
    time_t now = time(NULL);
    if (now == ((time_t)-1)) {
        g_printerr("time failed: %s\n", g_strerror(errno));
        exit(EXIT_FAILURE);
    }
    return now;
}

static struct tm* now_t(void)
{
    time_t n = now();
    return localtime(&n);
}

static void parse_reltime(gchar *first, gchar *part2, gchar *format,
        gchar *second, gchar *qualifier)
{
    glong first_num  = atol(first);
    glong second_num = 0;

    // days, hours, minutes (default) and seconds
    if (g_strcmp0(qualifier, "d") == 0) {
        secondsToCount = 24 * 3600;
    } else if (g_strcmp0(qualifier, "h") == 0) {
        secondsToCount = 3600;
    } else if (g_strcmp0(qualifier, "m") == 0
            || g_strcmp0(qualifier, "min") == 0
            || g_strcmp0(qualifier, "") == 0) {
        secondsToCount = 60;
    } else if (g_strcmp0(qualifier, "s") == 0
            || g_strcmp0(qualifier, "sec") == 0) {
        secondsToCount = 1;
    } else {
        g_printerr("unknown qualifier: '%s'\n", qualifier);
        exit(EXIT_FAILURE);
    }

    // calculate the fraction here
    long double frac = 0;
    if (part2[0]) {
        size_t i;
        switch (format[0]) {
            case ':':
                second_num = atol(second);
                if (second_num >= 60) {
                    g_printerr("provide a valid timeout. see --help\n");
                    exit(EXIT_FAILURE);
                }
                frac = second_num / 60.0L;
                break;
            case ',':
            case '.':
                i = 0;
                while (second[i]) {
                    frac += (second[i] - '0') / (long double)pow(10, i+1);
                    i++;
                }
                break;
        }
    }
    secondsToCount *= first_num + frac;

    if (verbose) {
        g_print("alarm in %.0lf seconds\n", secondsToCount);
    }
}

static void parse_abstime(gchar *hour, gchar *minute, gchar *seconds,
        gchar *ampm)
{
    glong  h       = atol(hour);

    if (h>=24) {
        g_printerr("provide a valid timeout. see --help\n");
        exit(EXIT_FAILURE);
    }

    if (ampm && (ampm[0] == 'P' || ampm[0] == 'p')) // PM
        h+=12;

    struct tm *end = now_t();
    end->tm_hour = h;

    if (minute)
        end->tm_min = atol(minute);
    else
        end->tm_min = 0;

    if (seconds)
        end->tm_sec = atol(seconds);
    else
        end->tm_sec = 0;

    endtime = mktime(end);

    /* if end is before now, assume the next day is meant */
    if (endtime < now())
        endtime += 24 * 3600;

    if (verbose) {
        char timeBuffer[32];
        strftime(timeBuffer, sizeof(timeBuffer), "alarm at %H:%M:%S", localtime(&endtime));
        g_print("%s\n", timeBuffer);
    }
}

static void parse_endtime(const char* time_str)
{
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
    GRegex *reltime = g_regex_new("^(?P<First>\\d{1,5})(?P<Part2>(?P<Format>[:,.])(?P<Second>\\d{1,6}))?(?P<Qualifier>([dhms]|sec|min)?)$",
            G_REGEX_CASELESS | G_REGEX_OPTIMIZE, 0, &error);
    if (reltime == NULL) {
        g_printerr("g_regex_new: %s\n", error->message);
        error = NULL;
        exit(EXIT_FAILURE);
    }
    GRegex *abstime = g_regex_new("^\\@(?P<Hour>\\d{1,2})(:(?P<Minute>[0-5]?[0-9]))?(:(?P<Second>[0-5]?[0-9]))?(?P<AmPm>[ap]\\.?m\\.?)?$",
            G_REGEX_CASELESS | G_REGEX_OPTIMIZE, 0, &error);
    if (abstime == NULL) {
        g_printerr("g_regex_new: %s\n", error->message);
        error = NULL;
        exit(EXIT_FAILURE);
    }

    GMatchInfo *matchinfo;

    if (g_regex_match(reltime, time_str, 0, &matchinfo))
    {
        gchar *first      = g_match_info_fetch_named (matchinfo, "First");
        gchar *part2      = g_match_info_fetch_named (matchinfo, "Part2");
        gchar *format     = g_match_info_fetch_named (matchinfo, "Format");
        gchar *second     = g_match_info_fetch_named (matchinfo, "Second");
        gchar *qualifier  = g_match_info_fetch_named (matchinfo, "Qualifier");

        parse_reltime(first, part2, format, second, qualifier);

        g_free(first), g_free(part2), g_free(format), g_free(second), g_free(qualifier);
    }
    else if (g_regex_match(abstime, time_str, 0, &matchinfo))
    {
        gchar *hour    = g_match_info_fetch_named (matchinfo, "Hour");
        gchar *minute  = g_match_info_fetch_named (matchinfo, "Minute");
        gchar *seconds = g_match_info_fetch_named (matchinfo, "Second");
        gchar *ampm    = g_match_info_fetch_named (matchinfo, "AmPm");

        countdownMode = FALSE;
        parse_abstime(hour, minute, seconds, ampm);

        g_free(hour), g_free(minute), g_free(seconds), g_free(ampm);

    } else {
        g_printerr("provide a valid timeout. see --help\n");
        exit(EXIT_FAILURE);
    }

    g_regex_unref(reltime);
    g_regex_unref(abstime);
}

static void create_canberra() {
#ifdef HAVE_CANBERRA
    int ret = ca_context_create(&canberra);
    if (ret != 0) {
        g_printerr("create canberra: %s\n", ca_strerror(ret));
        g_assert(canberra == NULL);
        quiet = TRUE;
        return;
    }
    g_assert(canberra != NULL);

    ret = ca_context_change_props(canberra,
            CA_PROP_APPLICATION_NAME, "galarm",
            NULL);
    if (ret != 0)
        g_printerr("canberra, change_props: %s\n", ca_strerror(ret));

    g_debug("created canberra context");
#endif
}

static void destroy_canberra() {
#ifdef HAVE_CANBERRA
    if (canberra == NULL) return;
    int ret = ca_context_destroy(canberra);
    if (ret != 0) {
        g_printerr("destroy canberra: %s\n", ca_strerror(ret));
    }
    canberra = NULL;

    g_debug("destroyed canberra context");
#endif
}

static gint quit(gpointer data)
{
    UNUSED(data);
    if (!notify_notification_close(notification, &error)) {
        g_printerr("notify_notification_close: %s\n", error->message);
        error = NULL;
    }
    notification = NULL;

    destroy_canberra();

    gtk_main_quit();
    return FALSE;
}

static void interrupt(int a) {
    UNUSED(a);
    quit(0);
}

static void play_sound()
{
#ifdef HAVE_CANBERRA
    g_assert(canberra != NULL);

    // 'galarm-alert' is no offical XDG event
    // it is installed with galarm and might be replaced with
    // 'alarm-clock-elapsed' as soon as the first sound themes ship it
    int ret = ca_context_play(canberra, 0,
            CA_PROP_EVENT_ID, "galarm-alert",
            NULL);

    if (ret != 0) {
        g_printerr("canberra: %s\n", ca_strerror(ret));
    }
#endif
}

static gint pause_resume(gpointer data)
{
    UNUSED(data);
    if (timer_paused) {
        g_timer_continue(timer);
    } else {
        g_timer_stop(timer);
    }
    timer_paused = !timer_paused;

    return FALSE;       /* don't continue */
}

static void prepare_notification (void)
{
    if (!notify_init("galarm")) {
        g_printerr("notify_init failed");
        exit(EXIT_FAILURE);
    }

    GRegex *weblink = g_regex_new("(ftp|https?)://([^ ]+)(\\.)?\\b", G_REGEX_CASELESS | G_REGEX_OPTIMIZE, 0, &error);
    if (weblink == NULL) {
        g_printerr("g_regex_new: %s\n", error->message);
        error = NULL;
        exit(EXIT_FAILURE);
    }

    alarm_message = g_regex_replace(weblink, alarm_message, -1, 0, "<a href='\\0'>\\0</a>", 0, &error);

    g_regex_unref(weblink);

    if (alarm_message == NULL) {
        g_printerr("g_regex_replace: %s\n", error->message);
        error = NULL;
        exit(EXIT_FAILURE);
    }

    GString *buffer = g_string_sized_new(512);
    g_string_printf(buffer, "<b>%s</b>", alarm_message);
    notification = notify_notification_new ("Alarm", buffer->str, NULL);
    g_string_free(buffer, TRUE);

    g_signal_connect (notification, "closed", G_CALLBACK(quit), NULL);
    GdkPixbuf *icon = gdk_pixbuf_new_from_file(SHARE_INSTALL_PREFIX "/pixmaps/galarm.png", &error);
    if (icon == NULL) {
        g_printerr("gdk_pixbuf_new_from_file: %s\n", error->message);
        error = NULL;
    } else {
        notify_notification_set_icon_from_pixbuf(notification, icon);
    }
    notify_notification_set_category(notification, APPLICATION_NAME);
    notify_notification_set_urgency(notification, NOTIFY_URGENCY_NORMAL);
    notify_notification_set_timeout(notification, popup_timeout);

    create_canberra();
}

static gint show_alarm(gpointer data)
{
    UNUSED(data);
    if (!notify_notification_show (notification, &error)) {
        g_printerr("notify_notification_show: %s\n", error->message);
        error = NULL;
    }

    if (!quiet)
        play_sound();

    return FALSE;
}

static void statusicon_activate(GtkStatusIcon * icon, gpointer data)
{
    UNUSED(icon);
    UNUSED(data);
}

static void statusicon_popup(GtkStatusIcon * status_icon, guint button, guint activate_time, gpointer user_data)
{
    UNUSED(status_icon);
    UNUSED(user_data);

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
static gboolean galarm_timer(gpointer data)
{
    UNUSED(data);

    gdouble diff_time;
    char    timeBuffer[32];
    size_t  ret;

    if (countdownMode) {
        gdouble elapsed = g_timer_elapsed(timer, NULL);
        g_assert(elapsed >= 0);
        diff_time = secondsToCount - elapsed;
    } else {
        diff_time = endtime - now();
    }

    if (diff_time <= 0)
    {
        GString *buffer = g_string_sized_new(512);
        g_string_printf(buffer, "alarm: <b>%s</b>", alarm_message);
        gtk_status_icon_set_tooltip_markup(icon, buffer->str);
        g_string_free(buffer, TRUE);
        show_alarm(NULL);
        return FALSE;   /* don't continue */
    }

    GString *buffer = g_string_sized_new(50);
    g_string_append_printf(buffer, "<b>%s</b>: ", alarm_message);

    time_t tt = fabs(diff_time);
    ret = strftime(timeBuffer, sizeof(timeBuffer), "%H:%M:%S", gmtime(&tt));
    if (ret == 0 || ret >= sizeof(timeBuffer)) {
        g_printerr("strftime failed or buffer too small\n");
        exit(EXIT_FAILURE);
    }

    /* more than one day */
    if (diff_time > 24*3600.0) {
        int days = diff_time/(24*3600.0);
        g_string_append_printf(buffer, "%d day%s %s", days, (days>1)?"s":"", timeBuffer);
    } else {
        /* make the diff RED */
        if (diff_time < BLINK_TRESHOLD) {
            g_string_append_printf(buffer, "<span weight='bold' color='red'>%s</span>", timeBuffer);
        } else {
            g_string_append_printf(buffer, "%s", timeBuffer);
        }
    }

    if (timer_paused) {
        g_assert(countdownMode);
        g_string_append_printf(buffer, " (paused)");
    } else {
        if (countdownMode)
            endtime = now() + diff_time;

        struct tm* end = localtime(&endtime);

        if (end->tm_yday == now_t()->tm_yday)
            ret = strftime(timeBuffer, sizeof(timeBuffer), " <i>(@%H:%M:%S)</i>", localtime(&endtime));
        else
            ret = strftime(timeBuffer, sizeof(timeBuffer), " <i>(@%Y-%m-%d %H:%M:%S)</i>", localtime(&endtime));

        if (ret == 0 || ret >= sizeof(timeBuffer)) {
            g_printerr("strftime failed or buffer too small\n");
            exit(EXIT_FAILURE);
        }
        g_string_append_printf(buffer, "%s", timeBuffer);
    }

    gtk_status_icon_set_tooltip_markup(icon, buffer->str);
    g_string_free(buffer, TRUE);

    // update tooltips on all screens
    GdkDisplay* display = gdk_display_get_default();
    if (display)
        gtk_tooltip_trigger_tooltip_query(display); // requires GTK+-2.12

    return TRUE;        /* continue */
}

int main(int argc, char **argv)
{
    g_set_prgname(APPLICATION_NAME);

    signal(SIGINT, interrupt);

    timer = g_timer_new();
    g_assert(timer != NULL);

    GError *error = NULL;
    GOptionContext *context;

    parse_config();

    context = g_option_context_new(NULL);
    g_assert(context != NULL);
    g_option_context_set_help_enabled(context, TRUE);
    g_option_context_set_summary(context,
            "Possible Timeout values:\n"
            "  5s\t\t5 seconds\n"
            "  30sec\t\t30 seconds\n"
            "  6.5m\t\t6:30 minutes\n"
            "  3min\t\t3 minutes\n"
            "  19\t\t19 minutes\n"
            "  7h\t\t7 hours\n"
            "  1,5d\t\t36 hours\n"
            "  @17\t\tat 17:00 o'clock\n"
            "  @17:25\tat 17:25 o'clock\n"
            "  @9.5pm\tat 21:30 o'clock");
    g_option_context_add_main_entries(context, entries, NULL);

    /* adds GTK+ options */
    g_option_context_add_group(context, gtk_get_option_group(TRUE));
    if (g_option_context_parse(context, &argc, &argv, &error) == FALSE) {
        if (error != NULL)
            g_printerr("%s\n", error->message);
        else
            g_printerr("unknown options. see --help\n");
        error = NULL;
        exit(EXIT_FAILURE);
    }

    g_option_context_free(context);

    if (version) {
        g_printf("%s version %s\n", APPLICATION_NAME, APPLICATION_VERSION);
        exit(EXIT_SUCCESS);
    }

    if (opt_remaining == NULL || opt_remaining[0] == NULL) {
        g_printerr("provide a valid timeout. see --help\n");
        exit(EXIT_FAILURE);
    }

    parse_endtime(opt_remaining[0]);

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

    icon = gtk_status_icon_new_from_file(SHARE_INSTALL_PREFIX "/pixmaps/galarm.png");
    g_signal_connect(G_OBJECT(icon), "activate",
            G_CALLBACK(statusicon_activate), NULL);
    g_signal_connect(G_OBJECT(icon), "popup-menu",
            G_CALLBACK(statusicon_popup), NULL);
    gtk_status_icon_set_visible(icon, TRUE);
    gtk_status_icon_set_tooltip_text(icon, "galarm");

    prepare_notification();
    // start the timer loop
    g_timeout_add(1000, galarm_timer, NULL);
    gtk_main();

    return 0;
}

// vim: set ts=4 sw=4 et foldmethod=syntax :
