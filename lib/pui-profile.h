/*
 * pui-profile.h
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

#ifndef __PUI_PROFILE_H_INCUDED__
#define __PUI_PROFILE_H_INCUDED__

#include <telepathy-glib/telepathy-glib.h>

G_BEGIN_DECLS

struct _PuiAccount
{
  gchar *account_id;
  gchar *presence;
};

typedef struct _PuiAccount PuiAccount;

struct _PuiProfile
{
  gchar *name;
  gchar *icon;
  gchar *icon_error;
  gboolean builtin;
  GSList *accounts;
  gchar *default_presence;
};

typedef struct _PuiProfile PuiProfile;

void
pui_profile_free(PuiProfile *profile);

void
pui_profile_set_account_presence(PuiProfile *profile, const char *account_id,
                                 gchar *presence);

const gchar *
pui_profile_get_presence(PuiProfile *profile, TpAccount *account);

G_END_DECLS

#endif /* __PUI_PROFILE_H_INCUDED__ */
