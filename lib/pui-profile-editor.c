/*
 * pui-profile-editor.c
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

#include "pui-profile-editor.h"

struct _PuiProfileAccount
{
  TpAccount *account;
  GdkPixbuf *icon;
};

typedef struct _PuiProfileAccount PuiProfileAccount;

struct _PuiProfilePresence
{
  gchar *title;
  const gchar *status;
};

typedef struct _PuiProfilePresence PuiProfilePresence;

struct _PuiProfileEditorPrivate
{
  PuiMaster *master;
  PuiProfile *profile;
  GtkWidget *name_entry;
  GtkWidget *image;
  GtkWidget *vbox1;
  GtkWidget *vbox2;
  GtkSizeGroup *size_group;
  const gchar *icon;
  gboolean profile_set;
};

typedef struct _PuiProfileEditorPrivate PuiProfileEditorPrivate;

#define PRIVATE(editor) \
  ((PuiProfileEditorPrivate *) \
   pui_profile_editor_get_instance_private((PuiProfileEditor *)(editor)))

G_DEFINE_TYPE_WITH_PRIVATE(
  PuiProfileEditor,
  pui_profile_editor,
  GTK_TYPE_DIALOG
);

enum
{
  PROP_MASTER = 1,
  PROP_PROFILE
};

static void
presences_destroy(GArray *arr)
{
  guint i;

  for (i = 0; i < arr->len; i++)
  {
    PuiProfilePresence *presences = (PuiProfilePresence *)arr->data;

    g_free(presences[i].title);
  }

  g_array_free(arr, TRUE);
}

static gint
profile_account_compare(gconstpointer a, gconstpointer b, gpointer user_data)
{
  PuiProfileAccount *pa = (PuiProfileAccount *)a;
  PuiProfileAccount *pb = (PuiProfileAccount *)b;
  PuiMaster *master = user_data;
  gint rv;

  rv = g_strcmp0(pui_master_get_account_service_name(master, pa->account, NULL),
                 pui_master_get_account_service_name(master, pb->account, NULL));

  if (!rv)
  {
    rv = g_strcmp0(
        pui_master_get_account_display_name(master, pa->account),
        pui_master_get_account_display_name(master, pb->account));
  }

  return rv;
}

static void
on_presence_value_changed(HildonPickerButton *button, GArray *arr)
{
  TpAccount *account = g_object_get_data(G_OBJECT(button), "account");
  PuiProfilePresence *presences = (PuiProfilePresence *)arr->data;
  gint active;
  gchar *presence;

  g_return_if_fail(TP_IS_ACCOUNT(account));

  active = hildon_picker_button_get_active(button);

  if ((active < 0) || (active >= arr->len))
    active = 0;

  presence = g_strdup(presences[active].status);
  g_object_set_data_full(G_OBJECT(button), "presence", presence,
                         (GDestroyNotify)&g_free);
}

static const gchar *
get_presence_title_msgid(const gchar *name)
{
  g_return_val_if_fail(name != NULL, NULL);

  if (!strcmp(name, "offline"))
    return "pres_fi_status_offline";
  else if (!strcmp(name, "available"))
    return "pres_fi_status_online";
  else if (!strcmp(name, "away"))
    return "pres_bd_gtalk_away";
  else if (!strcmp(name, "xa"))
    return "pres_bd_gtalk_busy";
  else if (!strcmp(name, "dnd"))
    return "pres_bd_jabber_do_not_disturb";
  else if (!strcmp(name, "hidden"))
    return "pres_bd_jabber_invisible";
  else
    return NULL;
}

static GObject *
pui_profile_editor_constructor(GType type, guint n_construct_properties,
                               GObjectConstructParam *construct_properties)
{
  GObject *object;
  PuiProfileEditor *editor;
  PuiProfileEditorPrivate *priv;
  PuiProfile *profile;
  GdkPixbuf *icon;
  GtkListStore *model;
  const char *title;
  GList *l;
  GList *accounts = NULL;
  GtkTreeIter iter;

  object = G_OBJECT_CLASS(pui_profile_editor_parent_class)->constructor(
      type, n_construct_properties, construct_properties);

  g_return_val_if_fail(object != NULL, NULL);

  editor = PUI_PROFILE_EDITOR(object);
  priv = PRIVATE(editor);
  profile = priv->profile;

  if (profile)
    hildon_entry_set_text(HILDON_ENTRY(priv->name_entry), profile->name);
  else
  {
    profile = g_slice_new0(PuiProfile);
    priv->profile = profile;
    profile->default_presence = g_strdup("available");
  }

  if (profile->icon)
    priv->icon = profile->icon;
  else
  {
    priv->icon = "general_presence_home";
    profile = priv->profile;
    profile->icon = g_strdup("general_presence_home");
    profile->icon_error = g_strconcat(priv->icon, "_error", NULL);
  }

  icon = pui_master_get_profile_icon(priv->master, profile);

  if (icon)
    gtk_image_set_from_pixbuf(GTK_IMAGE(priv->image), icon);

  model = pui_master_get_model(priv->master);

  if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(model), &iter))
  {
    do
    {
      PuiProfileAccount *pa = g_slice_new(PuiProfileAccount);

      gtk_tree_model_get(GTK_TREE_MODEL(model), &iter,
                         COLUMN_ACCOUNT, &pa->account,
                         -1);

      if (pa->account)
      {
        const char *icon_name = tp_account_get_icon_name(pa->account);

        pa->icon = NULL;

        if (icon_name)
        {
          pa->icon = pui_master_get_icon(priv->master, icon_name,
                                         HILDON_ICON_PIXEL_SIZE_FINGER);
        }

        accounts = g_list_insert_sorted_with_data(
            accounts, pa, profile_account_compare, priv->master);
      }
      else
        g_slice_free(PuiProfileAccount, pa);
    }
    while (gtk_tree_model_iter_next(GTK_TREE_MODEL(model), &iter));

    for (l = accounts; l; l = l->next)
    {
      PuiProfileAccount *pa = l->data;
      TpAccount *account = pa->account;
      GArray *arr = g_array_sized_new(
          FALSE, FALSE, sizeof(PuiProfileAccount), 2);
      TpProtocol *protocol = pui_master_get_account_protocol(
          priv->master, account);
      gint index = -1;
      gint i;
      GtkWidget *selector;
      HildonPickerButton *picker_button;
      const gchar *presence;

      if (protocol &&
          tp_proxy_has_interface_by_id(
            protocol, TP_IFACE_QUARK_PROTOCOL_INTERFACE_PRESENCE))
      {
        GList *presences = tp_protocol_dup_presence_statuses(protocol);
        GList *pres;

        for (pres = presences; pres; pres = pres->next)
        {
          PuiProfilePresence presence;
          const gchar *name = tp_presence_status_spec_get_name(pres->data);
          const gchar *msgid = get_presence_title_msgid(name);

          /* we just ignore stuff we have no idea about, like jabber 'chat'
             status */
          if (msgid)
          {
            presence.title = g_strdup(_(msgid));
            presence.status = g_strdup(name);

            g_array_append_vals(arr, &presence, 1);
          }
        }

        g_list_free_full(presences,
                         (GDestroyNotify)tp_presence_status_spec_free);
      }

      if (!arr->len)
      {
        PuiProfilePresence presences[2];

        g_warning("No presences for account %s",
                  tp_account_get_path_suffix(account));
        presences[0].status = "available";
        presences[0].title = g_strdup(_("pres_bd_sip_online"));
        presences[1].status = "offline";
        presences[1].title = g_strdup(_("pres_bd_sip_offline"));
        g_array_append_vals(arr, presences, 2);
      }

      title = pui_master_get_account_display_name(priv->master, account);

      if (!title || !*title)
        title = tp_account_get_path_suffix(account);

      presence = pui_profile_get_presence(priv->profile, account);
      selector = hildon_touch_selector_new_text();

      for (i = 0; i < arr->len; i++)
      {
        PuiProfilePresence *presences = (PuiProfilePresence *)arr->data;

        if (presence && !strcmp(presence, presences[i].status))
          index = i;

        hildon_touch_selector_append_text(
          HILDON_TOUCH_SELECTOR(selector), presences[i].title);
      }

      gtk_widget_show(GTK_WIDGET(selector));

      picker_button = g_object_new(
          HILDON_TYPE_PICKER_BUTTON,
          "arrangement", HILDON_BUTTON_ARRANGEMENT_VERTICAL,
          "size", HILDON_SIZE_FINGER_HEIGHT,
          "title", title,
          "value", presence,
          "touch-selector", selector,
          NULL);

      if (pa->icon)
      {
        hildon_button_set_image(HILDON_BUTTON(picker_button),
                                gtk_image_new_from_pixbuf(pa->icon));
        hildon_button_set_image_position(HILDON_BUTTON(picker_button),
                                         GTK_POS_LEFT);
      }

      hildon_picker_button_set_active(picker_button, index);
      hildon_button_add_title_size_group(HILDON_BUTTON(picker_button),
                                         priv->size_group);
      gtk_button_set_alignment(&picker_button->parent.parent, 0.0, 0.5);
      g_object_set_data_full(G_OBJECT(picker_button), "account",
                             account, (GDestroyNotify)&g_object_unref);
      g_object_set_data_full(G_OBJECT(picker_button), "presences", arr,
                             (GDestroyNotify)presences_destroy);
      g_signal_connect(picker_button, "value-changed",
                       G_CALLBACK(on_presence_value_changed), arr);
      gtk_widget_show(GTK_WIDGET(picker_button));
      gtk_container_add(GTK_CONTAINER(priv->vbox1), GTK_WIDGET(picker_button));
    }

    for (l = accounts; l; l = l->next)
      g_slice_free(PuiProfileAccount, l->data);
  }

  g_list_free(accounts);

  if (priv->profile_set)
  {
    gtk_dialog_add_button(&editor->parent,
                          dgettext("hildon-libs", "wdgt_bd_delete"), 1);
  }

  gtk_dialog_add_buttons(
    GTK_DIALOG(editor),
    dgettext("hildon-libs", "wdgt_bd_save"), GTK_RESPONSE_OK,
    "gtk-cancel", GTK_RESPONSE_CANCEL,
    NULL);

  return object;
}

