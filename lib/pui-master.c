/*
 * pui-master.c
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

#include <canberra.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <glib/gi18n-lib.h>
#include <libprofile.h>
#include <mce/dbus-names.h>

#include <time.h>

#include "pui-dbus.h"
#include "pui-marshal.h"

#include "pui-master.h"

#define PUI_PROFILE_HEADER "Profile "
#define PUI_ACCOUNT_HEADER "Account-"

struct _PuiMasterPrivate
{
  TpAccountManager *manager;
  GtkWidget *parent;
  gchar *config_filename;
  GKeyFile *config;
  GtkListStore *list_store;
  guint presence_supported_count;
  GList *profiles;
  PuiProfile *active_profile;
  time_t profile_change_time;
  time_t connected_time;
  time_t disconnected_time;
  gchar *presence_message;
  gchar *status_message;
  const gchar *default_presence_message;
  int flags;
  TpConnectionPresenceType global_presence_type;
  guint global_status;
  GHashTable *icons_default;
  GHashTable *icons_mid;
  GHashTable *icons_small;
  GHashTable *disconnected_accounts;
  PuiLocation *location;
  ca_context *ca_ctx;
  guint compute_global_presence_id;
  guint set_presence_id;
  gboolean disposed;
  DBusGProxy *mce_proxy;
  gboolean display_on;
  gboolean has_disconnected_account;
  GHashTable *connection_managers;
  time_t last_info_time;
};

typedef struct _PuiMasterPrivate PuiMasterPrivate;

#define PRIVATE(self) \
  ((PuiMasterPrivate *) \
   pui_master_get_instance_private((PuiMaster *)(self)))

G_DEFINE_TYPE_WITH_PRIVATE(
  PuiMaster,
  pui_master,
  G_TYPE_OBJECT
);

enum
{
  PRESENCE_CHANGED,
  PROFILE_CREATED,
  PROFILE_CHANGED,
  PROFILE_DELETED,
  PROFILE_ACTIVATED,
  AVATAR_CHANGED,
  PRESENCE_SUPPORT,
  SCREEN_STATE_CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

enum
{
  PROP_DBUS_DAEMON = 1
};

static PuiProfile default_profiles[] =
{
  {
    "pres_fi_status_online",
    "general_presence_online",
    "statusarea_presence_online_error",
    TRUE,
    NULL,
    "available"
  },
  {
    "pres_fi_status_busy",
    "general_presence_busy",
    "statusarea_presence_busy_error",
    TRUE,
    NULL,
    "busy"
  },
  {
    "pres_fi_status_offline",
    "general_presence_offline",
    "general_presence_offline",
    TRUE,
    NULL,
    "offline"
  }
};

static gboolean
tp_account_is_not_sip(TpAccount *account)
{
  const gchar *protocol_name = tp_account_get_protocol_name(account);

  g_return_val_if_fail(protocol_name, TRUE);

  return strcmp(protocol_name, "sip") ? TRUE : FALSE;
}

static gboolean
account_get_by_id(PuiMaster *master, const char *account_id, GtkTreeIter *iter)
{
  PuiMasterPrivate *priv = PRIVATE(master);
  GtkTreeIter it;

  if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(priv->list_store), &it))
  {
    do
    {
      TpAccount *account;

      gtk_tree_model_get(GTK_TREE_MODEL(priv->list_store), &it,
                         COLUMN_ACCOUNT, &account,
                         -1);

      if (account)
      {
        if (!strcmp(tp_account_get_path_suffix(account), account_id))
        {
          if (iter)
            *iter = it;

          g_object_unref(account);
          return TRUE;
        }

        g_object_unref(account);
      }
    }
    while (gtk_tree_model_iter_next(GTK_TREE_MODEL(priv->list_store), &it));
  }

  return FALSE;
}

static gboolean
account_can_change_presence(PuiMaster *master, TpAccount *account)
{
  TpProtocol *protocol = pui_master_get_account_protocol(master, account);
  GList *presences, *l;
  gboolean rv = FALSE;

  g_return_val_if_fail(protocol, FALSE);

  if (!tp_proxy_has_interface_by_id(protocol,
                                    TP_IFACE_QUARK_PROTOCOL_INTERFACE_PRESENCE))
  {
    return FALSE;
  }

  presences = tp_protocol_dup_presence_statuses(protocol);

  /* assume we can if list is empty */
  if (!presences)
    return TRUE;

  for (l = presences; l; l = l->next)
  {
    TpConnectionPresenceType type =
      tp_presence_status_spec_get_presence_type(l->data);

    if ((type == TP_CONNECTION_PRESENCE_TYPE_OFFLINE) ||
        (type == TP_CONNECTION_PRESENCE_TYPE_AVAILABLE))
    {
      rv = TRUE;
      break;
    }
  }

  g_list_free_full(presences, (GDestroyNotify)tp_presence_status_spec_free);

  return rv;
}

static void
master_presence_changed_cb(PuiMaster *master)
{
  PuiMasterPrivate *priv = PRIVATE(master);

  if (!(priv->global_status & PUI_MASTER_STATUS_CONNECTED) ||
      (pui_master_get_location_level(master) == PUI_LOCATION_LEVEL_NONE))
  {
    pui_location_stop(priv->location);
  }
  else
    pui_location_start(priv->location);
}

static void
list_store_enable_sort(GtkTreeSortable *sortable, gboolean enable)
{
  gint id;

  if (enable)
    id = GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID;
  else
    id = GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID;

  gtk_tree_sortable_set_sort_column_id(sortable, id, GTK_SORT_ASCENDING);
}

static const char *
get_presence_icon(TpConnectionPresenceType type)
{
  if (type == TP_CONNECTION_PRESENCE_TYPE_AVAILABLE)
    return "general_presence_online";

  if (type == TP_CONNECTION_PRESENCE_TYPE_OFFLINE)
    return "general_presence_offline";

  return "general_presence_busy";
}

