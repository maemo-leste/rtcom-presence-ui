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
  int profile_buttons_count;
  int flags;
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

  if (priv->flags & 1)
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

  g_return_val_if_fail( view != NULL, NULL);

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
                       G_CALLBACK(on_button_size_allocate), priv);
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
  priv->flags = (priv->flags & ~1) | ((status & 4) != 0);

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

  hildon_gtk_window_set_progress_indicator(GTK_WINDOW(widget), priv->flags & 1);

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

PuiMainView *
pui_main_view_new(PuiMaster *master)
{
  return g_object_new(PUI_TYPE_MAIN_VIEW,
                      "master", master,
                      "title", _("pres_ti_set_presence_title"),
                      NULL);
}
