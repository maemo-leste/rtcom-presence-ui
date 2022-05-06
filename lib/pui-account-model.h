/*
 * pui-account-model.h
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

#ifndef __PUI_ACCOUNT_MODEL_H_INCLUDED__
#define __PUI_ACCOUNT_MODEL_H_INCLUDED__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define PUI_TYPE_ACCOUNT_MODEL \
                (pui_account_model_get_type ())
#define PUI_ACCOUNT_MODEL(obj) \
                (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                 PUI_TYPE_ACCOUNT_MODEL, \
                 PuiAccountModel))
#define PUI_ACCOUNT_MODEL_CLASS(cls) \
                (G_TYPE_CHECK_CLASS_CAST ((cls), \
                 PUI_TYPE_ACCOUNT_MODEL, \
                 PuiAccountModelClass))
#define PUI_IS_ACCOUNT_MODEL(obj) \
                (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                 PUI_TYPE_ACCOUNT_MODEL))
#define PUI_IS_ACCOUNT_MODEL_CLASS(obj) \
                (G_TYPE_CHECK_CLASS_TYPE ((obj), \
                 PUI_TYPE_ACCOUNT_MODEL))
#define PUI_ACCOUNT_MODEL_GET_CLASS(obj) \
                (G_TYPE_INSTANCE_GET_CLASS ((obj), \
                 PUI_TYPE_ACCOUNT_MODEL, \
                 PuiAccountModelClass))

struct _PuiAccountModel
{
  GtkListStore parent;
};

typedef struct _PuiAccountModel PuiAccountModel;

struct _PuiAccountModelClass
{
  GtkListStoreClass parent_class;
};

typedef struct _PuiAccountModelClass PuiAccountModelClass;

GType
pui_account_model_get_type(void) G_GNUC_CONST;

G_END_DECLS

#endif /* __PUI_ACCOUNT_MODEL_H_INCLUDED__ */