static gboolean
compute_global_presence_idle(gpointer user_data)
{
  PuiMaster *master = user_data;
  PuiMasterPrivate *priv = PRIVATE(master);
  GtkTreeModel *tree_model = GTK_TREE_MODEL(priv->list_store);
  gchar *presence_icon_name;
  GdkPixbuf *presence_icon;
  TpConnectionStatus account_connection_status;
  const gchar *account_old_presence;
  TpConnectionStatusReason account_status_reason;
  gchar *status_message = NULL;
  GtkTreeIter iter;
  TpConnectionStatus account_old_connection_status;
  TpAccount *account;
  gboolean is_changing_status;
  TpConnectionStatusReason account_old_status_reason;
  TpConnectionPresenceType type;

  list_store_enable_sort(GTK_TREE_SORTABLE(priv->list_store), FALSE);

  priv->global_presence_type = TP_CONNECTION_PRESENCE_TYPE_OFFLINE;
  priv->global_status = PUI_MASTER_STATUS_NONE;

  if (!gtk_tree_model_get_iter_first(tree_model, &iter))
  {
    master_presence_changed_cb(master);
    g_signal_emit(master, signals[PRESENCE_CHANGED], 0,
                  TP_CONNECTION_PRESENCE_TYPE_OFFLINE, priv->status_message, 0);
  }
  else
  {
    int active_accounts_count = 0;
    gchar *message;

    do
    {
      gtk_tree_model_get(
        tree_model, &iter,
        COLUMN_ACCOUNT, &account,
        COLUMN_CONNECTION_STATUS, &account_old_connection_status,
        COLUMN_STATUS_REASON, &account_old_status_reason,
        COLUMN_IS_CHANGING_STATUS, &is_changing_status,
        -1);

      if (account)
      {
        gboolean not_connected = TRUE;
        gboolean can_change_presence;

        can_change_presence = account_can_change_presence(master, account);
        account_connection_status =
          tp_account_get_connection_status(account, &account_status_reason);

        if (account_connection_status == TP_CONNECTION_STATUS_CONNECTING)
        {
          if (account_old_connection_status == TP_CONNECTION_STATUS_CONNECTED)
            play_account_disconnected(master);

          if (account_old_connection_status == TP_CONNECTION_STATUS_CONNECTING)
            not_connected = FALSE;

          if (can_change_presence)
          {
            const gchar *presence =
              pui_profile_get_presence(priv->active_profile, account);

            type = pui_master_get_presence_type(master, account, presence);
          }
          else
            type = TP_CONNECTION_PRESENCE_TYPE_AVAILABLE;

          priv->global_status |= PUI_MASTER_STATUS_CONNECTING;
        }
        else if (account_connection_status == TP_CONNECTION_STATUS_DISCONNECTED)
        {
          const gchar *err_msg;
          const gchar *presence;

          if (account_old_connection_status == TP_CONNECTION_STATUS_CONNECTED)
            play_account_disconnected(master);

          presence = pui_profile_get_presence(priv->active_profile,
                                              account);

          if (!(pui_master_get_presence_type(master, account, presence) ==
                TP_CONNECTION_PRESENCE_TYPE_OFFLINE))
          {
            priv->global_status |= PUI_MASTER_STATUS_ERROR;

            if (is_changing_status &&
                (account_status_reason !=
                 TP_CONNECTION_STATUS_REASON_REQUESTED))
            {
              priv->global_status |= PUI_MASTER_STATUS_REASON_ERROR;
            }

            switch (account_status_reason)
            {
              case TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED:
              case TP_CONNECTION_STATUS_REASON_NETWORK_ERROR:
              {
                err_msg = _("pres_li_network_error");
                break;
              }
              case TP_CONNECTION_STATUS_REASON_REQUESTED:
              {
                err_msg = _("pres_ib_network_error");
                break;
              }
              case TP_CONNECTION_STATUS_REASON_AUTHENTICATION_FAILED:
              {
                err_msg = _("pres_li_authentication_error");
                break;
              }
              case TP_CONNECTION_STATUS_REASON_ENCRYPTION_ERROR:
              {
                err_msg = _("pres_li_encryption_error");
                break;
              }
              case TP_CONNECTION_STATUS_REASON_NAME_IN_USE:
              {
                err_msg = _("pres_li_error_name_in_use");
                break;
              }
              case TP_CONNECTION_STATUS_REASON_CERT_NOT_PROVIDED:
              case TP_CONNECTION_STATUS_REASON_CERT_UNTRUSTED:
              case TP_CONNECTION_STATUS_REASON_CERT_EXPIRED:
              case TP_CONNECTION_STATUS_REASON_CERT_NOT_ACTIVATED:
              case TP_CONNECTION_STATUS_REASON_CERT_HOSTNAME_MISMATCH:
              case TP_CONNECTION_STATUS_REASON_CERT_FINGERPRINT_MISMATCH:
              case TP_CONNECTION_STATUS_REASON_CERT_SELF_SIGNED:
              case TP_CONNECTION_STATUS_REASON_CERT_OTHER_ERROR:
              {
                err_msg = _("pres_li_error_certificate");
                break;
              }
              default:
              {
                err_msg = NULL;
                break;
              }
            }

#if 0
            gtk_tree_model_get(tree_model, &iter,
                               COLUMN_STATUS_MESSAGE, &message,
                               -1);
            g_free(message);
#endif

            if (err_msg)
            {
              const gchar *fmt = _("pres_li_account_with_error");

              status_message = g_strdup_printf(fmt, err_msg);
            }
          }

          type = TP_CONNECTION_PRESENCE_TYPE_OFFLINE;
        }
        else
        {
          gboolean not_sip;
          gboolean msg_diff = FALSE;
          const char *old_status_message;

          if (account_old_connection_status)
            play_account_connected(master);
          else
            not_connected = FALSE;

          if (!can_change_presence)
            type = TP_CONNECTION_PRESENCE_TYPE_AVAILABLE;
          else
            type = tp_account_get_current_presence(account, NULL, &message);

          not_sip = tp_account_is_not_sip(account);

          if (not_sip)
          {
            old_status_message = priv->status_message;
            priv->global_status |= PUI_MASTER_STATUS_CONNECTED;

            if (!old_status_message)
              old_status_message = "";
          }

          msg_diff = g_strcmp0(message, old_status_message);

          if (not_sip && msg_diff)
          {
            account_status_reason = 'r';
            status_message = message;
            priv->global_status |= PUI_MASTER_STATUS_MESSAGE_CHANGED;
          }

          if ((!not_sip || (not_sip && msg_diff)) && can_change_presence)
          {
            gboolean same_presence_type = FALSE;
            gboolean was_offline = FALSE;

            account_old_presence =
              pui_profile_get_presence(priv->active_profile, account);

            if (account_old_presence)
            {
              if (account_can_change_presence(master, account))
              {
                if (pui_master_get_presence_type(master, account,
                                                 account_old_presence) ==
                    tp_account_get_current_presence(account, NULL, NULL))
                {
                  same_presence_type = TRUE;
                }
              }
              else
              {
                TpConnectionStatus connection_status =
                  tp_account_get_connection_status(account, NULL);

                if (!strcmp(account_old_presence, "offline"))
                {
                  was_offline = TRUE;

                  if (connection_status != TP_CONNECTION_STATUS_DISCONNECTED)
                    priv->global_status |= PUI_MASTER_STATUS_OFFLINE;
                }
                else if (connection_status == TP_CONNECTION_STATUS_CONNECTED)
                  same_presence_type = TRUE;
              }

              if (!was_offline && !same_presence_type)
                priv->global_status |= PUI_MASTER_STATUS_OFFLINE;
            }
            else if (account_old_status_reason == 'r')
              account_status_reason = TP_CONNECTION_STATUS_REASON_REQUESTED;
          }
        }

        presence_icon_name = g_strdup(get_presence_icon(type));
        presence_icon = pui_master_get_icon(master, presence_icon_name,
                                            ICON_SIZE_MID);
        g_free(presence_icon_name);

        if (not_connected)
        {
          gtk_list_store_set(
            priv->list_store,
            &iter,
            COLUMN_PRESENCE_TYPE, type,
            COLUMN_PRESENCE_ICON, presence_icon,
            COLUMN_CONNECTION_STATUS, account_connection_status,
            COLUMN_STATUS_MESSAGE, status_message,
            COLUMN_STATUS_REASON, account_status_reason,
            COLUMN_IS_CHANGING_STATUS, FALSE,
            -1);
        }
        else
        {
          gtk_list_store_set(
            priv->list_store,
            &iter,
            COLUMN_PRESENCE_TYPE, type,
            COLUMN_PRESENCE_ICON, presence_icon,
            COLUMN_CONNECTION_STATUS, account_connection_status,
            COLUMN_IS_CHANGING_STATUS, FALSE,
            -1);
        }

        g_free(status_message);
        g_object_unref(account);

        if (can_change_presence)
        {
          if (type == TP_CONNECTION_PRESENCE_TYPE_AVAILABLE)
            priv->global_presence_type = TP_CONNECTION_PRESENCE_TYPE_AVAILABLE;
          else if ((priv->global_presence_type !=
                    TP_CONNECTION_PRESENCE_TYPE_AVAILABLE) &&
                   (type != TP_CONNECTION_PRESENCE_TYPE_OFFLINE))
          {
            priv->global_presence_type = TP_CONNECTION_PRESENCE_TYPE_BUSY;
          }
        }
        else
        {
          if ((account_connection_status == TP_CONNECTION_STATUS_CONNECTED) ||
              (account_connection_status == TP_CONNECTION_STATUS_CONNECTING))
          {
            active_accounts_count++;
          }
        }
      }
    }
    while (gtk_tree_model_iter_next(tree_model, &iter));

    if ((priv->global_presence_type == TP_CONNECTION_PRESENCE_TYPE_OFFLINE) &&
        (active_accounts_count > 0))
    {
      priv->global_presence_type = TP_CONNECTION_PRESENCE_TYPE_AVAILABLE;
    }

    master_presence_changed_cb(master);

    g_signal_emit(master, signals[PRESENCE_CHANGED], 0,
                  priv->global_presence_type, priv->status_message,
                  priv->global_status);

    if (priv->global_status & PUI_MASTER_STATUS_REASON_ERROR)
    {
      if (priv->has_disconnected_account)
      {
        priv->has_disconnected_account = FALSE;

        if (time(0) - priv->last_info_time > 59)
        {
          priv->last_info_time = time(NULL);
          hildon_banner_show_information(
            priv->parent, NULL, _("pres_ib_unable_to_connect_to_service"));
        }
      }
    }
  }

  list_store_enable_sort(GTK_TREE_SORTABLE(priv->list_store), TRUE);
  priv->compute_global_presence_id = 0;

  return FALSE;
}