static void
pui_profile_editor_dispose(GObject *object)
{
  PuiProfileEditorPrivate *priv = PRIVATE(object);

  if (priv->size_group)
  {
    g_object_unref(priv->size_group);
    priv->size_group = NULL;
  }

  if (priv->master)
  {
    g_object_unref(priv->master);
    priv->master = NULL;
  }

  G_OBJECT_CLASS(pui_profile_editor_parent_class)->dispose(object);
}

static void
pui_profile_editor_finalize(GObject *object)
{
  PuiProfileEditorPrivate *priv = PRIVATE(object);

  if (!priv->profile_set)
  {
    if (priv->profile)
      pui_profile_free(priv->profile);
  }

  G_OBJECT_CLASS(pui_profile_editor_parent_class)->finalize(object);
}

static void
pui_profile_editor_set_property(GObject *object, guint property_id,
                                const GValue *value, GParamSpec *pspec)
{
  PuiProfileEditorPrivate *priv = PRIVATE(object);

  switch (property_id)
  {
    case PROP_MASTER:
    {
      g_assert(priv->master == NULL);
      priv->master = g_value_dup_object(value);
      break;
    }
    case PROP_PROFILE:
    {
      g_assert(priv->profile == NULL);
      priv->profile = g_value_get_pointer(value);

      if (priv->profile)
        priv->profile_set = TRUE;

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
pui_profile_editor_size_request(GtkWidget *widget, GtkRequisition *requisition)
{
  PuiProfileEditorPrivate *priv = PRIVATE(widget);
  GtkWidget *area;
  GtkRequisition r;

  gtk_widget_size_request(priv->vbox2, &r);

  area = gtk_widget_get_ancestor(priv->vbox2, HILDON_TYPE_PANNABLE_AREA);

  if (r.height >= 350)
    r.height = 350;

  r.height = r.height;

  g_object_set(area, "height-request", r.height, NULL);

  GTK_WIDGET_CLASS(pui_profile_editor_parent_class)->size_request(
    widget, requisition);
}

static void
pui_profile_editor_class_init(PuiProfileEditorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->constructor = pui_profile_editor_constructor;
  object_class->dispose = pui_profile_editor_dispose;
  object_class->finalize = pui_profile_editor_finalize;
  object_class->set_property = pui_profile_editor_set_property;

  GTK_WIDGET_CLASS(klass)->size_request = pui_profile_editor_size_request;

  g_object_class_install_property(
    object_class, PROP_MASTER,
    g_param_spec_object(
      "master",
      "Master",
      "Master",
      PUI_TYPE_MASTER,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));

  g_object_class_install_property(
    object_class, PROP_PROFILE,
    g_param_spec_pointer(
      "profile",
      "Profile",
      "Profile",
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));
}

static void
selection_changed_cb(GtkIconView *icon_view, GtkDialog *dialog)
{
  GtkTreePath *path = NULL;

  gtk_icon_view_get_cursor(icon_view, &path, NULL);

  if (path)
  {
    gint idx = *gtk_tree_path_get_indices(path);

    gtk_tree_path_free(path);
    gtk_dialog_response(dialog, idx);
  }
}

static void
button_clicked_cb(GtkWidget *button, PuiProfileEditor *editor)
{
  PuiProfileEditorPrivate *priv = PRIVATE(editor);
  GtkListStore *store = gtk_list_store_new(1, GDK_TYPE_PIXBUF);
  GtkWidget *icon_view;
  GtkDialog *dialog;
  gint response_id;
  GtkTreeIter iter;
  GdkPixbuf *pixbuf;
  static const char *profile_icons[] =
  {
    "general_presence_home",
    "general_presence_work",
    "general_presence_travel",
    "general_presence_sports",
    "general_presence_cultural_activities",
    "general_presence_out",
    NULL
  };
  const char **icon_name = profile_icons;

  while (*icon_name)
  {
    GdkPixbuf *icon = gtk_icon_theme_load_icon(gtk_icon_theme_get_default(),
                                               *icon_name,
                                               HILDON_ICON_PIXEL_SIZE_FINGER,
                                               0, NULL);

    gtk_list_store_insert_with_values(store, NULL, -1, 0, icon, -1);

    if (icon)
      g_object_unref(icon);

    icon_name++;
  }

  icon_view = g_object_new(GTK_TYPE_ICON_VIEW,
                           "model", store,
                           "pixbuf-column", 0,
                           "columns", 6,
                           "column-spacing", 80,
                           NULL);
  gtk_widget_show(icon_view);

  dialog = g_object_new(GTK_TYPE_DIALOG,
                        "title", _("pres_ti_select_icon"),
                        NULL);
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(editor));
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), icon_view,
                     TRUE, TRUE, 0);
  g_signal_connect(icon_view, "selection-changed",
                   G_CALLBACK(selection_changed_cb), dialog);
  response_id = gtk_dialog_run(dialog);
  gtk_widget_destroy(GTK_WIDGET(dialog));

  if (response_id >= 0)
  {
    if (gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(store), &iter, NULL,
                                      response_id))
    {
      gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, 0, &pixbuf, -1);
      gtk_image_set_from_pixbuf(GTK_IMAGE(priv->image), pixbuf);
      g_object_unref(pixbuf);
      priv->icon = profile_icons[response_id];
    }
  }

  g_object_unref(store);
}

