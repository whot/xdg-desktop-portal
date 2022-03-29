#include <config.h>

#include "inputcapture.h"

#include <libportal/portal.h>
#include "src/xdp-impl-dbus.h"

extern char outdir[];

static int got_info;

static void
input_capture_cb (GObject *obj,
                  GAsyncResult *result,
                  gpointer data)
{
  XdpPortal *portal = XDP_PORTAL (obj);
  g_autoptr(GError) error = NULL;
  GKeyFile *keyfile = data;
  int response;
  XdpSession *session;

  response = g_key_file_get_integer (keyfile, "result", "response", NULL);

  session = xdp_portal_create_input_capture_session_finish (portal, result, &error);

  if (response == 0)
    {
      g_assert_no_error (error);
    }
  else if (response == 1)
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  else if (response == 2)
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);
  else
    g_assert_not_reached ();

  got_info++;

  g_main_context_wakeup (NULL);
}

void
test_inputcapture_basic (void)
{
  g_autoptr(XdpPortal) portal = NULL;
  g_autoptr(GKeyFile) keyfile = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *path = NULL;

  keyfile = g_key_file_new ();

  g_key_file_set_integer (keyfile, "result", "response", 0);

  path = g_build_filename (outdir, "inputcapture", NULL);
  g_key_file_save_to_file (keyfile, path, &error);
  g_assert_no_error (error);

  portal = xdp_portal_new ();

  got_info = 0;
  xdp_portal_create_input_capture_session (portal, XDP_CAPABILITY_POINTER_RELATIVE,
                                           NULL, input_capture_cb, keyfile);

  while (!got_info)
    g_main_context_iteration (NULL, TRUE);
}