static void
compute_global_presence_delayed(PuiMaster *master)
{
  PuiMasterPrivate *priv;

  g_return_if_fail(PUI_IS_MASTER(master));

  priv = PRIVATE(master);

  if (!priv->compute_global_presence_id)
  {
    priv->compute_global_presence_id =
      g_idle_add(compute_global_presence_idle, master);
  }
}

static void
account_remove(PuiMaster *master, GtkTreeIter *iter)
{
  PuiMasterPrivate *priv = PRIVATE(master);
  TpAccount *account;

  gtk_tree_model_get(GTK_TREE_MODEL(priv->list_store), iter,
                     COLUMN_ACCOUNT, &account,
                     -1);

  if (account_can_change_presence(master, account))
  {
    priv->presence_supported_count--;

    if (priv->presence_supported_count == 0)
      g_signal_emit(master, signals[PRESENCE_SUPPORT], 0, FALSE);
  }

  g_object_unref(account);
  gtk_list_store_remove(priv->list_store, iter);
  compute_global_presence_delayed(master);
}

static void
on_account_disabled_cb(TpAccountManager *am, TpAccount *account,
                       PuiMaster *master)
{
  GtkTreeIter iter;
  PuiMasterPrivate *priv = PRIVATE(master);

  if (account_get_by_id(master, tp_account_get_path_suffix(account), &iter))
    account_remove(master, &iter);

  if (gtk_tree_model_iter_n_children(
        GTK_TREE_MODEL(priv->list_store), NULL) == 1)
  {
    pui_master_activate_profile(master, default_profiles);
    pui_master_save_config(master);
  }
}

static GdkPixbuf *
avatar_to_pixbuf(const guchar *data, gsize len, const char *mime_type)
{
  GdkPixbufLoader *loader;
  GdkPixbuf *pixbuf = NULL;
  GError *error = NULL;

  if (!data || !mime_type || !*mime_type)
    return NULL;

  loader = gdk_pixbuf_loader_new_with_mime_type(mime_type, &error);

  if (!error)
  {
    gdk_pixbuf_loader_set_size(loader, 48, 48);
    gdk_pixbuf_loader_write(loader, data, len, &error);

    if (!error)
    {
      gdk_pixbuf_loader_close(loader, &error);

      if (!error)
        pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);

      if (pixbuf)
        g_object_ref(pixbuf);
    }

    g_object_unref(loader);
  }

  if (error)
  {
    g_warning("%s failed: %s", __FUNCTION__, error->message);
    g_error_free(error);
  }

  return pixbuf;
}

static void
get_avatar_ready_cb(TpProxy *proxy, const GValue *out_Value,
                    const GError *error, gpointer user_data,
                    GObject *weak_object)
{
  if (error)
  {
    g_warning("%s: Could not get new avatar data %s", __FUNCTION__,
              error->message);
  }
  else if (!G_VALUE_HOLDS(out_Value, TP_STRUCT_TYPE_AVATAR))
  {
    g_warning("%s: Avatar had wrong type: %s", __FUNCTION__,
              G_VALUE_TYPE_NAME(out_Value));
  }
  else
  {
    TpAccount *account = (TpAccount *)proxy;
    PuiMaster *master = PUI_MASTER(weak_object);
    PuiMasterPrivate *priv = PRIVATE(master);
    GdkPixbuf *pixbuf = NULL;
    TpAccount *local_account;
    GtkTreeIter it;
    GValueArray *array = g_value_get_boxed(out_Value);
    const GArray *avatar;
    const gchar *mime_type;

    tp_value_array_unpack(array, 2, &avatar, &mime_type);

    if (avatar)
      pixbuf = avatar_to_pixbuf((guchar *)avatar->data, avatar->len, mime_type);

    if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(priv->list_store), &it))
    {
      do
      {
        gtk_tree_model_get(GTK_TREE_MODEL(priv->list_store), &it,
                           COLUMN_ACCOUNT, &local_account, -1);

        if (account == local_account)
          gtk_list_store_set(priv->list_store, &it, COLUMN_AVATAR, pixbuf, -1);

        if (local_account)
          g_object_unref(local_account);
      }
      while (gtk_tree_model_iter_next(GTK_TREE_MODEL(priv->list_store), &it));
    }

    if (pixbuf)
      g_object_unref(pixbuf);
  }
}

static void
avatar_changed_cb(TpAccount *account, gpointer user_data)
{
  tp_cli_dbus_properties_call_get(
    account, -1, TP_IFACE_ACCOUNT_INTERFACE_AVATAR, "Avatar",
    get_avatar_ready_cb, NULL, NULL, user_data);
}

static void
account_add_to_store(PuiMaster *master, TpAccount *account,
                     gboolean set_presence)
{
  PuiMasterPrivate *priv = PRIVATE(master);
  const gchar *icon_name;
  GdkPixbuf *icon = NULL;
  TpConnectionStatus connection_status;

  icon_name = tp_account_get_icon_name(account);

  if (!icon_name)
  {
    TpProtocol *protocol = pui_master_get_account_protocol(master, account);

    if (protocol)
      icon_name = tp_protocol_get_icon_name(protocol);
  }

  if (icon_name)
  {
    icon = gtk_icon_theme_load_icon(gtk_icon_theme_get_default(), icon_name,
                                    ICON_SIZE_MID, 0, NULL);
  }

  avatar_changed_cb(account, master);
  connection_status = tp_account_get_connection_status(account, NULL);

  gtk_list_store_insert_with_values(
    priv->list_store, NULL, G_MAXINT32,
    COLUMN_ACCOUNT, account,
    COLUMN_SERVICE_ICON, icon,
    COLUMN_AVATAR, NULL,
    COLUMN_CONNECTION_STATUS, connection_status,
    COLUMN_STATUS_REASON, TP_CONNECTION_STATUS_REASON_REQUESTED,
    COLUMN_IS_CHANGING_STATUS, FALSE,
    -1);

  if (connection_status == TP_CONNECTION_STATUS_CONNECTED)
    play_account_connected(master);

  if (icon)
    g_object_unref(icon);

  if (account_can_change_presence(master, account))
  {
    priv->presence_supported_count++;

    if (priv->presence_supported_count == 1)
      g_signal_emit(master, signals[PRESENCE_SUPPORT], COLUMN_ACCOUNT);
  }

  if (set_presence)
    pui_master_set_account_presence(master, account, TRUE, TRUE);

  compute_global_presence_delayed(master);
}

static gboolean
account_get(PuiMaster *master, TpAccount *account, GtkTreeIter *iter)
{
  PuiMasterPrivate *priv = PRIVATE(master);
  TpAccount *local_account;

  if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(priv->list_store), iter))
  {
    do
    {
      gtk_tree_model_get(GTK_TREE_MODEL(priv->list_store), iter,
                         COLUMN_ACCOUNT, &local_account,
                         -1);

      if (account == local_account)
        return TRUE;
    }
    while (gtk_tree_model_iter_next(GTK_TREE_MODEL(priv->list_store), iter));
  }

  return FALSE;
}

static void
presence_changed_cb(TpAccount *account, guint presence, gchar *status,
                    gchar *status_message, PuiMaster *master)
{
  GtkTreeIter iter;

  if (tp_account_get_connection_status(account, NULL) ==
      TP_CONNECTION_STATUS_CONNECTING)
  {
    PuiMasterPrivate *priv = PRIVATE(master);

    if (account_get(master, account, &iter))
    {
      gtk_list_store_set(priv->list_store, &iter,
                         COLUMN_IS_CHANGING_STATUS, TRUE,
                         -1);
    }

    compute_global_presence_delayed(master);
  }
}

