/*
 * Copyright © 2018-2019 Red Hat, Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include <glib/gi18n.h>
#include <gio/gunixfdlist.h>
#include <gio/gdesktopappinfo.h>
#include <gio/gunixsocketaddress.h>
#include <stdio.h>

#include "device.h"
#include "request.h"
#include "permissions.h"
#include "xdp-dbus.h"
#include "xdp-impl-dbus.h"
#include "xdp-utils.h"

static XdpImplLockdown *lockdown;

typedef struct _EmulatedInput EmulatedInput;
typedef struct _EmulatedInputClass EmulatedInputClass;

struct _EmulatedInput
{
  XdpEmulatedInputSkeleton parent_instance;
};

struct _EmulatedInputClass
{
  XdpEmulatedInputSkeletonClass parent_class;
};

static EmulatedInput *ei;

GType emulated_input_get_type (void);
static void ei_iface_init (XdpEmulatedInputIface *iface);

G_DEFINE_TYPE_WITH_CODE (EmulatedInput, ei, XDP_TYPE_EMULATED_INPUT_SKELETON,
                         G_IMPLEMENT_INTERFACE (XDP_TYPE_EMULATED_INPUT,
                                                ei_iface_init))

static void
handle_emulate_input_in_thread_func (GTask *task,
                                     gpointer source_object,
                                     gpointer task_data,
                                     GCancellable *cancellable)
{
  Request *request = (Request *)task_data;

  REQUEST_AUTOLOCK (request);

  if (request->exported)
    {
      GVariantBuilder results;

      g_variant_builder_init (&results, G_VARIANT_TYPE_VARDICT);

      xdp_request_emit_response (XDP_REQUEST (request),
				 XDG_DESKTOP_PORTAL_RESPONSE_SUCCESS,
                                 g_variant_builder_end (&results));
      request_unexport (request);
    }
}

static gboolean
handle_emulate_input (XdpEmulatedInput *object,
                      GDBusMethodInvocation *invocation,
                      GVariant *arg_options)
{
  Request *request = request_from_invocation (invocation);
  const char *app_id;
  g_autoptr(GTask) task = NULL;

  if (xdp_impl_lockdown_get_disable_emulated_input (lockdown))
    {
      g_debug ("Ei access disabled");
      g_dbus_method_invocation_return_error (invocation,
                                             XDG_DESKTOP_PORTAL_ERROR,
                                             XDG_DESKTOP_PORTAL_ERROR_NOT_ALLOWED,
                                             "Ei access disabled");
      return TRUE;
    }

  REQUEST_AUTOLOCK (request);

  app_id = xdp_app_info_get_id (request->app_info);

  g_object_set_data_full (G_OBJECT (request), "app-id", g_strdup (app_id), g_free);

  request_export (request, g_dbus_method_invocation_get_connection (invocation));

  xdp_emulated_input_complete_emulate_input (object, invocation, request->id);

  task = g_task_new (object, NULL, NULL, NULL);
  g_task_set_task_data (task, g_object_ref (request), g_object_unref);
  g_task_run_in_thread (task, handle_emulate_input_in_thread_func);

  return TRUE;
}

static int
connect_to_eis (const char *app_id,
		GError **error)
{
  g_autofree char *socketpath = NULL;
  g_autoptr(GSocketAddress) addr = NULL;
  g_autoptr(GSocketClient) client = NULL;
  g_autoptr(GSocketConnection) connection = NULL;
  GSocket *socket = NULL;

  socketpath = g_strdup_printf ("%s/eis-0", g_get_user_runtime_dir ());
  addr = g_unix_socket_address_new (socketpath);
  client = g_socket_client_new ();
  connection = g_socket_client_connect (client,
                                        G_SOCKET_CONNECTABLE (addr),
                                        NULL, error);
  if (!connection)
    return -1;

  socket = g_socket_connection_get_socket (connection);
  if (!socket)
    return -1;

  return dup(g_socket_get_fd (socket));
}

static gboolean
handle_connect (XdpEmulatedInput *object,
                GDBusMethodInvocation *invocation,
                GUnixFDList *in_fd_list,
                GVariant *arg_options)
{
  g_autoptr(XdpAppInfo) app_info = NULL;
  const char *app_id;
  g_autoptr(GUnixFDList) out_fd_list = NULL;
  g_autoptr(GError) error = NULL;
  int fd_id;
  int eisfd;

  if (xdp_impl_lockdown_get_disable_emulated_input (lockdown))
    {
      g_debug ("Ei access disabled");
      g_dbus_method_invocation_return_error (invocation,
                                             XDG_DESKTOP_PORTAL_ERROR,
                                             XDG_DESKTOP_PORTAL_ERROR_NOT_ALLOWED,
                                             "Ei access disabled");
      return TRUE;
    }

  app_info = xdp_invocation_lookup_app_info_sync (invocation, NULL, &error);
  app_id = xdp_app_info_get_id (app_info);

  eisfd = connect_to_eis (app_id, &error);
  if (eisfd == -1)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             XDG_DESKTOP_PORTAL_ERROR,
                                             XDG_DESKTOP_PORTAL_ERROR_FAILED,
                                             "Failed to connect to EIS: %s",
                                             error->message);
      return TRUE;
    }

  out_fd_list = g_unix_fd_list_new ();
  fd_id = g_unix_fd_list_append (out_fd_list, eisfd, &error);
  close (eisfd);

  if (fd_id == -1)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             XDG_DESKTOP_PORTAL_ERROR,
                                             XDG_DESKTOP_PORTAL_ERROR_FAILED,
                                             "Failed to append fd: %s",
                                             error->message);
      return TRUE;
    }

  xdp_emulated_input_complete_connect (object, invocation,
                                       out_fd_list,
                                       g_variant_new_handle (fd_id));
  return TRUE;
}

static void
ei_iface_init (XdpEmulatedInputIface *iface)
{
  iface->handle_emulate_input = handle_emulate_input;
  iface->handle_connect = handle_connect;
}

static void
ei_finalize (GObject *object)
{
  EmulatedInput *ei = (EmulatedInput *)object;

  G_OBJECT_CLASS (ei_parent_class)->finalize (object);
}

static void
ei_init (EmulatedInput *ei)
{
  g_autoptr(GError) error = NULL;

  xdp_emulated_input_set_version (XDP_EMULATED_INPUT (ei), 1);
}

static void
ei_class_init (EmulatedInputClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = ei_finalize;
}

GDBusInterfaceSkeleton *
ei_create (GDBusConnection *connection,
               gpointer lockdown_proxy)
{
  lockdown = lockdown_proxy;

  ei = g_object_new (ei_get_type (), NULL);

  return G_DBUS_INTERFACE_SKELETON (ei);
}
