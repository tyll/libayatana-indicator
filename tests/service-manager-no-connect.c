
#include <glib.h>
#include "libindicator/indicator-service-manager.h"

static GMainLoop * mainloop = NULL;
static gboolean passed = FALSE;

gboolean
timeout (gpointer data)
{
	passed = TRUE;
	g_debug("Timeout with no connection.");
	g_main_loop_quit(mainloop);
	return FALSE;
}

void
connection (void)
{
	g_debug("Connection");
	passed = FALSE;
	g_main_loop_quit(mainloop);
	return;
}

int
main (int argc, char ** argv)
{
	g_type_init();
	g_log_set_always_fatal(G_LOG_LEVEL_CRITICAL);

	IndicatorServiceManager * is = indicator_service_manager_new("my.test.name");
	g_signal_connect(G_OBJECT(is), INDICATOR_SERVICE_MANAGER_SIGNAL_CONNECTION_CHANGE, connection, NULL);

	g_timeout_add_seconds(1, timeout, NULL);

	mainloop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(mainloop);

	g_debug("Quiting");
	if (passed) {
		g_debug("Passed");
		return 0;
	}
	g_debug("Failed");
	return 1;
}