void
pui_profile_editor_init(PuiProfileEditor *editor)
{
  PuiProfileEditorPrivate *priv = PRIVATE(editor);
  GtkWidget *hbox;
  GtkWidget *label;
  GtkWidget *button;
  GtkWidget *viewport;
  GtkWidget *area;

  priv->size_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
  hbox = gtk_hbox_new(FALSE, 8);
  gtk_widget_show(hbox);

  label = gtk_label_new(_("pres_fi_new_status_name"));
  gtk_widget_show(label);
  gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 8);
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
  gtk_widget_set_can_focus(label, TRUE);
  gtk_widget_grab_focus(label);

  priv->name_entry = hildon_entry_new(HILDON_SIZE_FINGER_HEIGHT);
  hildon_entry_set_placeholder(HILDON_ENTRY(priv->name_entry),
                               _("pres_fi_personalised_status_name"));
  gtk_widget_show(priv->name_entry);
  gtk_box_pack_start(GTK_BOX(hbox), priv->name_entry, TRUE, TRUE, 4);

  priv->image = gtk_image_new();
  gtk_widget_show(priv->image);

  button = hildon_gtk_button_new(HILDON_SIZE_FINGER_HEIGHT);
  gtk_container_add(GTK_CONTAINER(button), priv->image);
  g_signal_connect(button, "clicked",
                   G_CALLBACK(button_clicked_cb), editor);
  gtk_widget_show(button);
  gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);

  priv->vbox1 = gtk_vbox_new(TRUE, 0);
  gtk_widget_show(priv->vbox1);

  priv->vbox2 = gtk_vbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(priv->vbox2), hbox, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(priv->vbox2), priv->vbox1, TRUE, TRUE, 0);
  gtk_widget_show(priv->vbox2);
  gtk_widget_set_size_request(priv->vbox2, 1, -1);

  viewport = g_object_new(GTK_TYPE_VIEWPORT, NULL);
  gtk_container_add(GTK_CONTAINER(viewport), priv->vbox2);
  gtk_widget_show(viewport);

  area = g_object_new(HILDON_TYPE_PANNABLE_AREA,
                      "hscrollbar-policy", GTK_POLICY_NEVER,
                      NULL);
  gtk_container_add(GTK_CONTAINER(area), viewport);
  gtk_widget_show(area);

  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(editor)->vbox), area, TRUE, TRUE, 0);
  gtk_dialog_set_has_separator(GTK_DIALOG(editor), FALSE);
}

