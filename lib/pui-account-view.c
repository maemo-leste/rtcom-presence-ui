/*
 * pui-account-view.c
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

#include "pui-master.h"

#include "pui-account-view.h"

struct _PuiAccountViewPrivate
{
  PuiMaster *master;
  GtkTreeViewColumn *presence_icon_col;
  guint timer_id;
  gboolean is_connecting : 1;
  gboolean show_offline_icon : 1;
};

typedef struct _PuiAccountViewPrivate PuiAccountViewPrivate;

#define PRIVATE(view) \
  ((PuiAccountViewPrivate *) \
   pui_account_view_get_instance_private((PuiAccountView *)(view)))

G_DEFINE_TYPE_WITH_PRIVATE(
  PuiAccountView,
  pui_account_view,
  GTK_TYPE_TREE_VIEW
);

enum
{
  PROP_MASTER = 1
};

static gboolean
refresh_connection_status_cb(PuiAccountView *view)
{
  PuiAccountViewPrivate *priv = PRIVATE(view);
  GtkTreeModel *model = GTK_TREE_MODEL(pui_master_get_model(priv->master));
  GtkTreeIter it;
  gboolean has_connecting_account = FALSE;

  if (gtk_tree_model_get_iter_first(model, &it))
  {
    do
    {
      TpConnectionStatus connection_status;

      gtk_tree_model_get(model, &it,
                         COLUMN_CONNECTION_STATUS, &connection_status,
                         -1);

      if (connection_status == TP_CONNECTION_STATUS_CONNECTING)
      {
        GtkTreePath *path = gtk_tree_model_get_path(model, &it);
        GdkRectangle r;

        gtk_tree_view_get_cell_area(&view->parent,
                                    path,
                                    priv->presence_icon_col,
                                    &r);
        gtk_tree_path_free(path);
        gtk_widget_queue_draw_area(&view->parent.parent.widget, r.x, r.y,
                                   r.width, r.height);
        has_connecting_account = TRUE;
      }
    }
    while (gtk_tree_model_iter_next(model, &it));
  }

  priv->show_offline_icon = !priv->show_offline_icon;

  if (!has_connecting_account)
  {
    priv->timer_id = 0;
    priv->is_connecting = FALSE;
  }

  return has_connecting_account;
}

static void
on_screen_state_changed(PuiMaster *master, gboolean is_on,
                        PuiAccountView *view)
{
  PuiAccountViewPrivate *priv = PRIVATE(view);

  if (priv->is_connecting)
  {
    if (is_on)
    {
      if (!priv->timer_id)
      {
        priv->timer_id = g_timeout_add_seconds(
            1, (GSourceFunc)refresh_connection_status_cb, view);
        priv->show_offline_icon = FALSE;
      }
    }
    else
    {
      if (priv->timer_id)
      {
        g_source_remove(priv->timer_id);
        priv->timer_id = 0;
      }
    }
  }
}

static void
pui_account_view_dispose(GObject *object)
{
  PuiAccountViewPrivate *priv = PRIVATE(object);

  if (priv->timer_id)
  {
    g_source_remove(priv->timer_id);
    priv->timer_id = 0;
  }

  if (priv->master)
  {
    g_signal_handlers_disconnect_matched(
      priv->master, G_SIGNAL_MATCH_DATA | G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
      on_screen_state_changed, object);
    g_object_unref(priv->master);
    priv->master = NULL;
  }

  G_OBJECT_CLASS(pui_account_view_parent_class)->dispose(object);
}

static void
pui_account_view_set_property(GObject *object, guint property_id,
                              const GValue *value, GParamSpec *pspec)
{
  switch (property_id)
  {
    case PROP_MASTER:
    {
      PuiAccountView *view = PUI_ACCOUNT_VIEW(object);
      PuiAccountViewPrivate *priv = PRIVATE(view);

      g_assert(priv->master == NULL);
      priv->master = g_value_dup_object(value);

      gtk_tree_view_set_model(
        GTK_TREE_VIEW(view),
        GTK_TREE_MODEL(pui_master_get_model(priv->master)));
      g_signal_connect(priv->master, "screen-state-changed",
                       G_CALLBACK(on_screen_state_changed), view);
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
pui_account_view_size_request(GtkWidget *widget, GtkRequisition *requisition)
{
  GTK_WIDGET_CLASS(pui_account_view_parent_class)->size_request(
    widget, requisition);
  requisition->width = 20;
}

static void
pui_account_view_class_init(PuiAccountViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = pui_account_view_dispose;
  object_class->set_property = pui_account_view_set_property;

  GTK_WIDGET_CLASS(klass)->size_request = pui_account_view_size_request;

  g_object_class_install_property(
    object_class, PROP_MASTER,
    g_param_spec_object(
      "master", "master", "master",
      PUI_TYPE_MASTER, G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));
}

static void
account_data_func(GtkTreeViewColumn *tree_column, GtkCellRenderer *cell,
                  GtkTreeModel *tree_model, GtkTreeIter *iter, gpointer data)
{
  PuiAccountView *view = data;
  TpConnectionStatusReason status_reason;
  gchar *status_message;
  TpAccount *account;

  gtk_tree_model_get(tree_model, iter,
                     COLUMN_ACCOUNT, &account,
                     COLUMN_STATUS_MESSAGE, &status_message,
                     COLUMN_STATUS_REASON, &status_reason,
                     -1);

  if (account)
  {
    PuiAccountViewPrivate *priv = PRIVATE(view);
    const gchar *markup =
      pui_master_get_account_display_name(priv->master, account);
    gchar *s = NULL;

    if (status_message)
    {
      GtkStyle *style = gtk_widget_get_style(
          gtk_tree_view_column_get_tree_view(tree_column));
      const char *color_name;
      gchar *fgcolor;

      if (status_reason == 'r')
        color_name = "SecondaryTextColor";
      else
        color_name = "AttentionColor";

      if (style)
      {
        GdkColor color;

        if (!gtk_style_lookup_color(style, color_name, &color))
        {
          color.green = 0xFFFF;
          color.red = 0xFFFF;
          color.blue = 0xFFFF;
        }

        fgcolor = g_strdup_printf(
            "foreground=\"#%02x%02x%02x\"",
            color.red >> 8, color.green >> 8, color.blue >> 8);
      }
      else
        fgcolor = g_strdup("");

      s = g_strdup_printf("%s\n<span %s size=\"x-small\">%s</span>",
                          markup, fgcolor, status_message);
      g_free(status_message);
      g_free(fgcolor);
      markup = s;
    }

    g_object_set(cell, "markup", markup, NULL);
    g_object_unref(account);
    g_free(s);
  }
  else
    g_object_set(cell, "text", _("pres_fi_accounts"), NULL);
}

static void
presence_data_func(GtkTreeViewColumn *tree_column, GtkCellRenderer *cell,
                   GtkTreeModel *tree_model, GtkTreeIter *it, gpointer data)
{
  PuiAccountView *view = data;
  PuiAccountViewPrivate *priv = PRIVATE(data);
  GdkPixbuf *presence_icon;
  TpConnectionStatus connection_status;

  gtk_tree_model_get(tree_model, it,
                     COLUMN_CONNECTION_STATUS, &connection_status,
                     COLUMN_PRESENCE_ICON, &presence_icon,
                     -1);

  if (presence_icon)
  {
    if (connection_status == TP_CONNECTION_STATUS_CONNECTING)
    {
      priv->is_connecting = TRUE;

      if (pui_master_get_display_on(priv->master))
      {
        if (!priv->timer_id)
        {
          priv->timer_id = g_timeout_add_seconds(
              1, (GSourceFunc)refresh_connection_status_cb, view);
          priv->show_offline_icon = FALSE;
          priv->timer_id = priv->timer_id;
        }
      }

      if (priv->show_offline_icon)
      {
        g_object_unref(presence_icon);
        presence_icon = pui_master_get_icon(priv->master,
                                            "general_presence_offline",
                                            ICON_SIZE_MID);
        g_object_ref(presence_icon);
      }
    }

    g_object_set(cell, "pixbuf", presence_icon, NULL);
    g_object_unref(presence_icon);
  }
  else
    g_object_set(cell, "pixbuf", NULL, NULL);
}

static void
pui_account_view_init(PuiAccountView *view)
{
  PuiAccountViewPrivate *priv = PRIVATE(view);
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *col;

  gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),
                              GTK_SELECTION_NONE);

  col = g_object_new(GTK_TYPE_TREE_VIEW_COLUMN,
                     "spacing", 8,
                     "expand", TRUE,
                     NULL);
  renderer = g_object_new(GTK_TYPE_CELL_RENDERER_TEXT, NULL);
  gtk_tree_view_column_pack_start(col, renderer, TRUE);
  gtk_tree_view_column_set_cell_data_func(col, renderer, account_data_func,
                                          view, NULL);
  gtk_tree_view_column_set_sizing(col, GTK_TREE_VIEW_COLUMN_FIXED);
  gtk_tree_view_column_set_fixed_width(col, TRUE);
  gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

  col = g_object_new(GTK_TYPE_TREE_VIEW_COLUMN, NULL);
  renderer = g_object_new(GTK_TYPE_CELL_RENDERER_PIXBUF,
                          "stock-size", HILDON_ICON_SIZE_SMALL,
                          NULL);
  gtk_tree_view_column_pack_start(col, renderer, TRUE);
  gtk_tree_view_column_add_attribute(col, renderer, "pixbuf",
                                     COLUMN_SERVICE_ICON);
  gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

  col = g_object_new(GTK_TYPE_TREE_VIEW_COLUMN, NULL);

  renderer = g_object_new(GTK_TYPE_CELL_RENDERER_PIXBUF,
                          "stock-size", HILDON_ICON_SIZE_SMALL,
                          NULL);
  gtk_tree_view_column_pack_start(col, renderer, TRUE);
  gtk_tree_view_column_set_sizing(col, GTK_TREE_VIEW_COLUMN_FIXED);
  gtk_tree_view_column_set_fixed_width(col, 34);
  gtk_tree_view_column_set_cell_data_func(col, renderer, presence_data_func,
                                          view, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

  priv->presence_icon_col = col;
}
