#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <unistd.h>      /* fork, chdir */
#include <stdlib.h>      /* exit */
#include <math.h>        /* HUGE_VAL */
#include <sys/stat.h>    /* umask */
#include <errno.h>
#include <time.h>        /* localtime, time, gmtime */
#include <fcntl.h>       /* creat - g_creat needs it! (?) */
#include <gtk/gtk.h>     /* 2.14 required */
#include <glib.h>
#include <glib/gstdio.h> /* g_creat */
#include <libnotify/notify.h>
#include "galarm_config.h"