static void
status_changed_cb(TpAccount *account, guint old_status, guint new_status,
                  guint reason, gchar *dbus_error_name, GHashTable *details,
                  PuiMaster *master)
{
  PuiMasterPrivate *priv = PRIVATE(master);
  GtkTreeIter it;

  if (!account_get(master, account, &it))
    return;

  gtk_list_store_set(priv->list_store, &it,
                     COLUMN_IS_CHANGING_STATUS, TRUE,
                     -1);

  if ((reason != TP_CONNECTION_STATUS_REASON_REQUESTED) &&
      (new_status == TP_CONNECTION_STATUS_DISCONNECTED))
  {
    const gchar *id = tp_account_get_path_suffix(account);

    if (!g_hash_table_lookup(priv->disconnected_accounts, id))
    {
      g_hash_table_insert(priv->disconnected_accounts, g_strdup(id),
                          GINT_TO_POINTER(1));
      priv->has_disconnected_account = TRUE;
    }
  }

  if ((reason == TP_CONNECTION_STATUS_REASON_REQUESTED) &&
      (new_status == TP_CONNECTION_STATUS_DISCONNECTED))
  {
    g_hash_table_remove_all(priv->disconnected_accounts);
  }

  compute_global_presence_delayed(master);
}

static void
on_property_changed(TpAccount *account, GParamSpec *pspec,
                    PuiMaster *master)
{
  GtkTreeIter it;
  gboolean found;

  found = account_get_by_id(master, tp_account_get_path_suffix(account), &it);

  if (tp_account_is_valid(account) &&
      tp_account_is_enabled(account) &&
      tp_account_get_has_been_online(account))
  {
    if (!found)
      account_add_to_store(master, account, TRUE);
  }
  else if (found)
  {
    account_remove(master, &it);
  }
}

static void
account_append(PuiMaster *master, TpAccount *account, gboolean set_presence)
{
  if (!strcmp(tp_account_get_protocol_name(account), "tel"))
    return;

  g_signal_connect(account, "presence-changed",
                   G_CALLBACK(presence_changed_cb), master);
  g_signal_connect(account, "status-changed",
                   G_CALLBACK(status_changed_cb), master);
  g_signal_connect(account, "avatar-changed",
                   G_CALLBACK(avatar_changed_cb), master);
  g_signal_connect(account, "notify::enabled",
                   G_CALLBACK(on_property_changed), master);
  g_signal_connect(account, "notify::enabled",
                   G_CALLBACK(on_property_changed), master);
  g_signal_connect(account, "notify::has-been-online",
                   G_CALLBACK(on_property_changed), master);

  if (tp_account_is_valid(account) &&
      tp_account_is_enabled(account) &&
      tp_account_get_has_been_online(account))
  {
    account_add_to_store(master, account, set_presence);
  }
}

static void
on_account_enabled_cb(TpAccountManager *am, TpAccount *account,
                      PuiMaster *master)
{
  if (!account_get_by_id(master, tp_account_get_path_suffix(account), NULL))
  {
    account_append(master, account, TRUE);
    compute_global_presence_delayed(master);
  }
}

static void
on_account_validity_changed_cb(TpAccountManager *am, TpAccount *account,
                               gboolean valid, PuiMaster *master)
{
  if (valid)
    on_account_enabled_cb(am, account, master);
  else
    on_account_disabled_cb(am, account, master);
}

static void
on_manager_ready(GObject *object, GAsyncResult *res, gpointer user_data)
{
  TpAccountManager *manager = (TpAccountManager *)object;
  PuiMaster *master = user_data;
  GError *error = NULL;

  if (!tp_proxy_prepare_finish(object, res, &error))
  {
    g_warning("Error preparing AM: %s\n", error->message);
    g_error_free(error);
  }
  else
  {
    GList *accounts = tp_account_manager_dup_valid_accounts(manager);
    GList *l;

    for (l = accounts; l; l = l->next)
      account_append(master, l->data, FALSE);

    g_list_free_full(accounts, g_object_unref);
  }
}

static void
cms_ready_cb(GObject *object, GAsyncResult *res, gpointer user_data)
{
  PuiMaster *master = PUI_MASTER(user_data);
  PuiMasterPrivate *priv = PRIVATE(master);
  GError *error = NULL;
  GList *cms = tp_list_connection_managers_finish(res, &error);
  GList *l;

  if (error != NULL)
  {
    g_warning("Error getting list of CMs: %s", error->message);
    g_error_free(error);
  }
  else if (!cms)
    g_warning("No Telepathy connection managers found");

  for (l = cms; l; l = l->next)
  {
    g_hash_table_insert(priv->connection_managers,
                        g_strdup(tp_connection_manager_get_name(l->data)),
                        l->data);
  }

  g_list_free(cms);

  g_signal_connect(priv->manager, "account-validity-changed",
                   G_CALLBACK(on_account_validity_changed_cb), master);
  g_signal_connect(priv->manager, "account-removed",
                   G_CALLBACK(on_account_disabled_cb), master);
  g_signal_connect(priv->manager, "account-enabled",
                   G_CALLBACK(on_account_enabled_cb), master);
  g_signal_connect(priv->manager, "account-disabled",
                   G_CALLBACK(on_account_disabled_cb), master);

  tp_proxy_prepare_async(priv->manager, NULL, on_manager_ready, master);
}

static void
register_dbus(PuiMaster *self, DBusGConnection *gconnection)
{
  DBusConnection *connection;
  DBusError error;

  dbus_error_init(&error);
  connection = dbus_g_connection_get_connection(gconnection);
  dbus_bus_request_name(connection, "com.nokia.PresenceUI", 0, &error);

  if (dbus_error_is_set(&error))
  {
    g_error("Error registering 'com.nokia.PresenceUI': %s", error.message);

    while (1)
      sleep(1);
  }

  dbus_g_connection_register_g_object(gconnection, "/com/nokia/PresenceUI",
                                      G_OBJECT(self));
}

static void
compute_presence_message(PuiMaster *master)
{
  PuiMasterPrivate *priv;
  const gchar *presence_message = NULL;
  gchar *status_message = NULL;
  const gchar *location;

  g_return_if_fail(PUI_IS_MASTER(master));

  priv = PRIVATE(master);

  if (priv->presence_message && *priv->presence_message)
    presence_message = priv->presence_message;

  location = pui_location_get_location(priv->location);

  if (location && *location)
  {
    if (presence_message)
      status_message = g_strdup_printf("%s - %s", presence_message, location);
    else
      status_message = g_strdup_printf("@ %s", location);
  }
  else if (presence_message)
    status_message = g_strdup(presence_message);

  if (g_strcmp0(status_message, priv->status_message))
  {
    g_free(priv->status_message);
    priv->status_message = status_message;
    priv->flags |= 1;
    pui_master_set_presence(master);
    compute_global_presence_delayed(master);
    return;
  }
  else
    g_free(status_message);
}

static GObject *
pui_master_constructor(GType type, guint n_construct_properties,
                       GObjectConstructParam *construct_properties)
{
  ca_context *c = NULL;
  GObject *object;
  PuiMaster *master;
  PuiMasterPrivate *priv;
  int res;

  object = G_OBJECT_CLASS(pui_master_parent_class)->constructor(
      type, n_construct_properties, construct_properties);

  g_return_val_if_fail(object != NULL, NULL);

  master = PUI_MASTER(object);
  priv = PRIVATE(master);

  tp_list_connection_managers_async(tp_proxy_get_dbus_daemon(priv->manager),
                                    cms_ready_cb, master);

  g_signal_connect(master, "presence-changed",
                   G_CALLBACK(master_presence_changed_cb), master);
  master_presence_changed_cb(master);
  compute_presence_message(master);

  priv->ca_ctx = NULL;
  res = ca_context_create(&c);

  if (res || (res = ca_context_open(c)))
  {
    g_warning("Could not activate libcanberra: %s", ca_strerror(res));

    if (c)
      ca_context_destroy(c);
  }
  else
    priv->ca_ctx = c;

  register_dbus(master, tp_proxy_get_dbus_connection(priv->manager));

  return object;
}

static void
pui_master_finalize(GObject *object)
{
  PuiMasterPrivate *priv = PRIVATE(object);

  if (priv->ca_ctx)
    ca_context_destroy(priv->ca_ctx);

  if (priv->config)
    g_key_file_free(priv->config);

  g_free(priv->config_filename);

  g_list_free_full(priv->profiles, (GDestroyNotify)pui_profile_free);

  g_free(priv->presence_message);
  g_free(priv->status_message);

  G_OBJECT_CLASS(pui_master_parent_class)->finalize(object);
}

