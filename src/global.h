#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <unistd.h>      /* fork, chdir */
#include <stdlib.h>      /* exit */
#include <math.h>        /* HUGE_VAL */
#include <sys/stat.h>    /* umask */
#include <errno.h>
#include <time.h>        /* localtime, time, gmtime */
#include <gtk/gtk.h>     /* 2.10 required */
#include <glib.h>
#include <glib/gstdio.h> /* g_creat */
#include <fcntl.h>       /* creat - g_creat needs it! (?) */
#include "galarm_config.h"
