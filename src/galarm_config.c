#include <stdlib.h>		/* exit */
#include <glib.h>
#include "galarm_config.h"

gchar *sound_cmd = NULL;

void parse_config(void)
{
	GKeyFile *key_file = g_key_file_new();
	gchar *filename = g_strconcat(g_get_home_dir(), "/.galarmrc", NULL);
	GError *error = NULL;
	if (g_key_file_load_from_file
	    (key_file, filename, G_KEY_FILE_KEEP_COMMENTS, &error) == FALSE) {
		g_debug("loading configuration from %s failed: %s", filename,
			error->message);
		exit(EXIT_FAILURE);
	}

	sound_cmd = g_key_file_get_value(key_file,
					 g_key_file_get_start_group(key_file),
					 "sound_cmd", &error);

	if (sound_cmd == NULL) {
		g_printerr("please provide a sound_cmd in the config file.\n");
		_exit(EXIT_FAILURE);
	}

	g_free(filename);
}
