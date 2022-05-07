/*
 * pui-location.c
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

#ifdef ENABLE_LOCATION
#include <iphbd/libiphb.h>
#include <location/location-gps-device.h>
#include <location/location-gpsd-control.h>
#include <navigation/navigation-provider.h>
#endif

#include <errno.h>

#include "pui-location.h"

#ifdef ENABLE_LOCATION
struct _iphb_event_source
{
  GSource source;
  GPollFD pollfd;
  iphb_t heartbeat;
  GTimeVal start_time;
  unsigned short mintime;
  unsigned short maxtime;
};

typedef struct _iphb_event_source iphb_event_source;
#endif

struct _PuiLocationPrivate
{
  gboolean disposed;
  PuiLocationLevel level;
  gchar *locations[4];
#ifdef ENABLE_LOCATION
  LocationGPSDControl *gpsd_control;
  LocationGPSDevice *gps_device;
  NavigationProvider *navigation;
  gboolean gpsd_control_started;
  NavigationLocation location;
  double uncertainty;
  double last_uncertainty;
  glong address_time;
  iphb_t iphb;
  iphb_event_source *hb_source;
  unsigned short heartbeat_interval;
  gboolean hb_active;
  gboolean waiting_address;
#endif
};

typedef struct _PuiLocationPrivate PuiLocationPrivate;

#define PRIVATE(self) \
  ((PuiLocationPrivate *) \
   pui_location_get_instance_private((PuiLocation *)(self)))

G_DEFINE_TYPE_WITH_PRIVATE(
  PuiLocation,
  pui_location,
  G_TYPE_OBJECT
);

enum
{
  ERROR,
  ADDRESS_CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

#ifdef ENABLE_LOCATION
static void
pui_location_iphb_start(PuiLocation *self, unsigned short mintime,
                        unsigned short maxtime);

static void
pui_location_dispose(GObject *object)
{
  PuiLocationPrivate *priv = PRIVATE(object);

  if (!priv->disposed)
  {
    priv->disposed = TRUE;
    pui_location_stop(PUI_LOCATION(object));
  }

  G_OBJECT_CLASS(pui_location_parent_class)->dispose(object);
}

static void
pui_location_finalize(GObject *object)
{
  PuiLocationPrivate *priv = PRIVATE(object);
  int i;

  if (priv->iphb)
    iphb_close(priv->iphb);

  for (i = 0; i < G_N_ELEMENTS(priv->locations); i++)
    g_free(priv->locations[i]);

  G_OBJECT_CLASS(pui_location_parent_class)->finalize(object);
}
#endif

static void
pui_location_class_init(PuiLocationClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

#ifdef ENABLE_LOCATION
  object_class->dispose = pui_location_dispose;
  object_class->finalize = pui_location_finalize;
#endif

  signals[ERROR] = g_signal_new(
      "error", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
      G_TYPE_NONE, 1, G_TYPE_UINT);
  signals[ADDRESS_CHANGED] = g_signal_new(
      "address-changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0);
}

static void
pui_location_init(PuiLocation *self)
{
#ifdef ENABLE_LOCATION
  PuiLocationPrivate *priv = PRIVATE(self);
  int hb_int;

  priv->location.latitude = 91.0;
  priv->location.longitude = 181.0;

  priv->gpsd_control_started = FALSE;
  priv->last_uncertainty = 13000000.0;

  if ((priv->iphb = iphb_open(&hb_int)))
    priv->heartbeat_interval = hb_int;
  else
    priv->heartbeat_interval = 0;
#endif
}

#ifdef ENABLE_LOCATION
static GString *
append_string(GString *str, const gchar *sep, const gchar *s)
{
  if (s && *s)
  {
    if (str->len)
    {
      g_string_append(str, sep);
      str = g_string_append(str, s);
    }
    else
      str = g_string_append(str, s);
  }

  return str;
}

static void
location_to_address_cb(NavigationProvider *provider, NavigationAddress *address,
                       gpointer user_data)
{
  PuiLocation *self = user_data;
  PuiLocationPrivate *priv = PRIVATE(self);
  GTimeVal tv;
  int i;

  priv->waiting_address = FALSE;

  if (!address)
  {
    g_warning(
      "null pointer passed to the navigation_provider_location_to_address callback");
    return;
  }

  for (i = 0; i < G_N_ELEMENTS(priv->locations); i++)
  {
    g_free(priv->locations[i]);
    priv->locations[i] = NULL;
  }

  if (priv->last_uncertainty < 30000.0)
  {
    GString *s = g_string_new(address->town);

    append_string(s, ", ", address->municipality);
    append_string(s, ", ", address->province);
    append_string(s, ", ", address->country);

    priv->locations[PUI_LOCATION_LEVEL_CITY] = g_string_free(s, FALSE);

    s = g_string_new(address->suburb);
    append_string(s, ", ", priv->locations[PUI_LOCATION_LEVEL_CITY]);

    priv->locations[PUI_LOCATION_LEVEL_DISTRICT] = g_string_free(s, FALSE);
  }
  else
  {
    const gchar *country = address->country;

    if (!country)
      country = "";

    priv->locations[PUI_LOCATION_LEVEL_CITY] = g_strdup(country);
    priv->locations[PUI_LOCATION_LEVEL_DISTRICT] =
        g_strdup(priv->locations[PUI_LOCATION_LEVEL_CITY]);
  }

  if (priv->last_uncertainty < 500.0)
  {
    GString *s = g_string_new(address->street);

    append_string(s, " ", address->house_num);
    append_string(s, ", ", priv->locations[PUI_LOCATION_LEVEL_DISTRICT]);

    priv->locations[PUI_LOCATION_LEVEL_STREET] = g_string_free(s, FALSE);
  }
  else
  {
    priv->locations[PUI_LOCATION_LEVEL_STREET] =
        g_strdup(priv->locations[PUI_LOCATION_LEVEL_DISTRICT]);
  }

  navigation_address_free(address);

  g_get_current_time(&tv);
  priv->address_time = tv.tv_sec;

  g_signal_emit(self, signals[ADDRESS_CHANGED], 0);

  if (priv->hb_active)
    pui_location_iphb_start(self, 240, 300);
}

static gboolean
on_iphb_event(gpointer user_data)
{
  PuiLocation *self = user_data;
  PuiLocationPrivate *priv = PRIVATE(self);
  GError *error = NULL;

  priv->hb_active = FALSE;

  if (priv->hb_source)
  {
    g_source_unref(&priv->hb_source->source);
    priv->hb_source = NULL;
  }

  if (navigation_provider_location_to_address(priv->navigation, &priv->location,
                                              location_to_address_cb, self,
                                              &error))
  {
    priv->waiting_address = TRUE;
    priv->last_uncertainty = priv->uncertainty;
  }
  else
  {
    const gchar *msg;

    if (error)
      msg = error->message;
    else
      msg = "";

    g_warning("navigation address lookup failed: %s", msg);

    if (error)
      g_error_free(error);
  }

  return FALSE;
}

static void
pui_heartbeat_degrade(iphb_event_source *hb_source)
{
  g_assert(hb_source->heartbeat != NULL);
  g_source_remove_poll(&hb_source->source, &hb_source->pollfd);
  hb_source->heartbeat = NULL;
}

static gboolean
iphb_event_prepare(GSource *source, gint *timeout)
{
  iphb_event_source *hbs = (iphb_event_source *)source;
  gboolean rv = FALSE;
  glong sec_diff;
  glong usec_diff;
  GTimeVal timeval;

  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  g_source_get_current_time(&hbs->source, &timeval);

  G_GNUC_END_IGNORE_DEPRECATIONS
    sec_diff = hbs->start_time.tv_sec - timeval.tv_sec;
  usec_diff = hbs->start_time.tv_usec - timeval.tv_usec;

  if (sec_diff <= 0)
  {
    if ((sec_diff == 0) && (usec_diff > 0))
      *timeout = usec_diff / 1000;
    else
    {
      rv = TRUE;
      *timeout = 0;
    }
  }
  else
    *timeout = (usec_diff / 1000) + (1000 * sec_diff);

  return rv;
}

static gboolean
iphb_event_check(GSource *source)
{
  iphb_event_source *hns = (iphb_event_source *)source;
  unsigned int revents;
  GTimeVal timeval;

  if (hns->heartbeat)
  {
    revents = hns->pollfd.revents;

    if ((revents & (G_IO_HUP | G_IO_ERR)))
    {
      g_warning(
        "heartbeat connection closed prematurely with condition %hu, falling back to timeout with a period of %hu sec",
        revents,
        hns->maxtime);
      pui_heartbeat_degrade(hns);
      return FALSE;
    }

    if (revents & G_IO_IN)
      return TRUE;
  }

  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  g_source_get_current_time(&hns->source, &timeval);

  G_GNUC_END_IGNORE_DEPRECATIONS

  if (timeval.tv_sec > hns->start_time.tv_sec)
    return TRUE;

  if (timeval.tv_sec == hns->start_time.tv_sec)
    return timeval.tv_usec >= hns->start_time.tv_usec;

  return FALSE;
}

static gboolean
iphb_event_dispatch(GSource *source, GSourceFunc callback, gpointer user_data)
{
  iphb_event_source *hbs = (iphb_event_source *)source;
  GTimeVal timeval;

  if (!callback)
  {
    g_critical("callback not set for the heartbeat source");
    return FALSE;
  }

  if (!callback(user_data))
    return FALSE;

  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  g_source_get_current_time(&hbs->source, &timeval);

  G_GNUC_END_IGNORE_DEPRECATIONS
  hbs->start_time.tv_sec = timeval.tv_sec;
  hbs->start_time.tv_usec = timeval.tv_usec;
  g_time_val_add(&hbs->start_time, 1000000 * hbs->maxtime);

  if (hbs->heartbeat &&
      (iphb_wait(hbs->heartbeat, hbs->mintime, hbs->maxtime, 0) < 0))
  {
    g_warning(
      "iphb_wait failed: %s, falling back to timeout with a period of %hu sec",
      strerror(errno),
      hbs->maxtime);
    pui_heartbeat_degrade(hbs);
  }

  return TRUE;
}

static GSourceFuncs iphb_event_funcs =
{
  iphb_event_prepare,
  iphb_event_check,
  iphb_event_dispatch,
  NULL,
  NULL,
  NULL
};

static iphb_event_source *
pui_location_iphb_wait(iphb_t iphbh, unsigned short mintime,
                       unsigned short maxtime)
{
  iphb_event_source *source = (iphb_event_source *)g_source_new(
      &iphb_event_funcs,
      sizeof(
        iphb_event_source));

  source->mintime = mintime;
  source->maxtime = maxtime;
  g_get_current_time(&source->start_time);
  source->heartbeat = iphbh;

  g_time_val_add(&source->start_time, 1000000 * maxtime);

  if (iphbh)
  {
    if (iphb_wait(iphbh, mintime, maxtime, 0) < 0)
    {
      g_warning(
        "iphb_wait failed: %s, falling back to timeout with a period of %hu sec",
        strerror(errno),
        maxtime);
      source->heartbeat = NULL;
    }
    else
    {
      source->pollfd.events = G_IO_IN | G_IO_HUP | G_IO_ERR;
      source->pollfd.fd = iphb_get_fd(iphbh);
      g_source_add_poll(&source->source, &source->pollfd);
    }
  }
  else
  {
    g_warning(
      "heartbeat handle is not valid, falling back to timeout with a period of %hu sec",
      maxtime);
  }

  return source;
}

static void
pui_location_iphb_start(PuiLocation *self, unsigned short mintime,
                        unsigned short maxtime)
{
  PuiLocationPrivate *priv = PRIVATE(self);

  priv->hb_source = pui_location_iphb_wait(priv->iphb, mintime, maxtime);
  g_source_set_callback(&priv->hb_source->source, on_iphb_event, self, NULL);
  g_source_attach(&priv->hb_source->source, NULL);
}

static void
gps_device_changed_cb(LocationGPSDevice *gps_device, PuiLocation *self)
{
  PuiLocationPrivate *priv = PRIVATE(self);
  LocationGPSDeviceFix *fix = gps_device->fix;

  if (!(fix->fields & LOCATION_GPS_DEVICE_LATLONG_SET))
    return;

  if ((priv->location.latitude == fix->latitude) &&
      (priv->location.longitude == fix->longitude))
  {
    return;
  }

  priv->location.latitude = fix->latitude;
  priv->location.longitude = fix->longitude;
  priv->uncertainty = fix->eph * 0.01;

  if (priv->hb_active)
    return;

  priv->hb_active = TRUE;

  if (!priv->waiting_address)
  {
    glong diff = 0;

    if (priv->address_time)
    {
      GTimeVal tv;

      g_get_current_time(&tv);

      diff = tv.tv_sec - priv->address_time;
    }

    if (240 - diff > 0)
      pui_location_iphb_start(self, 240 - diff, 300 - diff);
    else
      pui_location_iphb_start(self, 0, priv->heartbeat_interval);
  }
}

static void
gpsd_control_error_verbose_cb(LocationGPSDControl *control, gint error,
                              PuiLocation *self)
{
  PuiLocationPrivate *priv = PRIVATE(self);

  if ((error == LOCATION_ERROR_USER_REJECTED_DIALOG) ||
      (error == LOCATION_ERROR_USER_REJECTED_SETTINGS))
  {
    priv->gpsd_control_started = TRUE;
  }

  g_signal_emit(self, signals[ERROR], 0, (guint)error);
}

#endif

void
pui_location_start(PuiLocation *location)
{
#ifdef ENABLE_LOCATION
  PuiLocationPrivate *priv = PRIVATE(location);

  if (!priv->gpsd_control)
  {
    priv->gpsd_control = location_gpsd_control_get_default();

    g_return_if_fail(priv->gpsd_control != NULL);

    /* FIXME - what is this "preferred-interval"? */
    g_object_set(priv->gpsd_control,
                 "preferred-method", LOCATION_METHOD_ACWP,
                 "preferred-interval", 1200,
                 NULL);

    g_signal_connect(priv->gpsd_control, "error-verbose",
                     G_CALLBACK(gpsd_control_error_verbose_cb), location);
  }

  if (!priv->gps_device)
  {
    priv->gps_device = g_object_new(LOCATION_TYPE_GPS_DEVICE, NULL);

    g_return_if_fail(priv->gps_device != NULL);

    g_signal_connect(priv->gps_device, "changed",
                     G_CALLBACK(gps_device_changed_cb), location);
  }

  if (!priv->navigation)
  {
    priv->navigation = navigation_provider_new_default();

    g_return_if_fail(priv->navigation != NULL);

    if (!priv->gpsd_control_started)
      location_gpsd_control_start(priv->gpsd_control);
  }
