/*
 * Copyright 2023 Robert Tari <robert@tari.in>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3, as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranties of
 * MERCHANTABILITY, SATISFACTORY QUALITY, or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __INDICATOR_A11Y_SERVICE_H__
#define __INDICATOR_A11Y_SERVICE_H__

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define INDICATOR_A11Y_SERVICE(o) (G_TYPE_CHECK_INSTANCE_CAST((o), INDICATOR_TYPE_A11Y_SERVICE, IndicatorA11yService))
#define INDICATOR_TYPE_A11Y_SERVICE (indicator_a11y_service_get_type())
#define INDICATOR_IS_A11Y_SERVICE(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), INDICATOR_TYPE_A11Y_SERVICE))

typedef struct _IndicatorA11yService IndicatorA11yService;
typedef struct _IndicatorA11yServiceClass IndicatorA11yServiceClass;
typedef struct _IndicatorA11yServicePrivate IndicatorA11yServicePrivate;

struct _IndicatorA11yService
{
    GObject parent;
    IndicatorA11yServicePrivate *pPrivate;
};

struct _IndicatorA11yServiceClass
{
    GObjectClass parent_class;
    void (*pNameLost)(IndicatorA11yService *self);
};

GType indicator_a11y_service_get_type(void);
IndicatorA11yService* indicator_a11y_service_new();

G_END_DECLS

#endif
