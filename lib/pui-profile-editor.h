/*
 * pui-profile-editor.h
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

#ifndef __PUI_PROFILE_EDITOR_H_INCLUDED__
#define __PUI_PROFILE_EDITOR_H_INCLUDED__

#include "pui-master.h"

G_BEGIN_DECLS

#define PUI_TYPE_PROFILE_EDITOR \
                (pui_profile_editor_get_type ())
#define PUI_PROFILE_EDITOR(obj) \
                (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                 PUI_TYPE_PROFILE_EDITOR, \
                 PuiProfileEditor))
#define PUI_PROFILE_EDITOR_CLASS(cls) \
                (G_TYPE_CHECK_CLASS_CAST ((cls), \
                 PUI_TYPE_PROFILE_EDITOR, \
                 PuiProfileEditorClass))
#define PUI_IS_PROFILE_EDITOR(obj) \
                (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                 PUI_TYPE_PROFILE_EDITOR))
#define PUI_IS_PROFILE_EDITOR_CLASS(obj) \
                (G_TYPE_CHECK_CLASS_TYPE ((obj), \
                 PUI_TYPE_PROFILE_EDITOR))
#define PUI_PROFILE_EDITOR_GET_CLASS(obj) \
                (G_TYPE_INSTANCE_GET_CLASS ((obj), \
                 PUI_TYPE_PROFILE_EDITOR, \
                 PuiProfileEditorClass))

struct _PuiProfileEditor
{
  GtkDialog parent;
};

typedef struct _PuiProfileEditor PuiProfileEditor;

struct _PuiProfileEditorClass
{
  GtkDialogClass parent_class;
};

typedef struct _PuiProfileEditorClass PuiProfileEditorClass;

GType
pui_profile_editor_get_type(void) G_GNUC_CONST;

void
pui_profile_editor_run_new(PuiMaster *master, GtkWindow *parent);

void
pui_profile_editor_run_edit(PuiMaster *master, GtkWindow *parent,
                            PuiProfile *profile);

G_END_DECLS

#endif /* __PUI_PROFILE_EDITOR_H_INCLUDED__ */