static gboolean
activate_profile(PuiProfileEditor *editor)
{
  PuiProfileEditorPrivate *priv = PRIVATE(editor);
  PuiProfile *profile = priv->profile;
  gchar *name;
  const gchar *err_msg = NULL;
  GList *children;
  GList *l;

  name = g_strstrip(g_strdup(
                      hildon_entry_get_text(HILDON_ENTRY(priv->name_entry))));

  if (name && *name)
  {
    for (l = pui_master_get_profiles(priv->master); l; l = l->next)
    {
      PuiProfile *p = l->data;

      if (priv->profile != p)
      {
        if ((p->builtin && !strcmp(_(p->name), name)) || !strcmp(p->name, name))
        {
          err_msg = _("pres_ni_error_same_status_name");
          break;
        }
      }
    }
  }
  else
    err_msg = _("pres_ni_error_no_name");

  if (err_msg)
  {
    hildon_banner_show_information(GTK_WIDGET(editor), NULL, err_msg);
    g_free(name);
    return FALSE;
  }

  if (!profile->name)
    profile->name = name;
  else if (strcmp(profile->name, name))
  {
    pui_master_erase_profile(priv->master, profile);
    g_free(priv->profile->name);
    profile->name = name;
  }
  else
    g_free(name);

  if (priv->icon != profile->icon)
  {
    g_free(profile->icon);
    g_free(priv->profile->icon_error);
    profile = priv->profile;
    profile = priv->profile;
    profile->icon = g_strdup(priv->icon);
    profile->icon_error = g_strconcat(priv->icon, "_error", NULL);
  }

  children = gtk_container_get_children(GTK_CONTAINER(priv->vbox1));

  for (l = children; l; l = l->next)
  {
    if (GTK_IS_BUTTON(l->data))
    {
      TpAccount *account = g_object_get_data(l->data, "account");
      gchar *presence = g_object_steal_data(l->data, "presence");

      if (account && presence)
        pui_profile_set_account_presence(priv->profile, account, presence);
    }
  }

  g_list_free(children);
  pui_master_save_profile(priv->master, priv->profile);

  priv->profile = NULL;

  return TRUE;
}