#endif
}

void
pui_location_stop(PuiLocation *location)
{
#ifdef ENABLE_LOCATION
  PuiLocationPrivate *priv = PRIVATE(location);

  if (priv->hb_source)
  {
    g_source_unref(&priv->hb_source->source);
    priv->hb_source = NULL;
  }

  if (priv->gpsd_control)
  {
    location_gpsd_control_stop(priv->gpsd_control);
    g_object_unref(priv->gpsd_control);
    priv->gpsd_control = NULL;
  }

  if (priv->gps_device)
  {
    g_signal_handlers_disconnect_matched(
      priv->gps_device, G_SIGNAL_MATCH_DATA | G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
      gps_device_changed_cb, location);
    g_object_unref(priv->gps_device);
    priv->gps_device = NULL;
  }

  if (priv->navigation)
  {
    g_object_unref(priv->navigation);
    priv->navigation = NULL;
  }
#endif
}

PuiLocationLevel
pui_location_get_level(PuiLocation *location)
{
  return PRIVATE(location)->level;
}

void
pui_location_set_level(PuiLocation *location, PuiLocationLevel level)
{
  PRIVATE(location)->level = level;
}

const gchar *
pui_location_get_location(PuiLocation *location)
{
  PuiLocationPrivate *priv = PRIVATE(location);

  return PRIVATE(location)->locations[priv->level];
}

void
pui_location_reset(PuiLocation *location)
{
#ifdef ENABLE_LOCATION
  PRIVATE(location)->gpsd_control_started = FALSE;
#endif
}
