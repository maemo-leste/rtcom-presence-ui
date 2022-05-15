/*
 * pui-module.h
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

#ifndef __PUIMODULE_H_INCLUDED__
#define __PUIMODULE_H_INCLUDED__

G_BEGIN_DECLS

#define PUI_TYPE_MENU_ITEM \
  (pui_menu_item_get_type())

GType
pui_menu_item_get_type(void) G_GNUC_CONST;

G_END_DECLS

#endif /* __PUIMODULE_H_INCLUDED__ */
