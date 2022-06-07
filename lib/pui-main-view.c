/*
 * pui-main-view.c
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
#include <rtcom-accounts-ui-client/client.h>

#include "pui-account-view.h"
#include "pui-profile-editor.h"

#include "pui-main-view.h"

struct _PuiMainViewPrivate
{
  PuiMaster *master;
  AuicClient *auic;
  GtkWidget *hbox;
  GtkWidget *entry;
  GtkWidget *location_picker;
  GtkWidget *online_button;
  GtkWidget *busy_button;
  GtkWidget *offline_button;
  PuiProfile *active_profile;
  PuiLocationLevel location_level;
  GtkWidget *table;
  GtkWidget *first_button;
  gint profile_buttons_count;
  gboolean connecting : 1;
  GtkWidget *new_status_button;
  GtkWidget *edit_status_button;
  GtkWidget *vbox;
};

typedef struct _PuiMainViewPrivate PuiMainViewPrivate;

#define PRIVATE(view) \
  ((PuiMainViewPrivate *) \
   pui_main_view_get_instance_private((PuiMainView *)(view)))

G_DEFINE_TYPE_WITH_PRIVATE(
  PuiMainView,
  pui_main_view,
  GTK_TYPE_DIALOG
);

enum
{
  PROP_MASTER = 1
};

static const char *const location_levels[] =
{
  "pres_fi_location_level_street",
  "pres_fi_location_level_district",
  "pres_fi_location_level_city",
  "pres_fi_location_level_none",
  NULL
};

static gboolean rc_parsed = FALSE;

static void
update_new_status_button_visibility(PuiMainViewPrivate *priv)
{
  if (priv->profile_buttons_count > 5)
    gtk_widget_hide(priv->new_status_button);
  else
    gtk_widget_show(priv->new_status_button);
}

static void
update_buttons_visibility(PuiMainView *view)
{
  PuiMainViewPrivate *priv = PRIVATE(view);
  gboolean no_sip_in_profile;

  pui_master_scan_profile(priv->master, priv->active_profile,
                          &no_sip_in_profile, NULL);

  if (!no_sip_in_profile)
  {
    gtk_widget_hide(priv->hbox);

    if (priv->active_profile->builtin)
      gtk_widget_hide(priv->edit_status_button);
    else
      gtk_widget_show(priv->edit_status_button);
  }
  else
  {
    gtk_widget_show(priv->hbox);

    if (!priv->active_profile->builtin)
      gtk_widget_show(priv->edit_status_button);
    else
      gtk_widget_hide(priv->edit_status_button);
  }
}

static void
set_active_profile(PuiMainView *view, PuiProfile *profile)
{
  PRIVATE(view)->active_profile = profile;
  update_buttons_visibility(view);
}

static void
on_button_clicked(GtkWidget *button, PuiMainView *view)
{
  if (gtk_toggle_button_get_active((GtkToggleButton *)button))
  {
    PuiProfile *profile = g_object_get_data(G_OBJECT(button), "puiprofile");

    g_return_if_fail(profile != NULL);

    set_active_profile(view, profile);
  }
}

static void
on_button_size_request(GtkWidget *widget, GtkRequisition *requisition,
                       gpointer user_data)
{
  requisition->width = 0;
}

static void
hack_fix_button(GtkWidget *button)
{
  GtkWidget *alignment;
  GtkWidget *hbox;
  GList *children;
  GList *l;

  alignment = gtk_bin_get_child(GTK_BIN(button));

  g_return_if_fail(GTK_IS_ALIGNMENT(alignment));

  hbox = gtk_bin_get_child(GTK_BIN(alignment));

  g_return_if_fail(GTK_IS_BOX(hbox));

  children = gtk_container_get_children(GTK_CONTAINER(hbox));

  for (l = children; l; l = l->next)
  {
    if (GTK_IS_LABEL(l->data))
    {
      gtk_box_set_child_packing(
        GTK_BOX(hbox), l->data, TRUE, TRUE, 0, GTK_PACK_END);
    }
  }

  g_list_free(children);
}

static GtkWidget *
create_profile_button(PuiMainView *view, PuiProfile *profile)
{
  PuiMainViewPrivate *priv = PRIVATE(view);
  const gchar *label;
  GtkWidget *button;
  GdkPixbuf *icon;

  if (profile->builtin)
    label = _(profile->name);
  else
    label = profile->name;

  button = gtk_radio_button_new_with_label_from_widget(
      GTK_RADIO_BUTTON(priv->first_button), label);
  gtk_button_set_alignment(GTK_BUTTON(button), 0.0, 0.5);
  gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(button), FALSE);
  g_object_set_data(G_OBJECT(button), "puiprofile", profile);
  g_signal_connect(button, "toggled",
                   G_CALLBACK(on_button_clicked), view);
  g_signal_connect(button, "clicked",
                   G_CALLBACK(on_button_clicked), view);
  g_signal_connect(button, "size-request",
                   G_CALLBACK(on_button_size_request), NULL);
  hildon_gtk_widget_set_theme_size(button, HILDON_SIZE_FINGER_HEIGHT);
  gtk_widget_show(button);

  icon = pui_master_get_profile_icon(priv->master, profile);

  if (icon)
  {
    GtkWidget *image = gtk_image_new_from_pixbuf(icon);
    gtk_button_set_image(GTK_BUTTON(button), image);
    gtk_widget_show(image);
  }

  hack_fix_button(button);

  return button;
}

static void
on_screen_state_changed(PuiMaster *master, gboolean is_on, PuiMainView *view)
{
  PuiMainViewPrivate *priv = PRIVATE(view);

  if (priv->connecting)
  {
    if (is_on)
    {
      if (!gtk_widget_get_realized(GTK_WIDGET(view)))
        return;
    }

    hildon_gtk_window_set_progress_indicator(&view->parent.window, is_on);
  }
}

static void
on_location_value_changed(HildonPickerButton *button, PuiMainView *view)
{
  PRIVATE(view)->location_level = hildon_picker_button_get_active(button);
}

static void
on_button_size_allocate(GtkWidget *widget, GdkRectangle *allocation,
                        PuiMainView *view)
{
  g_object_set(PRIVATE(view)->location_picker,
               "width-request", allocation->width,
               NULL);
}

static void
on_profile_created(PuiMaster *master, PuiProfile *profile, PuiMainView *view)
{
  PuiMainViewPrivate *priv = PRIVATE(view);
  GtkWidget *button = create_profile_button(view, profile);

  gtk_table_attach_defaults(GTK_TABLE(priv->table), button,
                            priv->profile_buttons_count % 3,
                            priv->profile_buttons_count % 3 + 1,
                            priv->profile_buttons_count / 3 + 1,
                            priv->profile_buttons_count / 3 + 2);
  priv->profile_buttons_count++;
  update_new_status_button_visibility(priv);
}

static GtkWidget *
find_profile_button(PuiMainView *view, PuiProfile *profile)
{
  PuiMainViewPrivate *priv = PRIVATE(view);
  GList *l;

  for (l = GTK_TABLE(priv->table)->children; l; l = l->next)
  {
    GtkTableChild *child = l->data;

    if (g_object_get_data(G_OBJECT(child->widget), "puiprofile") == profile)
      return child->widget;
  }

  return NULL;
}

static void
on_profile_changed(PuiMaster *master, PuiProfile *profile, PuiMainView *view)
{
  PuiMainViewPrivate *priv = PRIVATE(view);
  GtkWidget *button = find_profile_button(view, profile);
  GdkPixbuf *icon;

  g_return_if_fail(button != NULL);

  gtk_button_set_label(GTK_BUTTON(button), profile->name);
  icon = pui_master_get_profile_icon(priv->master, profile);

  if (icon)
  {
    GtkWidget *image = gtk_image_new_from_pixbuf(icon);

    gtk_button_set_image(GTK_BUTTON(button), image);
    gtk_widget_show(image);
  }

  hack_fix_button(button);
  update_buttons_visibility(view);
}

static gint
sort_profile_buttons(gconstpointer a, gconstpointer b)
{
  const GtkTableChild *ca = a;
  const GtkTableChild *cb = b;

  if (ca->top_attach == cb->top_attach)
    return ca->left_attach - cb->left_attach;
  else
    return ca->top_attach - cb->top_attach;
}

static void
remove_profile_button(PuiMainView *view, GtkWidget *button,
                      gint top_attach, gint left_attach)
{
  PuiMainViewPrivate *priv = PRIVATE(view);
  GList *l;
  GList *sorted;

  gtk_container_remove(GTK_CONTAINER(priv->table), button);
  sorted = g_list_sort(g_list_copy(GTK_TABLE(priv->table)->children),
                       sort_profile_buttons);
  priv->profile_buttons_count--;

  for (l = sorted; l; l = l->next)
  {
    GtkTableChild *child = l->data;

    if ((child->top_attach >= top_attach) && (child->left_attach > left_attach))
      break;
  }

  for (; l; l = l->next)
  {
    GtkTableChild *child = l->data;
    GtkWidget *widget = child->widget;

    g_object_ref(widget);
    gtk_container_remove(GTK_CONTAINER(priv->table), widget);
    gtk_table_attach_defaults(GTK_TABLE(priv->table), widget,
                              left_attach, left_attach + 1,
                              top_attach, top_attach + 1);
    g_object_unref(widget);

    left_attach++;

    if (left_attach > 2)
    {
      left_attach = 0;
      top_attach++;
    }
  }

  g_list_free(sorted);
}

static void
on_profile_deleted(PuiMaster *master, PuiProfile *profile, PuiMainView *view)
{
  PuiMainViewPrivate *priv = PRIVATE(view);
  GtkWidget *profile_button = find_profile_button(view, profile);
  GList *l;

  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(profile_button)))
  {
    GtkWidget *button =
      find_profile_button(view, pui_master_get_active_profile(master));

    if (button)
    {
      set_active_profile(view, profile);
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
    }
  }

  for (l = GTK_TABLE(priv->table)->children; l; l = l->next)
  {
    GtkTableChild *child = l->data;

    if (child->top_attach)
    {
      if (g_object_get_data(G_OBJECT(child->widget), "puiprofile") == profile)
      {
        remove_profile_button(view, child->widget,
                              child->top_attach, child->left_attach);
        update_new_status_button_visibility(priv);

        if (!(priv->profile_buttons_count % 3))
        {
          gtk_table_resize(GTK_TABLE(priv->table),
                           priv->profile_buttons_count / 3 + 1, 3);
        }

        break;
      }
    }
  }
}

static void
on_vbox_size_request(GtkWidget *widget, GtkRequisition *requisition,
                     PuiMainView *view)
{
  gtk_widget_queue_resize(GTK_WIDGET(view));
}

static void
on_row_activated(GtkTreeView *tree_view, GtkTreePath *path,
                 GtkTreeViewColumn *column, PuiMainView *view)
{
  PuiMainViewPrivate *priv = PRIVATE(view);
  GtkTreeIter iter;

  if (priv->auic)
  {
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);

    if (gtk_tree_model_get_iter(model, &iter, path))
    {
      TpAccount *account = NULL;

      gtk_tree_model_get(model, &iter,
                         COLUMN_ACCOUNT, &account,
                         -1);

      if (account)
      {
        GHashTable *parameters;
        const gchar *user_name;
        gchar *service;

        parameters = (GHashTable *)tp_account_get_parameters(account);
        user_name = tp_asv_get_string(parameters, "account");
        service = g_strdup_printf("%s/%s",
                                  tp_account_get_cm_name(account),
                                  tp_account_get_protocol_name(account));
        auic_client_open_edit_account(priv->auic, service, user_name);
        g_free(service);
        g_object_unref(account);
      }
      else
        auic_client_open_accounts_list(priv->auic);
    }
  }
}

static void
on_row_deleted(GtkTreeModel *tree_model, GtkTreePath *path, PuiMainView *view)
{
  if (gtk_tree_model_iter_n_children(tree_model, NULL) == 1)
    gtk_dialog_response(GTK_DIALOG(view), GTK_RESPONSE_CLOSE);
}

static void
on_presence_changed(PuiMaster *master, TpConnectionPresenceType type,
                    const gchar *status_message, guint status,
                    PuiMainView *view)
{
  PuiMainViewPrivate *priv = PRIVATE(view);
  gboolean connecting = !!(status & PUI_MASTER_STATUS_CONNECTING);

  if (priv->connecting != connecting)
  {
    priv->connecting = connecting;

    if (gtk_widget_get_mapped(GTK_WIDGET(view)) &&
        pui_master_get_display_on(priv->master))
    {
      hildon_gtk_window_set_progress_indicator(GTK_WINDOW(view), connecting);
    }
  }
}

static void
on_presence_support(PuiMaster *master, gboolean supported, PuiMainView *view)
{
  PuiMainViewPrivate *priv = PRIVATE(view);
  int left_attach;
  int right_attach;

  if (supported)
  {
    gtk_widget_show(priv->busy_button);
    left_attach = 2;
    right_attach = 3;
  }
  else
  {
    gtk_widget_hide(priv->busy_button);
    left_attach = 1;
    right_attach = 2;
  }

  gtk_container_child_set(GTK_CONTAINER(priv->table), priv->offline_button,
                          "left-attach", left_attach,
                          "right-attach", right_attach,
                          NULL);
}

static GObject *
pui_main_view_constructor(GType type, guint n_construct_properties,
                          GObjectConstructParam *construct_properties)
{
  GObject *object = G_OBJECT_CLASS(pui_main_view_parent_class)->constructor(
      type, n_construct_properties, construct_properties);
  PuiMainView *view = PUI_MAIN_VIEW(object);
  PuiMainViewPrivate *priv;
  const gchar *presence_message;
  const char *const *msgid;
  GtkWidget *selector;
  GList *profiles;
  GtkWidget *profile_button;
  int *pidx;
  PuiAccountView *account_view;
  GtkWidget *viewport;
  GtkWidget *pannable_area;
  PuiProfile *active_profile;
  guint top;
  guint bottom;
  int idx = 0;
  int builtin_idx = 0;
  guint status;

  g_return_val_if_fail(view != NULL, NULL);

  priv = PRIVATE(view);

  priv->location_level = pui_master_get_location_level(priv->master);
  g_signal_connect(priv->master, "screen-state-changed",
                   G_CALLBACK(on_screen_state_changed), view);

  priv->vbox = gtk_vbox_new(FALSE, 0);
  gtk_widget_show(priv->vbox);

  priv->entry = hildon_entry_new(HILDON_SIZE_FINGER_HEIGHT);
  gtk_widget_show(priv->entry);
  hildon_entry_set_placeholder(
    HILDON_ENTRY(priv->entry),
    pui_master_get_default_presence_message(priv->master));

  presence_message = pui_master_get_presence_message(priv->master);

  if (!presence_message)
    presence_message = "";

  hildon_entry_set_text(HILDON_ENTRY(priv->entry), presence_message);

  selector = hildon_touch_selector_new_text();

  msgid = location_levels;

  while (*msgid)
  {
    hildon_touch_selector_append_text(HILDON_TOUCH_SELECTOR(selector),
                                      _(*msgid));
    msgid++;
  }

  priv->location_picker = g_object_new(
      HILDON_TYPE_PICKER_BUTTON,
      "arrangement", HILDON_BUTTON_ARRANGEMENT_VERTICAL,
      "size", HILDON_SIZE_FINGER_HEIGHT,
      "title", _("pres_bd_location"),
      "value", _(location_levels[priv->location_level]),
      "touch-selector", selector,
      NULL);
  gtk_button_set_alignment(GTK_BUTTON(priv->location_picker), 0.0, 0.5);
  hildon_picker_button_set_active(HILDON_PICKER_BUTTON(priv->location_picker),
                                  priv->location_level);
  g_signal_connect(priv->location_picker, "value-changed",
                   G_CALLBACK(on_location_value_changed), view);
  gtk_widget_show(priv->location_picker);

  priv->hbox = gtk_hbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(priv->hbox), priv->entry, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(priv->hbox), priv->location_picker,
                     FALSE, FALSE, 0);

  priv->table = gtk_table_new(1, 3, TRUE);
  active_profile = pui_master_get_active_profile(priv->master);
  set_active_profile(view, active_profile);

  for (profiles = pui_master_get_profiles(priv->master); profiles;
       profiles = profiles->next)
  {
    PuiProfile *profile = profiles->data;

    if (profile->builtin)
    {
      pidx = &builtin_idx;
      top = 0;
      bottom = 1;
    }
    else
    {
      pidx = &idx;
      top = priv->profile_buttons_count / 3 + 1;
      bottom = priv->profile_buttons_count / 3 + 2;
      priv->profile_buttons_count++;
    }

    profile_button = create_profile_button(view, profile);

    if (!priv->first_button)
    {
      priv->first_button = profile_button;
      g_signal_connect(profile_button, "size-allocate",
                       G_CALLBACK(on_button_size_allocate), view);
    }

    if (profile->builtin)
    {
      if (!*pidx)
        priv->online_button = profile_button;
      else if (*pidx == 1)
        priv->busy_button = profile_button;
      else if (*pidx == 2)
        priv->offline_button = profile_button;
    }

    gtk_table_attach_defaults(GTK_TABLE(priv->table), profile_button,
                              *pidx % 3, *pidx % 3 + 1, top, bottom);
    (*pidx)++;

    if (active_profile == profile)
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(profile_button), TRUE);
  }

  update_new_status_button_visibility(priv);

  g_signal_connect(priv->master, "profile-created",
                   G_CALLBACK(on_profile_created), view);
  g_signal_connect(priv->master, "profile-changed",
                   G_CALLBACK(on_profile_changed), view);
  g_signal_connect(priv->master, "profile-deleted",
                   G_CALLBACK(on_profile_deleted), view);

  gtk_widget_show(priv->table);

  gtk_box_pack_start(GTK_BOX(priv->vbox), priv->table, FALSE, FALSE, 0);
  gtk_box_pack_start((GtkBox *)priv->vbox, priv->hbox, FALSE, FALSE, 0);

  account_view = g_object_new(PUI_TYPE_ACCOUNT_VIEW,
                              "master", priv->master,
                              NULL);
  gtk_widget_set_name(GTK_WIDGET(account_view),
                      "presence-ui::main-view::accounts-tree-view");

  if (!rc_parsed)
  {
    rc_parsed = TRUE;
    gtk_rc_parse_string(
      "widget \"*.presence-ui::main-view::accounts-tree-view\" style \"fremantle-touchlist\"");
  }

  g_signal_connect(account_view, "row-activated",
                   G_CALLBACK(on_row_activated), view);
  gtk_widget_show(GTK_WIDGET(account_view));
  gtk_box_pack_start(GTK_BOX(priv->vbox), GTK_WIDGET(account_view),
                     FALSE, FALSE, 0);
  gtk_widget_grab_focus(GTK_WIDGET(account_view));

  viewport = g_object_new(GTK_TYPE_VIEWPORT, NULL);
  gtk_widget_set_size_request(priv->vbox, 1, -1);
  gtk_container_add(GTK_CONTAINER(viewport), priv->vbox);
  gtk_widget_show(viewport);

  pannable_area = g_object_new(HILDON_TYPE_PANNABLE_AREA,
                               "hscrollbar-policy", GTK_POLICY_NEVER,
                               NULL);
  gtk_container_add(GTK_CONTAINER(pannable_area), viewport);
  gtk_widget_show(pannable_area);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(view)->vbox), pannable_area,
                     FALSE, FALSE, 0);

  g_signal_connect(priv->master, "presence-changed",
                   G_CALLBACK(on_presence_changed), view);

  pui_master_get_global_presence(priv->master, NULL, NULL, &status);
  priv->connecting = !!(status & PUI_MASTER_STATUS_CONNECTING);

  g_signal_connect(priv->master, "presence-support",
                   G_CALLBACK(on_presence_support), view);

  on_presence_support(priv->master,
                      pui_master_is_presence_supported(priv->master), view);

  g_signal_connect(priv->vbox, "size-request",
                   G_CALLBACK(on_vbox_size_request), view);
  g_signal_connect(pui_master_get_model(priv->master), "row-deleted",
                   G_CALLBACK(on_row_deleted), view);

  gtk_window_set_resizable(GTK_WINDOW(view), FALSE);

  return object;
}

static void
pui_main_view_dispose(GObject *object)
{
  PuiMainViewPrivate *priv = PRIVATE(object);

  if (priv->auic)
  {
    g_object_unref(priv->auic);
    priv->auic = NULL;
  }

  if (priv->master)
  {
    g_signal_handlers_disconnect_matched(
      priv->master, G_SIGNAL_MATCH_DATA | G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
      on_presence_support, object);
    g_signal_handlers_disconnect_matched(
      priv->master, G_SIGNAL_MATCH_DATA | G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
      on_presence_changed, object);
    g_signal_handlers_disconnect_matched(
      priv->master, G_SIGNAL_MATCH_DATA | G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
      on_profile_created, object);
    g_signal_handlers_disconnect_matched(
      priv->master, G_SIGNAL_MATCH_DATA | G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
      on_profile_changed, object);
    g_signal_handlers_disconnect_matched(
      priv->master, G_SIGNAL_MATCH_DATA | G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
      on_profile_deleted, object);
    g_signal_handlers_disconnect_matched(
      pui_master_get_model(priv->master),
      G_SIGNAL_MATCH_DATA | G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
      on_row_deleted, object);
    g_signal_handlers_disconnect_matched(
      priv->master, G_SIGNAL_MATCH_DATA | G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
      on_screen_state_changed, object);
    g_object_unref(priv->master);
    priv->master = NULL;
  }

  G_OBJECT_CLASS(pui_main_view_parent_class)->dispose(object);
}

static void
pui_main_view_set_property(GObject *object, guint property_id,
                           const GValue *value, GParamSpec *pspec)
{
  switch (property_id)
  {
    case PROP_MASTER:
    {
      PuiMainViewPrivate *priv = PRIVATE(object);

      g_assert(priv->master == NULL);
      priv->master = g_value_dup_object(value);
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
pui_main_view_map(GtkWidget *widget)
{
  PuiMainViewPrivate *priv = PRIVATE(widget);

  hildon_gtk_window_set_progress_indicator(GTK_WINDOW(widget),
                                           priv->connecting);

  GTK_WIDGET_CLASS(pui_main_view_parent_class)->map(widget);
}

static void
pui_main_view_realize(GtkWidget *widget)
{
  PuiMainViewPrivate *priv = PRIVATE(widget);

  GTK_WIDGET_CLASS(pui_main_view_parent_class)->realize(widget);

  priv->auic = auic_client_new(GTK_WINDOW(widget));
}

static void
pui_main_view_size_request(GtkWidget *widget, GtkRequisition *requisition)
{
  PuiMainViewPrivate *priv = PRIVATE(widget);
  GtkWidget *parent;
  GtkRequisition req;

  gtk_widget_size_request(priv->vbox, &req);
  parent = gtk_widget_get_ancestor(priv->vbox, HILDON_TYPE_PANNABLE_AREA);

  if (req.height >= 350)
    req.height = 350;

  g_object_set(parent, "height-request", req.height, NULL);

  GTK_WIDGET_CLASS(pui_main_view_parent_class)->size_request(
    widget, requisition);
}

static void
pui_main_view_class_init(PuiMainViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

  object_class->constructor = pui_main_view_constructor;
  object_class->dispose = pui_main_view_dispose;
  object_class->set_property = pui_main_view_set_property;

  widget_class->map = pui_main_view_map;
  widget_class->realize = pui_main_view_realize;
  widget_class->size_request = pui_main_view_size_request;

  g_object_class_install_property(
    object_class, PROP_MASTER,
    g_param_spec_object(
      "master", "master", "master",
      PUI_TYPE_MASTER, G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));
}

static gboolean
pui_main_view_activate_profile(PuiMainView *view, PuiProfile *profile)
{
  PuiMainViewPrivate *priv = PRIVATE(view);
  const char *presence_message;
  gboolean is_not_sip;

  presence_message = hildon_entry_get_text((HildonEntry *)priv->entry);

  if (!presence_message)
    presence_message = "";

  pui_master_scan_profile(priv->master, profile, &is_not_sip, NULL);

  if (is_not_sip)
    pui_master_set_location_level(priv->master, priv->location_level);

  pui_master_set_presence_message(priv->master, presence_message);
  pui_master_activate_profile(priv->master, profile);
  pui_master_save_config(priv->master);

  return TRUE;
}

static void
on_new_status_button_clicked(GtkWidget *button, PuiMainView *view)
{
  pui_profile_editor_run_new(PRIVATE(view)->master, GTK_WINDOW(view));
}

static void
on_edit_status_button_clicked(GtkWidget *button, PuiMainView *view)
{
  PuiMainViewPrivate *priv = PRIVATE(view);
  PuiProfile *active_profile = priv->active_profile;

  if (!active_profile->builtin)
  {
    PuiProfile *profile;

    pui_profile_editor_run_edit(priv->master, GTK_WINDOW(view), active_profile);
    profile = pui_master_get_active_profile(priv->master);

    if (profile == priv->active_profile)
      pui_main_view_activate_profile(view, profile);
  }
}

static void
pui_main_view_init(PuiMainView *view)
{
  PuiMainViewPrivate *priv = PRIVATE(view);

  priv->edit_status_button =
    gtk_button_new_with_label(_("pres_bd_presence_personalise"));
  g_signal_connect(priv->edit_status_button, "size-request",
                   G_CALLBACK(on_button_size_request), NULL);
  hildon_gtk_widget_set_theme_size(priv->edit_status_button,
                                   HILDON_SIZE_FINGER_HEIGHT);
  g_signal_connect(priv->edit_status_button, "clicked",
                   G_CALLBACK(on_edit_status_button_clicked), view);
  gtk_container_add(GTK_CONTAINER(GTK_DIALOG(view)->action_area),
                    priv->edit_status_button);

  priv->new_status_button =
    gtk_button_new_with_label(_("pres_bd_presence_new_status"));
  g_signal_connect(priv->new_status_button, "size-request",
                   G_CALLBACK(on_button_size_request), NULL);
  hildon_gtk_widget_set_theme_size(priv->new_status_button,
                                   HILDON_SIZE_FINGER_HEIGHT);
  g_signal_connect(priv->new_status_button, "clicked",
                   G_CALLBACK(on_new_status_button_clicked), view);
  gtk_container_add(GTK_CONTAINER(GTK_DIALOG(view)->action_area),
                    priv->new_status_button);

  gtk_widget_set_size_request(view->parent.action_area, 174, -1);
  gtk_dialog_set_has_separator(GTK_DIALOG(view), FALSE);

  gtk_dialog_add_buttons(
    GTK_DIALOG(view),
    dgettext("hildon-libs", "wdgt_bd_save"), GTK_RESPONSE_OK,
    "gtk-cancel", GTK_RESPONSE_CANCEL,
    NULL);
}

PuiMainView *
pui_main_view_new(PuiMaster *master)
{
  return g_object_new(PUI_TYPE_MAIN_VIEW,
                      "master", master,
                      "title", _("pres_ti_set_presence_title"),
                      NULL);
}

void
pui_main_view_run(PuiMainView *main_view)
{
  PuiMainViewPrivate *priv = PRIVATE(main_view);

  while (gtk_dialog_run(&main_view->parent) == GTK_RESPONSE_OK)
  {
    if (pui_main_view_activate_profile(main_view, priv->active_profile))
      break;
  }
}
