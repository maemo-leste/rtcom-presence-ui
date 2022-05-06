/*
 * pui-main-view.h
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

#ifndef __PUI_MAIN_VIEW_H_INCLUDED__
#define __PUI_MAIN_VIEW_H_INCLUDED__

#include <gtk/gtk.h>

#include "pui-master.h"

G_BEGIN_DECLS

#define PUI_TYPE_MAIN_VIEW \
                (pui_main_view_get_type ())
#define PUI_MAIN_VIEW(obj) \
                (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                 PUI_TYPE_MAIN_VIEW, \
                 PuiMainView))
#define PUI_MAIN_VIEW_CLASS(cls) \
                (G_TYPE_CHECK_CLASS_CAST ((cls), \
                 PUI_TYPE_MAIN_VIEW, \
                 PuiMainViewClass))
#define PUI_IS_MAIN_VIEW(obj) \
                (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                 PUI_TYPE_MAIN_VIEW))
#define PUI_IS_MAIN_VIEW_CLASS(obj) \
                (G_TYPE_CHECK_CLASS_TYPE ((obj), \
                 PUI_TYPE_MAIN_VIEW))
#define PUI_MAIN_VIEW_GET_CLASS(obj) \
                (G_TYPE_INSTANCE_GET_CLASS ((obj), \
                 PUI_TYPE_MAIN_VIEW, \
                 PuiMainViewClass))

struct _PuiMainView
{
  GtkDialog parent;
};

typedef struct _PuiMainView PuiMainView;

struct _PuiMainViewClass
{
  GtkDialogClass parent_class;
};

typedef struct _PuiMainViewClass PuiMainViewClass;

GType
pui_main_view_get_type(void) G_GNUC_CONST;

PuiMainView *
pui_main_view_new(PuiMaster *master);

void
pui_main_view_run(PuiMainView *main_view);

G_END_DECLS

#endif /* __PUI_MAIN_VIEW_H_INCLUDED__ */

