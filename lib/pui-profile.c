/*
 * pui-profile.c
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

#include "pui-profile.h"

void
pui_profile_free(PuiProfile *profile)
{
  GSList *l;

  if (profile->builtin)
    return;

  for (l = profile->accounts; l; l = l->next)
  {
    PuiAccount *account = l->data;

    g_free(account->account_id);
    g_free(account->presence);
    g_slice_free(PuiAccount, account);
  }

  g_slist_free(profile->accounts);
  g_free(profile->name);
  g_free(profile->icon);
  g_free(profile->icon_error);
  g_free(profile->default_presence);
  g_slice_free(PuiProfile, profile);
}

void
pui_profile_set_account_presence(PuiProfile *profile, const char *account_id,
                                 gchar *presence)
{
  GSList *l;
  PuiAccount *account;

  for (l = profile->accounts; l; l = l->next)
  {
    account = l->data;

    if (!strcmp(account->account_id, account_id))
      break;
  }

  if (l)
  {
    account = l->data;
    g_free(account->presence);
  }
  else
  {
    account = g_slice_new(PuiAccount);
    account->account_id = g_strdup(account_id);
    profile->accounts = g_slist_prepend(profile->accounts, account);
  }

  account->presence = presence;
}

const gchar *
pui_profile_get_presence(PuiProfile *profile, TpAccount *account)
{
  GSList *l;
  const gchar *id = tp_account_get_path_suffix(account);

  for (l = profile->accounts; l; l = l->next)
  {
    PuiAccount *a = l->data;

    if (!strcmp(id, a->account_id))
      return a->presence;
  }

  return profile->default_presence;
}
