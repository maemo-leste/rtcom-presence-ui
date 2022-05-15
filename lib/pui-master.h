/*
 * pui-master.h
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

#ifndef __PUI_MASTER_H_INCLUDED__
#define __PUI_MASTER_H_INCLUDED__

#include <hildon/hildon.h>

#include "pui-location.h"
#include "pui-profile.h"

G_BEGIN_DECLS

#define PUI_TYPE_MASTER \
                (pui_master_get_type ())
#define PUI_MASTER(obj) \
                (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                 PUI_TYPE_MASTER, \
                 PuiMaster))
#define PUI_MASTER_CLASS(cls) \
                (G_TYPE_CHECK_CLASS_CAST ((cls), \
                 PUI_TYPE_MASTER, \
                 PuiMasterClass))
#define PUI_IS_MASTER(obj) \
                (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                 PUI_TYPE_MASTER))
#define PUI_IS_MASTER_CLASS(obj) \
                (G_TYPE_CHECK_CLASS_TYPE ((obj), \
                 PUI_TYPE_MASTER))
#define PUI_MASTER_GET_CLASS(obj) \
                (G_TYPE_INSTANCE_GET_CLASS ((obj), \
                 PUI_TYPE_MASTER, \
                 PuiMasterClass))

struct _PuiMaster
{
  GObject parent;
};

typedef struct _PuiMaster PuiMaster;

struct _PuiMasterClass
{
  GObjectClass parent_class;
};

typedef struct _PuiMasterClass PuiMasterClass;

#define ICON_SIZE_DEFAULT HILDON_ICON_PIXEL_SIZE_FINGER
#define ICON_SIZE_MID 24
#define ICON_SIZE_SMALL 16

enum
{
  COLUMN_ACCOUNT,
  COLUMN_PRESENCE_TYPE,
  COLUMN_PRESENCE_ICON,
  COLUMN_SERVICE_ICON,
  COLUMN_STATUS_MESSAGE,
  COLUMN_AVATAR,
  COLUMN_CONNECTION_STATUS,
  COLUMN_STATUS_REASON,
  COLUMN_IS_CHANGING_STATUS
};

enum
{
  PUI_MASTER_STATUS_NONE = 0,
  PUI_MASTER_STATUS_ERROR = 1 << 0,
  PUI_MASTER_STATUS_CONNECTING = 1 << 2,
  PUI_MASTER_STATUS_MESSAGE_CHANGED = 1 << 3,
  PUI_MASTER_STATUS_CONNECTED = 1 << 4,
  PUI_MASTER_STATUS_OFFLINE = 1 << 5,
  PUI_MASTER_STATUS_REASON_ERROR = 1 << 6
};

GType
pui_master_get_type(void) G_GNUC_CONST;

PuiMaster *
pui_master_new(TpDBusDaemon *dbus_daemon);

TpConnectionPresenceType
pui_master_get_presence_type(PuiMaster *master, TpAccount *account,
                             const char *presence);

const gchar *
pui_master_get_presence_message(PuiMaster *master);

const gchar *
pui_master_get_default_presence_message(PuiMaster *master);

PuiProfile *
pui_master_get_active_profile(PuiMaster *master);

GKeyFile *
pui_master_get_config(PuiMaster *master);

gboolean
pui_master_get_display_on(PuiMaster *master);

PuiLocationLevel
pui_master_get_location_level(PuiMaster *master);

void
pui_master_set_location_level(PuiMaster *master, PuiLocationLevel level);

GtkListStore *
pui_master_get_model(PuiMaster *master);

GList *
pui_master_get_profiles(PuiMaster *master);

gboolean
pui_master_is_presence_supported(PuiMaster *master);

void
play_account_connected(PuiMaster *master);

void
play_account_disconnected(PuiMaster *master);

void
pui_master_set_presence_message(PuiMaster *master, const gchar *message);

void
pui_master_save_profile(PuiMaster *master, PuiProfile *profile);

void
pui_master_save_config(PuiMaster *master);

gboolean
pui_master_erase_profile(PuiMaster *master, PuiProfile *profile);

void
pui_master_delete_profile(PuiMaster *master, PuiProfile *profile);

TpProtocol *
pui_master_get_account_protocol(PuiMaster *master, TpAccount *account);

const gchar *
pui_master_get_account_service_name(PuiMaster *master, TpAccount *account,
                                    TpProtocol **protocol);

const gchar *
pui_master_get_account_display_name(PuiMaster *master, TpAccount *account);

void
pui_master_set_presence(PuiMaster *master);

void
pui_master_scan_profile(PuiMaster *master, PuiProfile *profile,
                        gboolean *no_sip_in_profile,
                        TpConnectionPresenceType *aggregate_presence);

gboolean
pui_master_set_account_presence(PuiMaster *master, TpAccount *account,
                                gboolean flag1, gboolean flag2);

void
pui_master_activate_profile(PuiMaster *master, PuiProfile *profile);

GdkPixbuf *
pui_master_get_icon(PuiMaster *master, const gchar *icon_name, gint icon_size);

GdkPixbuf *
pui_master_get_profile_icon(PuiMaster *master, PuiProfile *profile);

void
pui_master_get_global_presence(PuiMaster *master,
                               TpConnectionPresenceType *presence_type,
                               const gchar **status_message, guint *status);

G_END_DECLS

#endif /* __PUI_MASTER_H_INCLUDED__ */
