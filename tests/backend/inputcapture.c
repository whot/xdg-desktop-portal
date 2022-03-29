#include <config.h>
#include <stdio.h>
#include <stdlib.h>

#include <gio/gio.h>

#include "src/xdp-impl-dbus.h"

#include "request.h"
#include "inputcapture.h"


typedef struct {
  XdpImplInputCapture *impl;
  GDBusMethodInvocation *invocation;
  Request *request;
  GKeyFile *keyfile;
  char *app_id;
  char *id;
  char *action;
  GVariant *options;
  guint timeout;
} InputCaptureHandle;

static void
input_capture_handle_free (InputCaptureHandle *handle)
{
    g_object_unref (handle->impl);
    g_object_unref (handle->request);
    g_key_file_unref (handle->keyfile);
    g_free (handle->app_id);

    if (handle->timeout)
        g_source_remove (handle->timeout);

    g_free (handle);
}

static gboolean
send_response_create_session (gpointer data)
{
    InputCaptureHandle *handle = data;
    GVariantBuilder opt_builder;
    int response;
    int allowed_caps;

    if (g_key_file_get_boolean (handle->keyfile, "backend", "expect-close", NULL))
        g_assert_not_reached();

    g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);

    response = g_key_file_get_integer (handle->keyfile, "backend", "response", NULL);
    allowed_caps = g_key_file_get_integer (handle->keyfile, "inputcapture", "capabilities", NULL);

    g_variant_builder_add(&opt_builder, "{sv}", "capabilities", g_variant_new_uint32(allowed_caps));

    if (handle->request->exported)
        request_unexport (handle->request);

    g_debug ("%s %d", __func__, response);

    xdp_impl_input_capture_complete_create_session (handle->impl,
                                                    handle->invocation,
                                                    response,
                                                    g_variant_builder_end (&opt_builder));

    handle->timeout = 0;

    input_capture_handle_free (handle);

    return G_SOURCE_REMOVE;
}

static gboolean
handle_close_create_session (XdpImplRequest *object,
                             GDBusMethodInvocation *invocation,
                             InputCaptureHandle *handle)
{
    GVariantBuilder opt_builder;

    g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);
    g_debug ("InputCapture handling Close");
    xdp_impl_input_capture_complete_create_session (handle->impl,
                                                    handle->invocation,
                                                    2, g_variant_builder_end (&opt_builder));

    input_capture_handle_free (handle);

    return FALSE;
}

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
  const char *sender = NULL;
  g_autoptr(GKeyFile) keyfile = get_config_file ();
  g_autoptr(Request) request = NULL;
  InputCaptureHandle *handle = NULL;
  int delay;

  sender = g_dbus_method_invocation_get_sender (invocation);
  request = request_new (sender, arg_app_id, arg_handle);

  g_debug ("Handling CreateSession");

  handle = g_new0 (InputCaptureHandle, 1);
  handle->impl = g_object_ref (object);
  handle->invocation = invocation;
  handle->request = g_object_ref (request);
  handle->keyfile = g_key_file_ref (keyfile);
  handle->app_id = g_strdup (arg_app_id);

  g_signal_connect (request, "handle-close", G_CALLBACK (handle_close_create_session), handle);

  request_export (request, g_dbus_method_invocation_get_connection (invocation));

  if (g_key_file_has_key (keyfile, "backend", "delay", NULL))
    delay = g_key_file_get_integer (keyfile, "backend", "delay", NULL);
  else
    delay = 200;

  g_debug ("delay %d", delay);

  if (delay == 0)
      send_response_create_session (handle);
  else
      handle->timeout = g_timeout_add (delay, send_response_create_session, handle);

  return TRUE;
}

