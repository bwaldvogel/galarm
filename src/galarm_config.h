#ifndef __GALARM_CONFIG_H__
#define __GALARM_CONFIG_H__

#include <glib.h>

#ifndef EXIT_SUCCESS
#  define EXIT_SUCCESS 0
#endif
#ifndef EXIT_FAILURE
#  define EXIT_FAILURE 1
#endif

char *sound_cmd;
void parse_config(void);

#endif /* !__GALARM_CONFIG_H__ */
