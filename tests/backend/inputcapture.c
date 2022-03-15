#include <config.h>
#include <stdio.h>
#include <stdlib.h>

#include <gio/gio.h>

#include "src/xdp-impl-dbus.h"

#include "inputcapture.h"


typedef struct {
  XdpImplInputCapture *impl;
  char *app_id;
  char *id;
  char *action;
} ActionData;

static GKeyFile *
get_config_file (void)
{
  const char *dir = g_getenv ("XDG_DATA_HOME");
  g_autofree char *path = g_build_filename (dir, "inputcapture", NULL);
  g_autoptr(GKeyFile) keyfile = g_key_file_new ();
  g_autoptr(GError) error = NULL;

  g_key_file_load_from_file (keyfile, path, 0, &error);
  g_assert_no_error (error);

  return g_steal_pointer (&keyfile);
}


static gboolean
handle_create_session (XdpImplInputCapture *object,
                       GDBusMethodInvocation *invocation,
                       const gchar *arg_handle,
                       const gchar *arg_session_handle,
                       const gchar *arg_app_id,
                       const gchar *arg_parent_window,
                       GVariant *arg_options)
{
  g_autoptr(GKeyFile) keyfile = get_config_file ();
  g_autoptr(Request) request = NULL;

  sender = g_dbus_method_invocation_get_sender (invocation);
  request = request_new (sender, arg_app_id, arg_handle);

  g_debug ("Handling CreateSession");

  handle = g_new0 (AccessHandle, 1);
  handle->impl = g_object_ref (object);
  handle->invocation = invocation;
  handle->request = g_object_ref (request);
  handle->keyfile = g_key_file_ref (keyfile);
  handle->app_id = g_strdup (arg_app_id);
  handle->title = g_strdup (arg_title);
  handle->subtitle = g_strdup (arg_subtitle);
  handle->body = g_strdup (arg_body);

  g_signal_connect (request, "handle-close", G_CALLBACK (handle_close), handle);

  request_export (request, g_dbus_method_invocation_get_connection (invocation));

  if (g_key_file_has_key (keyfile, "backend", "delay", NULL))
    delay = g_key_file_get_integer (keyfile, "backend", "delay", NULL);
  else
    delay = 200;

  g_debug ("delay %d", delay);

  return TRUE;
}

void
input_capture_init (GDBusConnection *bus,
                    const char *object_path)
{
  g_autoptr(GError) error = NULL;
  GDBusInterfaceSkeleton *helper;

  helper = G_DBUS_INTERFACE_SKELETON (xdp_impl_input_capture_skeleton_new ());

  g_signal_connect (helper, "handle-create-session", G_CALLBACK (handle_create_session), NULL);

  if (!g_dbus_interface_skeleton_export (helper, bus, object_path, &error))
    {
      g_error ("Failed to export %s skeleton: %s\n",
               g_dbus_interface_skeleton_get_info (helper)->name,
               error->message);
      exit (1);
    }

  g_debug ("providing %s at %s", g_dbus_interface_skeleton_get_info (helper)->name, object_path);
}

