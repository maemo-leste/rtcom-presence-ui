/*
 * pui-module.c
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
#include <libhildondesktop/libhildondesktop.h>

#include "pui-main-view.h"
#include "pui-master.h"

#include "pui-module.h"

#define PUI_MENU_ITEM(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), \
                              PUI_TYPE_MENU_ITEM, \
                              PuiMenuItem))
#define PUI_MENU_ITEM_CLASS(cls) \
  (G_TYPE_CHECK_CLASS_CAST((cls), \
                           PUI_TYPE_MENU_ITEM, \
                           PuiMenuItemClass))
#define PUI_IS_MENU_ITEM(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), \
                              PUI_TYPE_MENU_ITEM))
#define PUI_IS_MENU_ITEM_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((obj), \
                           PUI_TYPE_MENU_ITEM))
#define PUI_MENU_ITEM_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj), \
                             PUI_TYPE_MENU_ITEM, \
                             PuiMenuItemClass))

struct _PuiMenuItem
{
  HDStatusMenuItem parent;
};

typedef struct _PuiMenuItem PuiMenuItem;

struct _PuiMenuItemClass
{
  HDStatusMenuItemClass parent;
};

typedef struct _PuiMenuItemClass PuiMenuItemClass;

struct _PuiMenuItemPrivate
{
  PuiMaster *master;
  GtkListStore *model;
  GtkWidget *image;
  GtkWidget *status_label;
  GdkPixbuf *status_area_icon;
  GdkPixbuf *icon;
  guint update_icons_id;
  GtkWidget *status_area;
  gboolean is_connecting : 1;
  gboolean show_presence_icon : 1;
};

typedef struct _PuiMenuItemPrivate PuiMenuItemPrivate;

#define PRIVATE(item) \
  ((PuiMenuItemPrivate *) \
   pui_menu_item_get_instance_private((PuiMenuItem *)(item)))

HD_DEFINE_PLUGIN_MODULE_WITH_PRIVATE(
  PuiMenuItem,
  pui_menu_item,
  HD_TYPE_STATUS_MENU_ITEM
)

enum
{
  PROP_MASTER = 1,
  PROP_STATUS_AREA
};

static void
on_row_deleted(PuiMenuItem *item)
{
  PuiMenuItemPrivate *priv = PRIVATE(item);

  if (gtk_tree_model_iter_n_children(GTK_TREE_MODEL(priv->model), 0) <= 1)
    gtk_widget_hide(GTK_WIDGET(item));
  else
    gtk_widget_show(GTK_WIDGET(item));
}

static TpConnectionPresenceType
get_profile_presence_type(TpConnectionPresenceType presence_type,
                          PuiProfile *profile)
{
  if ((presence_type == TP_CONNECTION_PRESENCE_TYPE_AVAILABLE) &&
      profile && !g_strcmp0(profile->default_presence, "busy"))
  {
    presence_type = TP_CONNECTION_PRESENCE_TYPE_AWAY;
  }

  return presence_type;
}

static const char *
get_status_icon_name(PuiMenuItem *item, TpConnectionPresenceType presence_type,
                     gboolean error)
{
  static const char *presence_icons[][2] =
  {
    { "general_presence_online", "statusarea_presence_online_error" },
    { "general_presence_busy", "statusarea_presence_busy_error" }
  };

  if ((presence_type == TP_CONNECTION_PRESENCE_TYPE_UNSET) ||
      (presence_type == TP_CONNECTION_PRESENCE_TYPE_OFFLINE))
    return NULL;

  return presence_icons
         [presence_type == TP_CONNECTION_PRESENCE_TYPE_AVAILABLE ? 0 : 1]
         [error ? 1 : 0];
}

static void
update_status_area_icon(PuiMenuItem *item, const char *icon_name)
{
  PuiMenuItemPrivate *priv = PRIVATE(item);
  GdkPixbuf *icon = pui_master_get_icon(priv->master, icon_name,
                                        ICON_SIZE_SMALL);

  if (icon != priv->status_area_icon)
  {
    if (priv->status_area)
      gtk_image_set_from_pixbuf(GTK_IMAGE(priv->status_area), icon);
    else
    {
      hd_status_plugin_item_set_status_area_icon(HD_STATUS_PLUGIN_ITEM(item),
                                                 icon);
    }

    priv->status_area_icon = icon;
  }
}

static void
update_icon(PuiMenuItem *item, const gchar *icon_name)
{
  PuiMenuItemPrivate *priv = PRIVATE(item);
  GdkPixbuf *icon = pui_master_get_icon(priv->master, icon_name,
                                        ICON_SIZE_DEFAULT);

  if (icon != priv->icon)
  {
    gtk_image_set_from_pixbuf(GTK_IMAGE(priv->image), icon);
    priv->icon = icon;
  }
}

static void
pui_menu_item_update_icon(PuiMenuItem *item, PuiProfile *profile, guint status)
{
  const gchar *icon_name;

  g_return_if_fail(profile != NULL);

  if (status & (PUI_MASTER_STATUS_OFFLINE | PUI_MASTER_STATUS_ERROR))
    icon_name = profile->icon_error;
  else
    icon_name = profile->icon;

  update_icon(item, icon_name);
}

static gboolean
update_icons(PuiMenuItem *item)
{
  PuiMenuItemPrivate *priv = PRIVATE(item);
  const gchar *status_icon_name;
  const gchar *icon_name;

  priv->show_presence_icon = !priv->show_presence_icon;

  if (priv->show_presence_icon)
  {
    PuiProfile *profile = pui_master_get_active_profile(priv->master);
    TpConnectionPresenceType presence_type;

    pui_master_get_global_presence(priv->master, &presence_type, NULL, NULL);
    presence_type = get_profile_presence_type(presence_type, profile);
    status_icon_name = get_status_icon_name(item, presence_type, 0);

    if (profile)
      icon_name = profile->icon;
    else
      icon_name = NULL;
  }
  else
  {
    status_icon_name = "general_presence_offline";
    icon_name = "general_presence_offline";
  }

  update_icon(item, icon_name);
  update_status_area_icon(item, status_icon_name);

  return TRUE;
}

static void
set_status_message(PuiMaster *master, PuiProfile *profile, PuiMenuItem *item)
{
  PuiMenuItemPrivate *priv = PRIVATE(item);
  const gchar *status_message = NULL;
  gboolean no_sip_in_profile;

  pui_master_scan_profile(master, profile, &no_sip_in_profile, NULL);

  if (no_sip_in_profile)
    pui_master_get_global_presence(master, NULL, &status_message, NULL);

  gtk_label_set_text(GTK_LABEL(priv->status_label), status_message);
}

static void
on_presence_changed(PuiMenuItem *item, TpConnectionPresenceType presence_type,
                    const gchar *status_message, guint status,
                    PuiMaster *master)
{
  PuiMenuItemPrivate *priv = PRIVATE(item);

  if (status & PUI_MASTER_STATUS_CONNECTING)
  {
    if (!priv->update_icons_id)
    {
      priv->is_connecting = TRUE;
      priv->show_presence_icon = TRUE;

      if (pui_master_get_display_on(priv->master))
      {
        priv->update_icons_id =
          g_timeout_add_seconds(1, (GSourceFunc)update_icons, item);
        update_icons(item);
      }
    }
  }
  else
  {
    PuiProfile *profile;
    TpConnectionPresenceType type;
    const gchar *status_icon_name;

    priv->is_connecting = FALSE;

    if (priv->update_icons_id)
    {
      g_source_remove(priv->update_icons_id);
      priv->update_icons_id = 0;
    }

    profile = pui_master_get_active_profile(priv->master);
    type = get_profile_presence_type(presence_type, profile);
    status_icon_name = get_status_icon_name(
        item, type, !!(status & PUI_MASTER_STATUS_ERROR));
    update_status_area_icon(item, status_icon_name);
    pui_menu_item_update_icon(item, profile, status);
  }

  set_status_message(master, pui_master_get_active_profile(master), item);
}

static void
on_profile_activated(PuiMenuItem *item, PuiProfile *profile, PuiMaster *master)
{
  guint status;

  pui_master_get_global_presence(master, NULL, NULL, &status);
  pui_menu_item_update_icon(item, profile, status);
  set_status_message(master, profile, item);
}

static void
on_profile_changed(PuiMenuItem *item, PuiProfile *profile, PuiMaster *master)
{
  PuiMenuItemPrivate *priv = PRIVATE(item);
  guint status;
  TpConnectionPresenceType type;

  g_return_if_fail(profile != NULL);

  if (pui_master_get_active_profile(master) == profile)
  {
    pui_master_get_global_presence(master, &type, NULL, &status);

    if (!priv->update_icons_id)
    {
      const gchar *icon;

      if (status & (PUI_MASTER_STATUS_OFFLINE | PUI_MASTER_STATUS_ERROR))
        icon = profile->icon_error;
      else
        icon = profile->icon;

      update_icon(item, icon);
    }

    set_status_message(master, profile, item);
  }
}

static void
on_screen_state_changed(PuiMaster *master, gboolean is_on, PuiMenuItem *item)
{
  PuiMenuItemPrivate *priv = PRIVATE(item);

  if (!priv->is_connecting)
    return;

  if (is_on)
  {
    if (!priv->update_icons_id)
    {
      priv->update_icons_id =
        g_timeout_add_seconds(1, (GSourceFunc)update_icons, item);
      update_icons(item);
    }
  }
  else if (priv->update_icons_id)
  {
    g_source_remove(priv->update_icons_id);
    priv->update_icons_id = 0;
  }
}

static void
pui_menu_item_constructed(GObject *object)
{
  PuiMenuItem *item = PUI_MENU_ITEM(object);
  PuiMenuItemPrivate *priv = PRIVATE(item);
  GObjectClass *object_class = G_OBJECT_CLASS(pui_menu_item_parent_class);
  guint status;
  const gchar *status_message;
  TpConnectionPresenceType presence_type;

  if (!priv->master)
  {
    GError *error = NULL;
    DBusGConnection *connection;

    connection = hd_status_plugin_item_get_dbus_g_connection(
        HD_STATUS_PLUGIN_ITEM(item), DBUS_BUS_SESSION, &error);

    if (error)
    {
      g_warning("Failed to open connection to D-Bus: %s", error->message);
      g_error_free(error);
    }
    else
    {
      TpDBusDaemon *dbus_daemon = tp_dbus_daemon_new(connection);

      tp_debug_set_flags(g_getenv("PUI_TP_DEBUG"));

      priv->master = pui_master_new(dbus_daemon);
      g_object_unref(dbus_daemon);
      dbus_g_connection_unref(connection);
    }
  }

  if (priv->master)
  {
    pui_master_get_global_presence(priv->master, &presence_type,
                                   &status_message, &status);
    on_presence_changed(item, presence_type, status_message, status,
                        priv->master);
    g_signal_connect_swapped(priv->master, "presence-changed",
                             G_CALLBACK(on_presence_changed), item);
    g_signal_connect_swapped(priv->master, "profile-activated",
                             G_CALLBACK(on_profile_activated), item);
    g_signal_connect_swapped(priv->master, "profile-changed",
                             G_CALLBACK(on_profile_changed), item);

    priv->model = pui_master_get_model(priv->master);
    g_object_ref(priv->model);
    on_row_deleted(item);
    g_signal_connect_swapped(priv->model, "row-deleted",
                             G_CALLBACK(on_row_deleted), item);
    g_signal_connect_swapped(priv->model, "row-inserted",
                             G_CALLBACK(gtk_widget_show), item);

    g_signal_connect(priv->master, "screen-state-changed",
                     G_CALLBACK(on_screen_state_changed), item);
  }

  if (object_class->constructed)
    object_class->constructed(object);
}

static void
pui_menu_item_dispose(GObject *object)
{
  PuiMenuItemPrivate *priv = PRIVATE(object);

  if (priv->update_icons_id)
  {
    g_source_remove(priv->update_icons_id);
    priv->update_icons_id = 0;
  }

  if (priv->model)
  {
    g_signal_handlers_disconnect_matched(
      priv->model, G_SIGNAL_MATCH_DATA | G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
      on_row_deleted, object);
    g_signal_handlers_disconnect_matched(
      priv->model, G_SIGNAL_MATCH_DATA | G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
      gtk_widget_show, object);
    g_object_unref(priv->model);
    priv->model = NULL;
  }

  if (priv->master)
  {
    g_signal_handlers_disconnect_matched(
      priv->master, G_SIGNAL_MATCH_DATA | G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
      on_presence_changed, object);
    g_signal_handlers_disconnect_matched(
      priv->master, G_SIGNAL_MATCH_DATA | G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
      on_profile_activated, object);
    g_signal_handlers_disconnect_matched(
      priv->master, G_SIGNAL_MATCH_DATA | G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
      on_profile_changed, object);
    g_signal_handlers_disconnect_matched(
      priv->master, G_SIGNAL_MATCH_DATA | G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
      on_screen_state_changed, object);
    g_object_unref(priv->master);
    priv->master = NULL;
  }

  if (priv->status_area)
  {
    g_object_unref(priv->status_area);
    priv->status_area = NULL;
  }

  G_OBJECT_CLASS(pui_menu_item_parent_class)->dispose(object);
}

static void
pui_menu_item_set_property(GObject *object, guint property_id,
                           const GValue *value, GParamSpec *pspec)
{
  PuiMenuItemPrivate *priv = PRIVATE(object);

  switch (property_id)
  {
    case PROP_MASTER:
    {
      g_assert(priv->master == NULL);
      priv->master = g_value_dup_object(value);
      break;
    }
    case PROP_STATUS_AREA:
    {
      g_assert(priv->status_area == NULL);
      priv->status_area = g_value_dup_object(value);
      break;
    }
    default:
    {
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
      break;
    }
  }
}

static void
pui_menu_item_class_init(PuiMenuItemClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->constructed = pui_menu_item_constructed;
  object_class->dispose = pui_menu_item_dispose;
  object_class->set_property = pui_menu_item_set_property;

  g_object_class_install_property(
    object_class, PROP_MASTER,
    g_param_spec_object(
      "master", "master", "master",
      PUI_TYPE_MASTER,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));
  g_object_class_install_property(
    object_class, PROP_STATUS_AREA,
    g_param_spec_object(
      "status-area", "StatusArea", "StatusArea",
      GTK_TYPE_IMAGE,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));
}

static void
pui_menu_item_class_finalize(PuiMenuItemClass *klass)
{}

static void
button_realize_cb(GtkWidget *button, PuiMenuItem *item)
{
  PuiMenuItemPrivate *priv = PRIVATE(item);
  gint spacing = 0;

  gtk_widget_style_get(button, "image-spacing", &spacing, NULL);
  gtk_box_set_spacing(GTK_BOX(priv->image->parent), spacing);
}

static void
button_clicked_cb(GtkWidget *button, PuiMenuItem *item)
{
  PuiMenuItemPrivate *priv = PRIVATE(item);
  PuiMainView *view = pui_main_view_new(priv->master);

  pui_main_view_run(view);

  gtk_widget_destroy(GTK_WIDGET(view));
}

static void
pui_menu_item_init(PuiMenuItem *item)
{
  PuiMenuItemPrivate *priv = PRIVATE(item);
  GtkWidget *presence_label;
  GtkWidget *hbox;
  GtkWidget *vbox;
  GtkWidget *button;
  GtkWidget *align;

  priv->update_icons_id = 0;
  button = hildon_button_new(HILDON_SIZE_FINGER_HEIGHT,
                             HILDON_BUTTON_ARRANGEMENT_VERTICAL);
  gtk_container_add(GTK_CONTAINER(item), button);
  gtk_widget_show(button);
  align = gtk_alignment_new(0.0, 0.5, 1.0, 0.0);
  gtk_container_add(GTK_CONTAINER(button), align);
  gtk_widget_show(align);

  hbox = gtk_hbox_new(FALSE, 0);
  priv->image = gtk_image_new();
  gtk_box_pack_start(GTK_BOX(hbox), priv->image, FALSE, FALSE, 0);
  gtk_widget_show(priv->image);

  vbox = gtk_vbox_new(FALSE, 0);
  presence_label = g_object_new(GTK_TYPE_LABEL,
                                "label", _("pres_smplugin_ti_presence_title"),
                                "xalign", 0.0,
                                "yalign", 1.0,
                                NULL);

  gtk_box_pack_start(GTK_BOX(vbox), presence_label, FALSE, FALSE, 0);

  priv->status_label = g_object_new(GTK_TYPE_LABEL,
                                    "xalign", 0.0,
                                    "yalign", 0.0,
                                    NULL);
  hildon_helper_set_logical_font(priv->status_label, "SmallSystemFont");
  hildon_helper_set_logical_color(priv->status_label, GTK_RC_FG,
                                  GTK_STATE_NORMAL, "ActiveTextColor");
  hildon_helper_set_logical_color(priv->status_label, GTK_RC_FG,
                                  GTK_STATE_PRELIGHT, "ActiveTextColor");
  gtk_widget_set_name(presence_label, "hildon-button-title");
  gtk_widget_set_name(priv->status_label, "hildon-button-value");
  gtk_box_pack_start(GTK_BOX(vbox), priv->status_label, FALSE, FALSE, 0);

  gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);
  gtk_widget_show_all(hbox);
  gtk_container_add(GTK_CONTAINER(align), hbox);
  gtk_widget_show(hbox);
  gtk_widget_show(GTK_WIDGET(item));

  g_signal_connect(button, "realize",
                   G_CALLBACK(button_realize_cb), item);
  g_signal_connect(button, "clicked",
                   G_CALLBACK(button_clicked_cb), item);
}
