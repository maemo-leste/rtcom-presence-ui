/*
 * pui-location.h
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

#ifndef __PUI_LOCATION_H_INCLUDED__
#define __PUI_LOCATION_H_INCLUDED__

#include <glib-object.h>

G_BEGIN_DECLS

#define PUI_TYPE_LOCATION \
                (pui_location_get_type ())
#define PUI_LOCATION(obj) \
                (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                 PUI_TYPE_LOCATION, \
                 PuiLocation))
#define PUI_LOCATION_CLASS(cls) \
                (G_TYPE_CHECK_CLASS_CAST ((cls), \
                 PUI_TYPE_LOCATION, \
                 PuiLocationClass))
#define PUI_IS_LOCATION(obj) \
                (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                 PUI_TYPE_LOCATION))
#define PUI_IS_LOCATION_CLASS(obj) \
                (G_TYPE_CHECK_CLASS_TYPE ((obj), \
                 PUI_TYPE_LOCATION))
#define PUI_LOCATION_GET_CLASS(obj) \
                (G_TYPE_INSTANCE_GET_CLASS ((obj), \
                 PUI_TYPE_LOCATION, \
                 PuiLocationClass))

struct _PuiLocation
{
  GObject parent;
};

typedef struct _PuiLocation PuiLocation;

struct _PuiLocationClass
{
  GObjectClass parent_class;
};

typedef struct _PuiLocationClass PuiLocationClass;

typedef enum
{
  PUI_LOCATION_LEVEL_STREET,
  PUI_LOCATION_LEVEL_DISTRICT,
  PUI_LOCATION_LEVEL_CITY,
  PUI_LOCATION_LEVEL_NONE,
  PUI_LOCATION_LEVEL_LAST
}
PuiLocationLevel;

GType
pui_location_get_type(void) G_GNUC_CONST;

void
pui_location_stop(PuiLocation *location);

void
pui_location_start(PuiLocation *location);

PuiLocationLevel
pui_location_get_level(PuiLocation *location);

void
pui_location_set_level(PuiLocation *location, PuiLocationLevel level);

const gchar *
pui_location_get_location(PuiLocation *location);

void
pui_location_reset(PuiLocation *location);

G_END_DECLS

#endif /* __PUI_LOCATION_H_INCLUDED__ */
