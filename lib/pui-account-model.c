/*
 * pui-account-model.c
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

#include "pui-account-model.h"

G_DEFINE_TYPE(
  PuiAccountModel,
  pui_account_model,
  GTK_TYPE_LIST_STORE
);

#if 0
static void
pui_account_model_finalize(GObject *object)
{
  G_OBJECT_CLASS(pui_account_model_parent_class)->finalize(object);
}

static void
pui_account_model_dispose(GObject *object)
{
  G_OBJECT_CLASS(pui_account_model_parent_class)->dispose(object);
}
#endif

static void
pui_account_model_class_init(PuiAccountModelClass *klass)
{
#if 0
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->finalize = pui_account_model_finalize;
  object_class->dispose = pui_account_model_dispose;
#endif
}

static void
pui_account_model_init(PuiAccountModel *self)
{}