void
pui_profile_editor_run_new(PuiMaster *master, GtkWindow *parent)
{
  PuiProfileEditor *editor;

  editor = g_object_new(PUI_TYPE_PROFILE_EDITOR,
                        "master", master,
                        "title", _("pres_ti_new_status"),
                        "transient-for", parent,
                        NULL);

  while (gtk_dialog_run(GTK_DIALOG(editor)) == GTK_RESPONSE_OK)
  {
    if (activate_profile(editor))
      break;
  }

  gtk_widget_destroy(GTK_WIDGET(editor));
}

void
pui_profile_editor_run_edit(PuiMaster *master, GtkWindow *parent,
                            PuiProfile *profile)
{
  PuiProfileEditor *editor;
  gboolean done = FALSE;

  editor = g_object_new(PUI_TYPE_PROFILE_EDITOR,
                        "master", master,
                        "title", _("pres_ti_edit_status"),
                        "transient-for", parent,
                        "profile", profile,
                        NULL);

  while (!done)
  {
    gint response = gtk_dialog_run(GTK_DIALOG(editor));

    if (response == GTK_RESPONSE_OK)
    {
      if (activate_profile(editor))
        done = TRUE;
    }
    else if (response == 1)
    {
      const gchar *fmt = _("pres_nc_delete_status");
      gchar *description = g_strdup_printf(fmt, profile->name);
      GtkWidget *note = hildon_note_new_confirmation(GTK_WINDOW(editor),
                                                     description);

      if (gtk_dialog_run(GTK_DIALOG(note)) == GTK_RESPONSE_OK)
      {
        pui_master_delete_profile(master, profile);
        done = TRUE;
      }

      g_free(description);
      gtk_widget_destroy(note);
    }
    else
      done = TRUE;
  }

  gtk_widget_destroy(GTK_WIDGET(editor));
}
