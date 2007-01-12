#include "global.h"

static GError *error = NULL;

static void create_rc(gchar *filename)
{
	g_assert(filename != NULL);
	int rcfile = g_creat(filename, 0644);
	if (rcfile == -1) {
		g_printerr("g_creat failed: %s\n", strerror(errno));
	}
	gchar template[] = "# galarm config\n[Main]\n#sound_cmd=aplay\n# vim: ft=config";
	if (!g_file_set_contents(filename, template, -1, &error)) {
		g_assert(error != NULL);
		g_printerr("%s\n", error->message);
		exit(EXIT_FAILURE);
	}
	g_debug("%s created", filename);
}


void parse_config(void)
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

	sound_cmd = g_key_file_get_value(key_file,
			g_key_file_get_start_group(key_file),
			"sound_cmd", &error);

	if (sound_cmd == NULL || g_utf8_collate(sound_cmd, "") == 0) {
		g_printerr("please provide a sound_cmd in the config file.\n");
	}

	g_free(filename);
}
