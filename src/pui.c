/*
 * pui.c
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

#include <glib/gi18n-lib.h>
#include <libhildondesktop/hd-plugin-module.h>

#include "pui-master.h"
#include "pui-module.h"

static void
term_handler(int signum)
{
  gtk_main_quit();
}

int
main(int argc, char **argv)
{
  TpDBusDaemon *dbus_daemon;
  PuiMaster *master;
  GtkWidget *dialog;
  GtkWidget *status_area_hbox;
  GtkWidget *status_area_label;
  GtkWidget *status_area;
  GtkWidget *menu_item_hbox;
  GtkWidget *menu_item_label;
  GtkWidget *menu_item;
  HDPluginModule *module;

  signal(SIGTERM, term_handler);

  bindtextdomain(GETTEXT_PACKAGE, "/usr/share/locale");
  bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
  textdomain(GETTEXT_PACKAGE);

  hildon_gtk_init(&argc, &argv);
  g_set_application_name(_("pres_ap_feature_name"));

  dbus_daemon = tp_dbus_daemon_dup(NULL);
  master = pui_master_new(dbus_daemon);
  g_object_unref(dbus_daemon);

  dialog = gtk_dialog_new_with_buttons("Presence", NULL, 0,
                                       "Close", GTK_RESPONSE_OK, NULL);

  status_area_hbox = gtk_hbox_new(FALSE, 8);
  status_area_label = gtk_label_new("Status area:");
  gtk_box_pack_start(GTK_BOX(status_area_hbox), status_area_label,
                     FALSE, FALSE, 8);
  gtk_widget_show(status_area_label);

  status_area = gtk_image_new();
  gtk_box_pack_start(GTK_BOX(status_area_hbox), status_area, FALSE, FALSE, 8);
  gtk_widget_show(status_area);

  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), status_area_hbox,
                     FALSE, FALSE, 8);
  gtk_widget_show(status_area_hbox);

  menu_item_hbox = gtk_hbox_new(FALSE, 8);
  menu_item_label = gtk_label_new("Menu item:");
  gtk_box_pack_start(GTK_BOX(menu_item_hbox), menu_item_label, FALSE, FALSE, 8);
  gtk_widget_show(menu_item_label);

  module = hd_plugin_module_new(HILDON_PLUGIN_DIR "/librtcom-presence-ui.so");

  g_type_module_use(G_TYPE_MODULE(module));

  menu_item = g_object_new(PUI_TYPE_MENU_ITEM,
                           "master", master,
                           "status-area", status_area,
                           NULL);
  gtk_box_pack_start(GTK_BOX(menu_item_hbox), menu_item, FALSE, FALSE, 8);
  gtk_widget_show(menu_item);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), menu_item_hbox,
                     FALSE, FALSE, 8);
  gtk_widget_show(menu_item_hbox);

  gtk_widget_show(dialog);
  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
  g_object_unref(master);

  g_type_module_unuse(G_TYPE_MODULE(module));

  return 0;
}