static void
pui_master_set_property(GObject *object, guint property_id, const GValue *value,
                        GParamSpec *pspec)
{
  switch (property_id)
  {
    case PROP_DBUS_DAEMON:
    {
      PuiMasterPrivate *priv = PRIVATE(object);

      g_assert(priv->manager == NULL);

      priv->manager = tp_account_manager_new(g_value_get_object(value));
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
pui_master_dispose(GObject *object)
{
  PuiMasterPrivate *priv = PRIVATE(object);

  if (!priv->disposed)
  {
    priv->disposed = TRUE;

    g_hash_table_destroy(priv->icons_default);
    g_hash_table_destroy(priv->icons_mid);
    g_hash_table_destroy(priv->icons_small);
    g_hash_table_destroy(priv->disconnected_accounts);
    g_hash_table_destroy(priv->connection_managers);

    if (priv->compute_global_presence_id)
    {
      g_source_remove(priv->compute_global_presence_id);
      priv->compute_global_presence_id = 0;
    }

    if (priv->set_presence_id)
    {
      g_source_remove(priv->set_presence_id);
      priv->set_presence_id = 0;
    }

    if (priv->list_store)
    {
      g_object_unref(priv->list_store);
      priv->list_store = NULL;
    }

    if (priv->manager)
    {
      g_object_unref(priv->manager);
      priv->manager = NULL;
    }

    if (priv->location)
    {
      g_object_unref(priv->location);
      priv->location = NULL;
    }
  }

  G_OBJECT_CLASS(pui_master_parent_class)->dispose(object);
}

static void
pui_master_class_init(PuiMasterClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->constructor = pui_master_constructor;
  object_class->dispose = pui_master_dispose;
  object_class->finalize = pui_master_finalize;
  object_class->set_property = pui_master_set_property;

  g_object_class_install_property(
    object_class, PROP_DBUS_DAEMON,
    g_param_spec_object(
      "dbus-daemon", "dbus-daemon", "dbus-daemon", TP_TYPE_DBUS_DAEMON,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));

  signals[PRESENCE_CHANGED] = g_signal_new(
      "presence-changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
      0, NULL, NULL, pui_signal_marshal_VOID__UINT_STRING_UINT, G_TYPE_NONE,
      3, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_UINT);
  signals[PROFILE_CREATED] = g_signal_new(
      "profile-created", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL,
      NULL, g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1, G_TYPE_POINTER);
  signals[PROFILE_CHANGED] = g_signal_new(
      "profile-changed", G_TYPE_FROM_CLASS(klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_VOID__POINTER,
      G_TYPE_NONE, 1, G_TYPE_POINTER);
  signals[PROFILE_DELETED] = g_signal_new(
      "profile-deleted", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL,
      NULL, g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1, G_TYPE_POINTER);
  signals[PROFILE_ACTIVATED] = g_signal_new(
      "profile-activated", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
      NULL, NULL, g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1,
      G_TYPE_POINTER);
  signals[AVATAR_CHANGED] = g_signal_new(
      "avatar-changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL,
      NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  signals[PRESENCE_SUPPORT] = g_signal_new(
      "presence-support", G_TYPE_FROM_CLASS(klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      g_cclosure_marshal_VOID__BOOLEAN, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
  signals[SCREEN_STATE_CHANGED] = g_signal_new(
      "screen-state-changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
      NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN, G_TYPE_NONE, TRUE,
      G_TYPE_BOOLEAN);

  pui_dbus_init(G_TYPE_FROM_CLASS(klass));
}

static void
mc_display_status_ind_cb(DBusGProxy *proxy, const char *status,
                         PuiMaster *master)
{
  PuiMasterPrivate *priv = PRIVATE(master);

  priv->display_on = g_strcmp0(status, "off") ? TRUE : FALSE;
  g_signal_emit(master, signals[SCREEN_STATE_CHANGED], 0, priv->display_on);
}

static void
mce_get_display_status_cb(DBusGProxy *proxy, DBusGProxyCall *call_id,
                          gpointer user_data)
{
  PuiMaster *master = user_data;
  PuiMasterPrivate *priv = PRIVATE(master);
  gchar *status = NULL;
  GError *error = NULL;

  if (dbus_g_proxy_end_call(proxy, call_id, &error,
                            G_TYPE_STRING, &status,
                            G_TYPE_INVALID))
  {
    priv->display_on = g_strcmp0(status, "off") ? TRUE : FALSE;
    g_signal_emit(master, signals[SCREEN_STATE_CHANGED], 0, priv->display_on);
    g_free(status);
  }
  else
  {
    g_warning("%s: error: %s (ignored)", __FUNCTION__, error->message);
    g_error_free(error);
  }

  g_object_unref(proxy);
}

static void
mce_dbus_init(PuiMaster *master)
{
  PuiMasterPrivate *priv = PRIVATE(master);
  GError *error = NULL;
  DBusGConnection *gdbus = dbus_g_bus_get(DBUS_BUS_SYSTEM, &error);

  priv->display_on = TRUE;

  if (gdbus)
  {
    DBusGProxy *proxy;

    priv->mce_proxy = dbus_g_proxy_new_for_name(gdbus, MCE_SERVICE,
                                                MCE_SIGNAL_PATH, MCE_SIGNAL_IF);
    dbus_g_proxy_add_signal(priv->mce_proxy, MCE_DISPLAY_SIG, G_TYPE_STRING,
                            G_TYPE_INVALID);
    dbus_g_proxy_connect_signal(priv->mce_proxy, MCE_DISPLAY_SIG,
                                G_CALLBACK(mc_display_status_ind_cb), master,
                                NULL);

    proxy = dbus_g_proxy_new_from_proxy(priv->mce_proxy, MCE_REQUEST_IF,
                                        MCE_REQUEST_PATH);
    dbus_g_proxy_begin_call(proxy, MCE_DISPLAY_STATUS_GET,
                            mce_get_display_status_cb, master, NULL,
                            G_TYPE_INVALID);
  }
  else if (error)
    g_error_free(error);
}

static void
load_profiles(PuiMaster *master)
{
  PuiMasterPrivate *priv = PRIVATE(master);
  int i;

  gchar **groups;
  gchar **group;

  for (i = 0; i < G_N_ELEMENTS(default_profiles); i++)
    priv->profiles = g_list_append(priv->profiles, &default_profiles[i]);

  groups = g_key_file_get_groups(priv->config, NULL);

  for (group = groups; group && *group; group++)
  {
    PuiProfile *profile;
    gchar **keys;
    gchar **key;

    if (strncmp(*group, PUI_PROFILE_HEADER, strlen(PUI_PROFILE_HEADER)))
      continue;

    profile = g_slice_new0(PuiProfile);
    profile->name = g_strdup(*group + strlen(PUI_PROFILE_HEADER));
    profile->icon = g_key_file_get_string(priv->config, *group, "Icon", NULL);
    profile->icon_error = g_strconcat(profile->icon, "_error", NULL);
    profile->default_presence = g_key_file_get_string(priv->config, *group,
                                                      "DefaultPresence", NULL);
    profile->accounts = NULL;
    keys = g_key_file_get_keys(priv->config, *group, NULL, NULL);

    for (key = keys; key && *key; key++)
    {
      PuiAccount *account;

      if (strncmp(*key, PUI_ACCOUNT_HEADER, strlen(PUI_ACCOUNT_HEADER)))
        continue;

      account = g_slice_new(PuiAccount);

      account->account_id = g_strdup(*key + strlen(PUI_ACCOUNT_HEADER));
      account->presence =
        g_key_file_get_string(priv->config, *group, *key, NULL);
      profile->accounts = g_slist_prepend(profile->accounts, account);
    }

    g_strfreev(keys);
    priv->profiles = g_list_append(priv->profiles, profile);
  }

  g_strfreev(groups);

  priv->active_profile = g_list_nth_data(
      priv->profiles,
      g_key_file_get_integer(priv->config, "General", "ActiveProfile", NULL));

  if (!priv->active_profile)
    priv->active_profile = priv->profiles->data;
}

static void
load_config(PuiMaster *master)
{
  PuiMasterPrivate *priv = PRIVATE(master);
  GError *error = NULL;

  priv->config_filename = g_build_filename(g_get_home_dir(), ".osso",
                                           ".rtcom-presence-ui.cfg", NULL);
  priv->config = g_key_file_new();
  g_key_file_load_from_file(priv->config, priv->config_filename,
                            G_KEY_FILE_KEEP_COMMENTS, &error);

  if (error)
  {
    g_warning("%s error loading %s: %s", __FUNCTION__, priv->config_filename,
              error->message);
    g_error_free(error);
  }
  else
  {
    pui_location_set_level(
      priv->location,
      g_key_file_get_integer(priv->config,
                             "General", "LocationLevel", &error));

    if (error)
    {
      g_clear_error(&error);
      pui_location_set_level(priv->location, PUI_LOCATION_LEVEL_NONE);
    }

    priv->presence_message = g_key_file_get_string(
        priv->config, "General", "StatusMessage", NULL);
  }

  load_profiles(master);
}

static void
location_error_cb(PuiLocation *location, guint error, PuiMaster *master)
{
  pui_master_set_location_level(master, PUI_LOCATION_LEVEL_NONE);
  pui_master_save_config(master);
}

static void
location_address_changed_cb(PuiLocation *location, PuiMaster *master)
{
  compute_presence_message(master);
}

static gint
get_presence_weight(TpConnectionPresenceType presence_type, gchar *msg)
{
  if (presence_type != TP_CONNECTION_PRESENCE_TYPE_OFFLINE)
    return presence_type != TP_CONNECTION_PRESENCE_TYPE_AVAILABLE;

  if (msg)
    return 2;

  return 3;
}

static gint
accounts_sort_cmp(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b,
                  gpointer user_data)
{
  PuiMaster *master = user_data;
  gint rv;
  gchar *msg2 = NULL;
  gchar *msg1 = NULL;
  TpConnectionPresenceType presence_type2;
  TpConnectionPresenceType presence_type1;
  TpAccount *account1;
  TpAccount *account2;

  gtk_tree_model_get(model, a,
                     COLUMN_ACCOUNT, &account1,
                     COLUMN_PRESENCE_TYPE, &presence_type1,
                     COLUMN_STATUS_MESSAGE, &msg1,
                     -1);

  if (!account1)
    return 1;

  gtk_tree_model_get(model, b,
                     COLUMN_ACCOUNT, &account2,
                     COLUMN_PRESENCE_TYPE, &presence_type2,
                     COLUMN_STATUS_MESSAGE, &msg2,
                     -1);

  if (!account2)
  {
    g_free(msg1);
    g_object_unref(account1);
    return -1;
  }

  rv = get_presence_weight(presence_type1, msg1) -
    get_presence_weight(presence_type2, msg2);

  if (!rv)
  {
    const gchar *service_name1;
    const gchar *service_name2;

    service_name1 = pui_master_get_account_service_name(master, account1, NULL);
    service_name2 = pui_master_get_account_service_name(master, account2, NULL);
    rv = g_strcmp0(service_name1, service_name2);

    if (!rv)
    {
      const gchar *display_name1;
      const gchar *display_name2;

      display_name1 = pui_master_get_account_display_name(master, account1);
      display_name2 = pui_master_get_account_display_name(master, account2);
      rv = g_strcmp0(display_name1, display_name2);
    }
  }

  g_free(msg1);
  g_free(msg2);
  g_object_unref(account1);
  g_object_unref(account2);

  return rv;
}

static void
pui_master_init(PuiMaster *master)
{
  PuiMasterPrivate *priv = PRIVATE(master);

  priv->global_presence_type = TP_CONNECTION_PRESENCE_TYPE_UNSET;

  priv->list_store = gtk_list_store_new(9, TP_TYPE_ACCOUNT, G_TYPE_UINT,
                                        GDK_TYPE_PIXBUF, GDK_TYPE_PIXBUF,
                                        G_TYPE_STRING, GDK_TYPE_PIXBUF,
                                        G_TYPE_UINT, G_TYPE_UINT,
                                        G_TYPE_BOOLEAN);

  gtk_tree_sortable_set_default_sort_func(GTK_TREE_SORTABLE(priv->list_store),
                                          accounts_sort_cmp, master, NULL);
  list_store_enable_sort(GTK_TREE_SORTABLE(priv->list_store), TRUE);
  gtk_list_store_insert_with_values(priv->list_store, NULL, G_MAXINT32,
                                    COLUMN_ACCOUNT, NULL, -1);

  priv->icons_default = g_hash_table_new_full((GHashFunc)g_str_hash,
                                              (GEqualFunc)g_str_equal,
                                              (GDestroyNotify)g_free,
                                              (GDestroyNotify)g_object_unref);
  priv->icons_mid = g_hash_table_new_full((GHashFunc)g_str_hash,
                                          (GEqualFunc)g_str_equal,
                                          (GDestroyNotify)g_free,
                                          (GDestroyNotify)g_object_unref);
  priv->icons_small = g_hash_table_new_full((GHashFunc)g_str_hash,
                                            (GEqualFunc)g_str_equal,
                                            (GDestroyNotify)g_free,
                                            (GDestroyNotify)g_object_unref);
  priv->flags |= 3;
  priv->default_presence_message = _("pres_fi_status_message_default_text");

  priv->location = g_object_new(PUI_TYPE_LOCATION, NULL);
  priv->disconnected_accounts = g_hash_table_new_full((GHashFunc)g_str_hash,
                                                      (GEqualFunc)g_str_equal,
                                                      (GDestroyNotify)g_free,
                                                      NULL);

  g_signal_connect(priv->location, "error",
                   G_CALLBACK(location_error_cb), master);
  g_signal_connect(priv->location, "address-changed",
                   G_CALLBACK(location_address_changed_cb), master);
  pui_location_set_level(priv->location, PUI_LOCATION_LEVEL_NONE);

  load_config(master);
  mce_dbus_init(master);

  priv->connection_managers =
    g_hash_table_new_full((GHashFunc)g_str_hash,
                          (GEqualFunc)g_str_equal,
                          (GDestroyNotify)g_free,
                          (GDestroyNotify)g_object_unref);
}

PuiMaster *
pui_master_new(TpDBusDaemon *dbus_daemon)
{
  return g_object_new(PUI_TYPE_MASTER, "dbus-daemon", dbus_daemon, NULL);
}

TpConnectionPresenceType
pui_master_get_presence_type(PuiMaster *master, TpAccount *account,
                             const char *presence)
{
  TpConnectionPresenceType presence_type = TP_CONNECTION_PRESENCE_TYPE_BUSY;
  TpProtocol *protocol;

  if (!strcmp(presence, "offline"))
    return TP_CONNECTION_PRESENCE_TYPE_OFFLINE;

  if (!strcmp(presence, "available"))
    return TP_CONNECTION_PRESENCE_TYPE_AVAILABLE;

  protocol = pui_master_get_account_protocol(master, account);

  if (protocol)
  {
    GList *presence_statuses;
    GList *l;

    presence_statuses = tp_protocol_dup_presence_statuses(protocol);

    for (l = presence_statuses; l; l = l->next)
    {
      if (!strcmp(tp_presence_status_spec_get_name(l->data), presence))
        break;
    }

    if (l)
    {
      presence_type = tp_presence_status_spec_get_presence_type(l->data);

      if (presence_type == TP_CONNECTION_PRESENCE_TYPE_UNSET)
        presence_type = TP_CONNECTION_PRESENCE_TYPE_BUSY;
    }

    g_list_free_full(presence_statuses,
                     (GDestroyNotify)tp_presence_status_spec_free);
  }

  return presence_type;
}

const gchar *
pui_master_get_presence_message(PuiMaster *master)
{
  g_return_val_if_fail(PUI_IS_MASTER(master), NULL);

  return PRIVATE(master)->presence_message;
}

const gchar *
pui_master_get_default_presence_message(PuiMaster *master)
{
  g_return_val_if_fail(PUI_IS_MASTER(master), NULL);

  return PRIVATE(master)->default_presence_message;
}

PuiProfile *
pui_master_get_active_profile(PuiMaster *master)
{
  g_return_val_if_fail(PUI_IS_MASTER(master), NULL);

  return PRIVATE(master)->active_profile;
}

GKeyFile *
pui_master_get_config(PuiMaster *master)
{
  g_return_val_if_fail(PUI_IS_MASTER(master), NULL);

  return PRIVATE(master)->config;
}

gboolean
pui_master_get_display_on(PuiMaster *master)
{
  g_return_val_if_fail(PUI_IS_MASTER(master), FALSE);

  return PRIVATE(master)->display_on;
}

PuiLocationLevel
pui_master_get_location_level(PuiMaster *master)
{
  g_return_val_if_fail(PUI_IS_MASTER(master), PUI_LOCATION_LEVEL_STREET);

  return pui_location_get_level(PRIVATE(master)->location);
}

GtkListStore *
pui_master_get_model(PuiMaster *master)
{
  g_return_val_if_fail(PUI_IS_MASTER(master), NULL);

  return PRIVATE(master)->list_store;
}

GList *
pui_master_get_profiles(PuiMaster *master)
{
  g_return_val_if_fail(PUI_IS_MASTER(master), NULL);

  return PRIVATE(master)->profiles;
}

gboolean
pui_master_is_presence_supported(PuiMaster *master)
{
  g_return_val_if_fail(PUI_IS_MASTER(master), FALSE);

  return PRIVATE(master)->presence_supported_count > 0;
}

static time_t
now()
{
  struct timespec tp;

  clock_gettime(CLOCK_MONOTONIC, &tp);

  return tp.tv_sec;
}

static gboolean
is_silent_profile()
{
  char *profile = profile_get_profile();
  gboolean rv = !strcmp("silent", profile) ? TRUE : FALSE;

  free(profile);

  return rv;
}

static void
pui_master_play_sound(ca_context *c, const char *sound, int min_time,
                      time_t *time_last_played)
{
  if (c)
  {
    time_t time_now = now();

    if (difftime(time_now, *time_last_played) > (double)min_time)
    {
      *time_last_played = time_now;

      g_return_if_fail(sound != NULL);

      if (sound)
      {
        if (!is_silent_profile())
        {
          ca_proplist *p;
          int res;

          ca_proplist_create(&p);
          ca_proplist_sets(p,
                           "module-stream-restore.id",
                           "x-maemo-system-sound");
          ca_proplist_sets(p, "media.role", "dialog-information");
          ca_proplist_sets(p, "media.filename", sound);
          res = ca_context_play_full(c, 0, p, NULL, NULL);

          if (res)
            g_warning("%s: %s", __FUNCTION__, ca_strerror(res));

          ca_proplist_destroy(p);
        }
      }
    }
  }
}

void
play_account_connected(PuiMaster *master)
{
  PuiMasterPrivate *priv;

  g_return_if_fail(PUI_IS_MASTER(master));

  priv = PRIVATE(master);
  pui_master_play_sound(priv->ca_ctx, "/usr/share/sounds/presence-online.wav",
                        5, &priv->connected_time);
}

void
play_account_disconnected(PuiMaster *master)
{
  PuiMasterPrivate *priv;

  g_return_if_fail(PUI_IS_MASTER(master));

  priv = PRIVATE(master);
  pui_master_play_sound(priv->ca_ctx, "/usr/share/sounds/presence-offline.wav",
                        5, &priv->disconnected_time);
}

void
pui_master_set_presence_message(PuiMaster *master, const gchar *message)
{
  PuiMasterPrivate *priv;

  g_return_if_fail(PUI_IS_MASTER(master));

  priv = PRIVATE(master);
  g_free(priv->presence_message);
  priv->presence_message = g_strdup(message);

  if (priv->default_presence_message == message)
    message = NULL;

  g_key_file_set_string(priv->config,
                        "General", "StatusMessage", message);
  compute_presence_message(master);
}

void
pui_master_save_profile(PuiMaster *master, PuiProfile *profile)
{
  PuiMasterPrivate *priv;
  GSList *l;
  gchar *key;

  g_return_if_fail(PUI_IS_MASTER(master));

  priv = PRIVATE(master);

  if (!g_list_find(priv->profiles, profile))
  {
    priv->profiles = g_list_append(NULL, profile);
    g_signal_emit(master, signals[PROFILE_CREATED], 0, profile);
  }
  else
    g_signal_emit(master, signals[PROFILE_CHANGED], 0, profile);

  key = g_strdup_printf("%s %s", "Profile", profile->name);

  g_key_file_set_string(priv->config,
                        key, "Icon", profile->icon);
  g_key_file_set_string(priv->config,
                        key, "DefaultPresence", profile->default_presence);

  for (l = profile->accounts; l; l = l->next)
  {
    PuiAccount *account = l->data;
    gchar *string = g_strdup_printf("%s%s", "Account-", account->account_id);

    g_key_file_set_string(priv->config,
                          key, string, account->presence);

    g_free(string);
  }

  g_free(key);
  pui_master_save_config(master);
}

void
pui_master_save_config(PuiMaster *master)
{
  PuiMasterPrivate *priv;
  GError *error = NULL;
  gchar *data;
  gsize length;

  g_return_if_fail(PUI_IS_MASTER(master));

  priv = PRIVATE(master);
  data = g_key_file_to_data(priv->config, &length, &error);

  if (error)
  {
    g_warning("%s error: %s", __FUNCTION__, error->message);
    g_error_free(error);
  }
  else
  {
    g_file_set_contents(priv->config_filename, data, length, &error);
    g_free(data);

    if (error)
    {
      g_warning("%s error writing %s: %s", __FUNCTION__,
                priv->config_filename, error->message);
      g_error_free(error);
    }
  }
}

gboolean
pui_master_erase_profile(PuiMaster *master, PuiProfile *profile)
{
  gboolean rv;
  PuiMasterPrivate *priv;
  gchar *group_name;

  g_return_val_if_fail(PUI_IS_MASTER(master), FALSE);

  priv = PRIVATE(master);
  group_name = g_strdup_printf("%s%s", "Profile ", profile->name);
  rv = g_key_file_remove_group(priv->config, group_name, NULL);
  g_free(group_name);

  return rv;
}

void
pui_master_delete_profile(PuiMaster *master, PuiProfile *profile)
{
  PuiMasterPrivate *priv;

  g_return_if_fail(PUI_IS_MASTER(master));

  priv = PRIVATE(master);

  if (priv->active_profile == profile)
  {
    priv->active_profile = NULL;
    pui_master_activate_profile(master, &default_profiles[0]);
  }

  g_signal_emit(master, signals[PROFILE_DELETED], 0, profile);
  pui_master_erase_profile(master, profile);
  priv->profiles = g_list_remove(priv->profiles, profile);
  pui_master_save_config(master);
  pui_profile_free(profile);
}

void
pui_master_set_location_level(PuiMaster *master, PuiLocationLevel level)
{
  PuiMasterPrivate *priv;

  g_return_if_fail(PUI_IS_MASTER(master));

  priv = PRIVATE(master);

  g_return_if_fail(level < PUI_LOCATION_LEVEL_LAST);

  pui_location_reset(priv->location);
  g_key_file_set_integer(priv->config,
                         "General", "LocationLevel", level);

  if ((level != PUI_LOCATION_LEVEL_NONE) &&
      (pui_location_get_level(priv->location) == PUI_LOCATION_LEVEL_NONE))
  {
    hildon_banner_show_information(priv->parent, NULL,
                                   _("pres_ib_location_turned_on"));
  }

  pui_location_set_level(priv->location, level);
  master_presence_changed_cb(master);
  compute_presence_message(master);
}

TpProtocol *
pui_master_get_account_protocol(PuiMaster *master, TpAccount *account)
{
  PuiMasterPrivate *priv;
  const gchar *cm_name;
  const gchar *protocol_name;
  TpConnectionManager *cm;

  g_return_val_if_fail(PUI_IS_MASTER(master), NULL);

  priv = PRIVATE(master);

  cm_name = tp_account_get_cm_name(account);
  g_return_val_if_fail(cm_name != NULL, NULL);

  cm = g_hash_table_lookup(priv->connection_managers, cm_name);
  g_return_val_if_fail(cm, NULL);

  protocol_name = tp_account_get_protocol_name(account);
  g_return_val_if_fail(protocol_name, NULL);

  return tp_connection_manager_get_protocol_object(cm, protocol_name);
}

const gchar *
pui_master_get_account_service_name(PuiMaster *master, TpAccount *account,
                                    TpProtocol **protocol)
{
  TpProtocol *local_protocol;
  const gchar *service_name = NULL;

  g_return_val_if_fail(PUI_IS_MASTER(master), NULL);

  local_protocol = pui_master_get_account_protocol(master, account);

  if (local_protocol)
    service_name = tp_protocol_get_english_name(local_protocol);

  if (protocol)
    *protocol = local_protocol;

  return service_name;
}

const gchar *
pui_master_get_account_display_name(PuiMaster *master, TpAccount *account)
{
  const gchar *display_name = tp_account_get_display_name(account);

  if (!display_name || !*display_name)
  {
    display_name = tp_account_get_normalized_name(account);

    if (!display_name || !*display_name)
    {
      GHashTable *parameters = (GHashTable *)tp_account_get_parameters(account);

      display_name = tp_asv_get_string(parameters, "account");
    }
  }

  return display_name;
}

void
pui_master_scan_profile(PuiMaster *master, PuiProfile *profile,
                        gboolean *no_sip_in_profile,
                        TpConnectionPresenceType *aggregate_presence)
{
  TpConnectionPresenceType presence = TP_CONNECTION_PRESENCE_TYPE_OFFLINE;
  PuiMasterPrivate *priv;
  GtkTreeIter it;

  g_return_if_fail(PUI_IS_MASTER(master));

  priv = PRIVATE(master);

  if (no_sip_in_profile)
    *no_sip_in_profile = FALSE;

  if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(priv->list_store), &it))
  {
    int cannot_change_presence = 0;

    do
    {
      TpAccount *account;

      gtk_tree_model_get(GTK_TREE_MODEL(priv->list_store), &it,
                         COLUMN_ACCOUNT, &account, -1);

      if (account)
      {
        TpConnectionPresenceType presence_type;

        presence_type = pui_master_get_presence_type(
            master, account, pui_profile_get_presence(profile, account));

        if (presence_type != TP_CONNECTION_PRESENCE_TYPE_OFFLINE)
        {
          if (tp_account_is_not_sip(account))
            *no_sip_in_profile = TRUE;

          if (!account_can_change_presence(master, account))
            cannot_change_presence++;
        }

        if (account_can_change_presence(master, account))
        {
          if (presence_type == TP_CONNECTION_PRESENCE_TYPE_AVAILABLE)
            presence = TP_CONNECTION_PRESENCE_TYPE_AVAILABLE;
          else if ((presence != TP_CONNECTION_PRESENCE_TYPE_AVAILABLE) &&
                   (presence_type != TP_CONNECTION_PRESENCE_TYPE_OFFLINE))
          {
            presence = TP_CONNECTION_PRESENCE_TYPE_BUSY;
          }
        }

        g_object_unref(account);
      }
    }
    while (gtk_tree_model_iter_next(GTK_TREE_MODEL(priv->list_store), &it));

    if ((presence == TP_CONNECTION_PRESENCE_TYPE_OFFLINE) &&
        (cannot_change_presence > 0))
    {
      presence = TP_CONNECTION_PRESENCE_TYPE_AVAILABLE;
    }
  }

  if (aggregate_presence)
    *aggregate_presence = presence;
}

static gboolean
pui_master_set_presence_idle(gpointer user_data)
{
  PuiMaster *master = user_data;
  PuiMasterPrivate *priv = PRIVATE(master);
  gboolean presence_set = FALSE;
  GtkTreeIter it;

  if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(priv->list_store), &it))
  {
    do
    {
      TpAccount *account;

      gtk_tree_model_get(GTK_TREE_MODEL(priv->list_store), &it,
                         COLUMN_ACCOUNT, &account, -1);

      if (account)
      {
        if (pui_master_set_account_presence(master, account,
                                            priv->flags & 2, priv->flags & 1))
        {
          presence_set = TRUE;
        }

        g_object_unref(account);
      }
    }
    while (gtk_tree_model_iter_next(GTK_TREE_MODEL(priv->list_store), &it));
  }

  priv->flags &= ~3u;

  if (!presence_set)
    compute_global_presence_delayed(master);

  priv->set_presence_id = 0;
  return FALSE;
}

void
pui_master_set_presence(PuiMaster *master)
{
  PuiMasterPrivate *priv;

  g_return_if_fail(PUI_IS_MASTER(master));

  priv = PRIVATE(master);

  if (!priv->set_presence_id)
    priv->set_presence_id = g_idle_add(pui_master_set_presence_idle, master);
}

gboolean
pui_master_set_account_presence(PuiMaster *master, TpAccount *account,
                                gboolean flag1, gboolean flag2)
{
  g_return_val_if_fail(PUI_IS_MASTER(master), FALSE);
  g_return_val_if_fail(TP_IS_ACCOUNT(account), FALSE);

  if (flag2 || flag1)
  {
    PuiMasterPrivate *priv = PRIVATE(master);
    const gchar *status =
      pui_profile_get_presence(priv->active_profile, account);
    TpConnectionPresenceType type =
      pui_master_get_presence_type(master, account, status);

    tp_account_request_presence_async(account, type, status,
                                      priv->status_message, NULL, NULL);

    if ((type == TP_CONNECTION_PRESENCE_TYPE_UNSET) ||
        (type == TP_CONNECTION_PRESENCE_TYPE_OFFLINE))
    {
      if (tp_account_get_connect_automatically(account))
        tp_account_set_connect_automatically_async(account, FALSE, NULL, NULL);
    }
    else
    {
      tp_account_set_automatic_presence_async(account, type, status,
                                              priv->status_message, NULL, NULL);

      if (!tp_account_get_connect_automatically(account))
        tp_account_set_connect_automatically_async(account, TRUE, NULL, NULL);
    }

    return TRUE;
  }

  return FALSE;
}

void
pui_master_activate_profile(PuiMaster *master, PuiProfile *profile)
{
  PuiMasterPrivate *priv;

  g_return_if_fail(PUI_IS_MASTER(master));

  priv = PRIVATE(master);

  priv->active_profile = profile;
  priv->profile_change_time = now();
  g_key_file_set_integer(priv->config, "General", "ActiveProfile",
                         g_list_index(priv->profiles, profile));
  g_signal_emit(master, signals[PROFILE_ACTIVATED], 0, priv->active_profile);
  priv->flags |= 2;
  pui_master_set_presence(master);
  compute_global_presence_delayed(master);
}

GdkPixbuf *
pui_master_get_icon(PuiMaster *master, const gchar *icon_name, gint icon_size)
{
  PuiMasterPrivate *priv;
  GdkPixbuf *icon;
  GHashTable *icons;

  g_return_val_if_fail(PUI_IS_MASTER(master), NULL);

  priv = PRIVATE(master);

  if (!icon_name)
    return NULL;

  g_return_val_if_fail(icon_size == ICON_SIZE_DEFAULT ||
                       icon_size == ICON_SIZE_MID ||
                       icon_size == ICON_SIZE_SMALL, NULL);

  if (icon_size == ICON_SIZE_DEFAULT)
    icons = priv->icons_default;
  else if (icon_size == ICON_SIZE_MID)
    icons = priv->icons_mid;
  else
    icons = priv->icons_small;

  icon = g_hash_table_lookup(icons, icon_name);

  if (!icon)
  {
    icon = gtk_icon_theme_load_icon(gtk_icon_theme_get_default(), icon_name,
                                    icon_size, 0, NULL);

    if (icon)
      g_hash_table_insert(icons, g_strdup(icon_name), icon);
  }

  return icon;
}

GdkPixbuf *
pui_master_get_profile_icon(PuiMaster *master, PuiProfile *profile)
{
  return pui_master_get_icon(master, profile->icon, ICON_SIZE_DEFAULT);
}

void
pui_master_get_global_presence(PuiMaster *master,
                               TpConnectionPresenceType *presence_type,
                               const gchar **status_message, guint *status)
{
  PuiMasterPrivate *priv = PRIVATE(master);

  if (presence_type)
    *presence_type = priv->global_presence_type;

  if (status_message)
    *status_message = priv->status_message;

  if (status)
    *status = priv->global_status;
}
