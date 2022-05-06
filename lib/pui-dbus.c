/*
 * pui-dbus.c
 *
 * Copyright (C) 2022 Ivaylo Dimitrov <ivo.g.dimitrov.75@gmail.com>
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <https://www.gnu.org/licenses/>.
 *
 */
#include "config.h"

#include "pui-main-view.h"

#include "pui-dbus.h"

static PuiMainView *main_view = NULL;

static gboolean
pui_main_view_run_delayed(gpointer user_data)
{
  PuiMainView *main_view = user_data;

  pui_main_view_run(main_view);
  gtk_widget_destroy(GTK_WIDGET(main_view));

  return FALSE;
}

static gboolean
presence_ui_start_up(PuiMaster *master, GError **error)
{
  if (main_view)
    return TRUE;

  main_view = pui_main_view_new(master);

  if (!main_view)
  {
    g_set_error(error, TP_ERROR, TP_ERROR_NOT_AVAILABLE, "%s",
                "Could not create main view");
    return FALSE;
  }

  g_signal_connect(main_view, "destroy",
                   G_CALLBACK(gtk_widget_destroyed), &main_view);
  g_idle_add(pui_main_view_run_delayed, main_view);

  return TRUE;
}

#include "dbus-glib-marshal-presence-ui.h"

void
pui_dbus_init(GType type)
{
  dbus_g_object_type_install_info(type, &dbus_glib_presence_ui_object_info);
}