static gboolean
send_response_get_zones (gpointer data)
{
    InputCaptureHandle *handle = data;
    GVariantBuilder opt_builder;
    GVariantBuilder array_builder;
    int response;
    guint serial;
    g_autofree gint *zones;
    gsize nzones;

    if (g_key_file_get_boolean (handle->keyfile, "backend", "expect-close", NULL))
        g_assert_not_reached();

    g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);

    response = g_key_file_get_integer (handle->keyfile, "backend", "response", NULL);
    serial = g_key_file_get_integer (handle->keyfile, "inputcapture", "serial", NULL);
    zones = g_key_file_get_integer_list (handle->keyfile, "inputcapture", "zones", &nzones, NULL);
    if (!zones) {
        zones = g_new(int, 4);
        nzones = 4;
        zones[0] = 1920;
        zones[1] = 1080;
        zones[2] = 0;
        zones[4] = 0;
    } else {
        g_assert (nzones % 4 == 0);
    }

    g_variant_builder_add (&opt_builder, "{sv}", "serial", g_variant_new_uint32(serial));
    g_variant_builder_init (&array_builder, G_VARIANT_TYPE_ARRAY);
    for (gsize i = 0; i < nzones; i += 4)
        g_variant_builder_add (&array_builder, "(uuii)", zones[i], zones[i+1], zones[i+2], zones[i+3]);

    g_variant_builder_add(&opt_builder, "{sv}", "zones", g_variant_builder_end (&array_builder));

    if (handle->request->exported)
        request_unexport (handle->request);

    g_debug ("%s %d", __func__, response);

    xdp_impl_input_capture_complete_get_zones (handle->impl,
                                               handle->invocation,
                                               response,
                                               g_variant_builder_end (&opt_builder));

    handle->timeout = 0;

    input_capture_handle_free (handle);

    return G_SOURCE_REMOVE;
}


static gboolean
handle_close_get_zones (XdpImplRequest *object,
                        GDBusMethodInvocation *invocation,
                        InputCaptureHandle *handle)
{
    GVariantBuilder opt_builder;

    g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);
    g_debug ("InputCapture handling Close");
    xdp_impl_input_capture_complete_get_zones (handle->impl,
                                               handle->invocation,
                                               2, g_variant_builder_end (&opt_builder));

    input_capture_handle_free (handle);

    return FALSE;
}


static gboolean
handle_get_zones (XdpImplInputCapture *object,
                  GDBusMethodInvocation *invocation,
                  const gchar *arg_handle,
                  const gchar *arg_session_handle,
                  const gchar *arg_app_id,
                  GVariant *arg_options)
{
  const char *sender = NULL;
  g_autoptr(GKeyFile) keyfile = get_config_file ();
  g_autoptr(Request) request = NULL;
  InputCaptureHandle *handle = NULL;
  int delay;

  sender = g_dbus_method_invocation_get_sender (invocation);
  request = request_new (sender, arg_app_id, arg_handle);

  g_debug ("Handling GetZones");

  handle = g_new0 (InputCaptureHandle, 1);
  handle->impl = g_object_ref (object);
  handle->invocation = invocation;
  handle->request = g_object_ref (request);
  handle->keyfile = g_key_file_ref (keyfile);
  handle->app_id = g_strdup (arg_app_id);

  g_signal_connect (request, "handle-close", G_CALLBACK (handle_close_get_zones), handle);

  request_export (request, g_dbus_method_invocation_get_connection (invocation));

  if (g_key_file_has_key (keyfile, "backend", "delay", NULL))
    delay = g_key_file_get_integer (keyfile, "backend", "delay", NULL);
  else
    delay = 200;

  g_debug ("delay %d", delay);

  if (delay == 0)
      send_response_get_zones (handle);
  else
      handle->timeout = g_timeout_add (delay, send_response_get_zones, handle);

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
  g_signal_connect (helper, "handle-get-zones", G_CALLBACK (handle_get_zones), NULL);

  if (!g_dbus_interface_skeleton_export (helper, bus, object_path, &error))
    {
      g_error ("Failed to export %s skeleton: %s\n",
               g_dbus_interface_skeleton_get_info (helper)->name,
               error->message);
      exit (1);
    }

  g_debug ("providing %s at %s", g_dbus_interface_skeleton_get_info (helper)->name, object_path);
}

