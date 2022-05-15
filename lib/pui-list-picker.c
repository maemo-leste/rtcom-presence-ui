/*
 * pui-list-picker.c
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

#include "pui-list-picker.h"

G_DEFINE_TYPE(
  PuiListPicker,
  pui_list_picker,
  GTK_TYPE_DIALOG
);

enum
{
  PROP_ITEMS = 1
};

#if 0
static void
pui_list_picker_finalize(GObject *object)
{
  G_OBJECT_CLASS(pui_list_picker_parent_class)->finalize(object);
}

static void
pui_list_picker_dispose(GObject *object)
{
  G_OBJECT_CLASS(pui_list_picker_parent_class)->dispose(object);
}

#endif

static void
on_button_clicked(GtkButton *button, gpointer user_data)
{
  GtkWidget *dialog = gtk_widget_get_ancestor(GTK_WIDGET(button),
                                              GTK_TYPE_DIALOG);

  g_return_if_fail(dialog != NULL);

  gtk_dialog_response(GTK_DIALOG(dialog), GPOINTER_TO_INT(user_data));
}

static void
pui_list_picker_set_property(GObject *object, guint property_id,
                             const GValue *value, GParamSpec *pspec)
{
  switch (property_id)
  {
    case PROP_ITEMS:
    {
      GtkBox *vbox = GTK_BOX(GTK_DIALOG(object)->vbox);
      const gchar **items;
      int idx = 1;

      for (items = g_value_get_boxed(value); *items; items++)
      {
        GtkWidget *button = gtk_button_new_with_label(_(*items));

        gtk_widget_show(button);
        gtk_box_pack_start(vbox, button, FALSE, FALSE, 8);
        g_signal_connect(button, "clicked",
                         G_CALLBACK(on_button_clicked), GINT_TO_POINTER(idx));
        idx++;
      }
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
pui_list_picker_class_init(PuiListPickerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

#if 0
  object_class->dispose = pui_list_picker_dispose;
  object_class->finalize = pui_list_picker_finalize;
#endif

  object_class->set_property = pui_list_picker_set_property;

  g_object_class_install_property(
    object_class, PROP_ITEMS,
    g_param_spec_boxed(
      "items",
      "Items",
      "Items",
      G_TYPE_STRV,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));
}

static void
pui_list_picker_init(PuiListPicker *self)
{}
