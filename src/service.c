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

#include <glib/gi18n.h>
#include <gio/gio.h>
#include "service.h"

#define BUS_NAME "org.ayatana.indicator.a11y"
#define BUS_PATH "/org/ayatana/indicator/a11y"

static guint m_nSignal = 0;

struct _IndicatorA11yServicePrivate
{
    guint nOwnId;
    guint nActionsId;
    GDBusConnection *pConnection;
    gboolean bMenusBuilt;
    GSimpleActionGroup *pActionGroup;
    GMenu *pMenu;
    GMenu *pSubmenu;
    guint nExportId;
    GSimpleAction *pHeaderAction;
    guint nOnboardSubscription;
    gboolean bOnboardActive;
    GSettings *pOrcaSettings;
    guint nOrcaSubscription;
    gboolean bOrcaActive;
    gboolean bHighContrast;
    GSettings *pHighContrastSettings;
    gboolean bIgnoreSettings;
    gchar *sThemeGtk;
    gchar *sThemeIcon;
    gboolean bGreeter;
};

typedef IndicatorA11yServicePrivate priv_t;

G_DEFINE_TYPE_WITH_PRIVATE (IndicatorA11yService, indicator_a11y_service, G_TYPE_OBJECT)

static GVariant* createHeaderState (IndicatorA11yService *self)
{
    GVariantBuilder cBuilder;
    g_variant_builder_init (&cBuilder, G_VARIANT_TYPE ("a{sv}"));
    g_variant_builder_add (&cBuilder, "{sv}", "title", g_variant_new_string (_("Accessibility")));
    g_variant_builder_add (&cBuilder, "{sv}", "tooltip", g_variant_new_string (_("Accessibility settings")));
    g_variant_builder_add (&cBuilder, "{sv}", "visible", g_variant_new_boolean (TRUE));
    g_variant_builder_add (&cBuilder, "{sv}", "accessible-desc", g_variant_new_string (_("Accessibility settings")));

    GIcon *pIcon = g_themed_icon_new_with_default_fallbacks ("preferences-desktop-accessibility-panel");
    GVariant *pSerialized = g_icon_serialize (pIcon);

    if (pSerialized != NULL)
    {
        g_variant_builder_add (&cBuilder, "{sv}", "icon", pSerialized);
        g_variant_unref (pSerialized);
    }

    g_object_unref (pIcon);

    return g_variant_builder_end (&cBuilder);
}

static void onOnboardBus (GDBusConnection *pConnection, const gchar *sSender, const gchar *sPath, const gchar *sInterface, const gchar *sSignal, GVariant *pParameters, gpointer pUserData)
{
    GVariant *pDict = g_variant_get_child_value (pParameters, 1);
    GVariant* pValue = g_variant_lookup_value (pDict, "Visible", G_VARIANT_TYPE_BOOLEAN);
    g_variant_unref (pDict);

    IndicatorA11yService *self = INDICATOR_A11Y_SERVICE (pUserData);
    gboolean bActive = g_variant_get_boolean (pValue);

    if (bActive != self->pPrivate->bOnboardActive)
    {
        self->pPrivate->bOnboardActive = bActive;
        GAction *pAction = g_action_map_lookup_action (G_ACTION_MAP (self->pPrivate->pActionGroup), "onboard");
        g_action_change_state (pAction, pValue);
    }

    g_variant_unref (pValue);
}

static void onBusAcquired (GDBusConnection *pConnection, const gchar *sName, gpointer pData)
{
    IndicatorA11yService *self = INDICATOR_A11Y_SERVICE (pData);

    g_debug ("bus acquired: %s", sName);

    self->pPrivate->pConnection = (GDBusConnection*) g_object_ref (G_OBJECT (pConnection));

    GError *pError = NULL;
    self->pPrivate->nActionsId = g_dbus_connection_export_action_group (pConnection, BUS_PATH, G_ACTION_GROUP (self->pPrivate->pActionGroup), &pError);

    // Export the actions
    if (!self->pPrivate->nActionsId)
    {
        g_warning ("cannot export action group: %s", pError->message);
        g_clear_error(&pError);
    }

    // Export the menu
    gchar *sPath = g_strdup_printf ("%s/desktop", BUS_PATH);
    self->pPrivate->nExportId = g_dbus_connection_export_menu_model (pConnection, sPath, G_MENU_MODEL (self->pPrivate->pMenu), &pError);

    if (!self->pPrivate->nExportId)
    {
        g_warning ("cannot export %s menu: %s", sPath, pError->message);
        g_clear_error (&pError);
    }

    g_free (sPath);

    if (!self->pPrivate->bGreeter)
    {
        // Listen to Onboard messages
        self->pPrivate->nOnboardSubscription = g_dbus_connection_signal_subscribe (self->pPrivate->pConnection, NULL, "org.freedesktop.DBus.Properties", "PropertiesChanged", "/org/onboard/Onboard/Keyboard", "org.onboard.Onboard.Keyboard", G_DBUS_SIGNAL_FLAGS_MATCH_ARG0_NAMESPACE, onOnboardBus, self, NULL);
    }
}

static void unexport (IndicatorA11yService *self)
{
    // Unexport the menu
    if (self->pPrivate->nExportId)
    {
        g_dbus_connection_unexport_menu_model (self->pPrivate->pConnection, self->pPrivate->nExportId);
        self->pPrivate->nExportId = 0;
    }

    // Unexport the actions
    if (self->pPrivate->nActionsId)
    {
        g_dbus_connection_unexport_action_group (self->pPrivate->pConnection, self->pPrivate->nActionsId);
        self->pPrivate->nActionsId = 0;
    }
}

static void onNameLost (GDBusConnection *pConnection, const gchar *sName, gpointer pData)
{
    IndicatorA11yService *self = INDICATOR_A11Y_SERVICE (pData);

    g_debug ("%s %s name lost %s", G_STRLOC, G_STRFUNC, sName);

    unexport (self);
}

static void onDispose (GObject *pObject)
{
    IndicatorA11yService *self = INDICATOR_A11Y_SERVICE (pObject);

    if (!self->pPrivate->bGreeter)
    {
        if (self->pPrivate->nOnboardSubscription)
        {
            g_dbus_connection_signal_unsubscribe (self->pPrivate->pConnection, self->pPrivate->nOnboardSubscription);
        }

        if (self->pPrivate->pOrcaSettings)
        {
            g_clear_object (&self->pPrivate->pOrcaSettings);
        }

        if (self->pPrivate->pHighContrastSettings)
        {
            g_clear_object (&self->pPrivate->pHighContrastSettings);
        }

        if (self->pPrivate->sThemeGtk)
        {
            g_free (self->pPrivate->sThemeGtk);
        }

        if (self->pPrivate->sThemeIcon)
        {
            g_free (self->pPrivate->sThemeIcon);
        }
    }

    if (self->pPrivate->nOwnId)
    {
        g_bus_unown_name (self->pPrivate->nOwnId);
        self->pPrivate->nOwnId = 0;
    }

    unexport (self);

    g_clear_object (&self->pPrivate->pHeaderAction);
    g_clear_object (&self->pPrivate->pActionGroup);
    g_clear_object (&self->pPrivate->pConnection);

    G_OBJECT_CLASS (indicator_a11y_service_parent_class)->dispose (pObject);
}

static void onOnboardState (GSimpleAction *pAction, GVariant* pValue, gpointer pUserData)
{
    g_simple_action_set_state (pAction, pValue);

    gboolean bActive = g_variant_get_boolean (pValue);
    IndicatorA11yService *self = INDICATOR_A11Y_SERVICE (pUserData);

    if (bActive != self->pPrivate->bOnboardActive)
    {
        gchar *sFunction = NULL;

        if (bActive)
        {
            sFunction = "Show";
        }
        else
        {
            sFunction = "Hide";
        }

        GError *pError = NULL;

        if (!self->pPrivate->bGreeter)
        {
            g_dbus_connection_call_sync (self->pPrivate->pConnection, "org.onboard.Onboard", "/org/onboard/Onboard/Keyboard", "org.onboard.Onboard.Keyboard", sFunction, NULL, NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &pError);
        }
        else
        {
            GVariant *pParam = g_variant_new ("(b)", bActive);
            g_dbus_connection_call_sync (self->pPrivate->pConnection, "org.ArcticaProject.ArcticaGreeter", "/org/ArcticaProject/ArcticaGreeter", "org.ArcticaProject.ArcticaGreeter", "ToggleOnBoard", pParam, NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &pError);
        }

        if (pError)
        {
            g_error ("Panic: Failed to toggle Onboard: %s", pError->message);
            g_error_free (pError);

            return;
        }

        self->pPrivate->bOnboardActive = bActive;
    }
}

static void onOrcaState (GSimpleAction *pAction, GVariant* pValue, gpointer pUserData)
{
    g_simple_action_set_state (pAction, pValue);

    IndicatorA11yService *self = INDICATOR_A11Y_SERVICE (pUserData);

    if (self->pPrivate->bGreeter)
    {
        gboolean bActive = g_variant_get_boolean (pValue);

        if (bActive != self->pPrivate->bOrcaActive)
        {
            GError *pError = NULL;
            GVariant *pParam = g_variant_new ("(b)", bActive);
            g_dbus_connection_call_sync (self->pPrivate->pConnection, "org.ArcticaProject.ArcticaGreeter", "/org/ArcticaProject/ArcticaGreeter", "org.ArcticaProject.ArcticaGreeter", "ToggleOrca", pParam, NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &pError);

            if (pError)
            {
                g_error ("Panic: Failed to toggle Orca: %s", pError->message);
                g_error_free (pError);

                return;
            }

            self->pPrivate->bOrcaActive = bActive;
        }
    }
}

static void onContrastState (GSimpleAction *pAction, GVariant* pValue, gpointer pUserData)
{
    g_simple_action_set_state (pAction, pValue);
    IndicatorA11yService *self = INDICATOR_A11Y_SERVICE (pUserData);
    gboolean bActive = g_variant_get_boolean (pValue);

    if (bActive != self->pPrivate->bHighContrast)
    {
        if (!self->pPrivate->bGreeter)
        {
            self->pPrivate->bIgnoreSettings = TRUE;

            if (bActive)
            {
                g_free (self->pPrivate->sThemeGtk);
                g_free (self->pPrivate->sThemeIcon);
                self->pPrivate->sThemeGtk = g_settings_get_string (self->pPrivate->pHighContrastSettings, "gtk-theme");
                self->pPrivate->sThemeIcon = g_settings_get_string (self->pPrivate->pHighContrastSettings, "icon-theme");
                g_settings_set_string (self->pPrivate->pHighContrastSettings, "gtk-theme", "ContrastHigh");
                g_settings_set_string (self->pPrivate->pHighContrastSettings, "icon-theme", "ContrastHigh");
            }
            else
            {
                g_settings_set_string (self->pPrivate->pHighContrastSettings, "gtk-theme", self->pPrivate->sThemeGtk);
                g_settings_set_string (self->pPrivate->pHighContrastSettings, "icon-theme", self->pPrivate->sThemeIcon);
            }

            self->pPrivate->bIgnoreSettings = FALSE;
        }
        else
        {
            GError *pError = NULL;
            GVariant *pParam = g_variant_new ("(b)", bActive);
            g_dbus_connection_call_sync (self->pPrivate->pConnection, "org.ArcticaProject.ArcticaGreeter", "/org/ArcticaProject/ArcticaGreeter", "org.ArcticaProject.ArcticaGreeter", "ToggleHighContrast", pParam, NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &pError);

            if (pError)
            {
                g_error ("Panic: Failed to toggle high contrast: %s", pError->message);
                g_error_free (pError);

                return;
            }
        }

        self->pPrivate->bHighContrast = bActive;
    }
}

static void onContrastSettings (GSettings *pSettings, const gchar *sKey, gpointer pUserData)
{
    IndicatorA11yService *self = INDICATOR_A11Y_SERVICE (pUserData);

    if (self->pPrivate->bIgnoreSettings)
    {
        return;
    }

    gboolean bThemeGtk = g_str_equal (sKey, "gtk-theme");
    gboolean bThemeIcon = g_str_equal (sKey, "icon-theme");

    if (bThemeGtk)
    {
        g_free (self->pPrivate->sThemeGtk);
        self->pPrivate->sThemeGtk = g_settings_get_string (self->pPrivate->pHighContrastSettings, "gtk-theme");
    }
    else if (bThemeIcon)
    {
        g_free (self->pPrivate->sThemeIcon);
        self->pPrivate->sThemeIcon = g_settings_get_string (self->pPrivate->pHighContrastSettings, "icon-theme");
    }

    bThemeGtk = g_str_equal (self->pPrivate->sThemeGtk, "ContrastHigh");
    bThemeIcon = g_str_equal (self->pPrivate->sThemeIcon, "ContrastHigh");
    gboolean bHighContrast = (bThemeGtk && bThemeIcon);

    if (self->pPrivate->bHighContrast != bHighContrast)
    {
        GAction *pAction = g_action_map_lookup_action (G_ACTION_MAP (self->pPrivate->pActionGroup), "contrast");
        GVariant *pValue = g_variant_new_boolean (bHighContrast);
        g_action_change_state (pAction, pValue);
    }
}

static gboolean valueFromVariant (GValue *pValue, GVariant *pVariant, gpointer pUserData)
{
    g_value_set_variant (pValue, pVariant);

    return TRUE;
}

static GVariant* valueToVariant (const GValue *pValue, const GVariantType *pType, gpointer pUserData)
{
    GVariant *pVariant = g_value_dup_variant (pValue);

    return pVariant;
}

static void indicator_a11y_service_init (IndicatorA11yService *self)
{
    self->pPrivate = indicator_a11y_service_get_instance_private (self);
    const char *sUser = g_get_user_name();
    self->pPrivate->bGreeter = g_str_equal (sUser, "lightdm");

    self->pPrivate->bOnboardActive = FALSE;
    self->pPrivate->bOrcaActive = FALSE;
    self->pPrivate->sThemeGtk = NULL;
    self->pPrivate->sThemeIcon = NULL;
    self->pPrivate->bIgnoreSettings = FALSE;

    GSettingsSchemaSource *pSource = g_settings_schema_source_get_default ();
    GSettingsSchema *pSchema = NULL;

    if (!self->pPrivate->bGreeter)
    {
        // Get the settings
        if (pSource)
        {
            pSchema = g_settings_schema_source_lookup (pSource, "org.gnome.desktop.a11y.applications", FALSE);

            if (pSchema)
            {
                g_settings_schema_unref (pSchema);
                self->pPrivate->pOrcaSettings = g_settings_new ("org.gnome.desktop.a11y.applications");
            }
            else
            {
                g_error ("Panic: No org.gnome.desktop.a11y.applications schema found");
            }

            /* This is what we should use, but not all applications react to "high-contrast" setting (yet)
            pSchema = g_settings_schema_source_lookup (pSource, "org.gnome.desktop.a11y.interface", FALSE);

            if (pSchema)
            {
                g_settings_schema_unref (pSchema);
                self->pPrivate->pHighContrastSettings = g_settings_new ("org.gnome.desktop.a11y.interface");
                self->pPrivate->bHighContrast = g_settings_get_boolean (self->pPrivate->pHighContrastSettings, "high-contrast");
            }
            else
            {
                g_error ("Panic: No org.gnome.desktop.a11y.interface schema found");
            }*/

            pSchema = g_settings_schema_source_lookup (pSource, "org.mate.interface", FALSE);

            if (pSchema)
            {
                g_settings_schema_unref (pSchema);
                self->pPrivate->pHighContrastSettings = g_settings_new ("org.mate.interface");
                self->pPrivate->sThemeGtk = g_settings_get_string (self->pPrivate->pHighContrastSettings, "gtk-theme");
                self->pPrivate->sThemeIcon = g_settings_get_string (self->pPrivate->pHighContrastSettings, "icon-theme");
                gboolean bThemeGtk = g_str_equal (self->pPrivate->sThemeGtk, "ContrastHigh");
                gboolean bThemeIcon = g_str_equal (self->pPrivate->sThemeIcon, "ContrastHigh");
                self->pPrivate->bHighContrast = (bThemeGtk && bThemeIcon);
            }
            else
            {
                g_error ("Panic: No org.mate.interface schema found");
            }
        }
    }
    else
    {
        // Get Arctica settings
        if (pSource)
        {
            pSchema = g_settings_schema_source_lookup (pSource, "org.ArcticaProject.arctica-greeter", FALSE);

            if (pSchema)
            {
                g_settings_schema_unref (pSchema);
                self->pPrivate->pHighContrastSettings = g_settings_new ("org.ArcticaProject.arctica-greeter");
                self->pPrivate->bHighContrast = g_settings_get_boolean (self->pPrivate->pHighContrastSettings, "high-contrast");
            }
            else
            {
                g_error ("Panic: No org.ArcticaProject.arctica-greeter schema found");
            }
        }
    }

    // Create actions
    GSimpleAction *pAction = NULL;
    self->pPrivate->pActionGroup = g_simple_action_group_new ();

    pAction = g_simple_action_new_stateful ("_header-desktop", NULL, createHeaderState (self));
    g_action_map_add_action (G_ACTION_MAP (self->pPrivate->pActionGroup), G_ACTION (pAction));
    self->pPrivate->pHeaderAction = pAction;

    GVariant *pContrast = g_variant_new_boolean (self->pPrivate->bHighContrast);
    pAction = g_simple_action_new_stateful ("contrast", G_VARIANT_TYPE_BOOLEAN, pContrast);

    if (!self->pPrivate->bGreeter)
    {
        /* This is what we should use, but not all applications react to "high-contrast" setting (yet)
        g_settings_bind_with_mapping (self->pPrivate->pHighContrastSettings, "high-contrast", pAction, "state", G_SETTINGS_BIND_DEFAULT, valueFromVariant, valueToVariant, NULL, NULL);*/

        // Workaround for applications that do not react to "high-contrast" setting
        g_signal_connect (self->pPrivate->pHighContrastSettings, "changed::gtk-theme", G_CALLBACK (onContrastSettings), self);
        g_signal_connect (self->pPrivate->pHighContrastSettings, "changed::icon-theme", G_CALLBACK (onContrastSettings), self);
    }

    g_action_map_add_action (G_ACTION_MAP (self->pPrivate->pActionGroup), G_ACTION (pAction));
    g_signal_connect (pAction, "change-state", G_CALLBACK (onContrastState), self);
    g_object_unref (G_OBJECT (pAction));

    GVariant *pOnboard = g_variant_new_boolean (self->pPrivate->bOnboardActive);
    pAction = g_simple_action_new_stateful ("onboard", G_VARIANT_TYPE_BOOLEAN, pOnboard);
    g_action_map_add_action (G_ACTION_MAP (self->pPrivate->pActionGroup), G_ACTION (pAction));
    g_signal_connect (pAction, "change-state", G_CALLBACK (onOnboardState), self);
    g_object_unref (G_OBJECT (pAction));

    GVariant *pOrca = g_variant_new_boolean (self->pPrivate->bOrcaActive);
    pAction = g_simple_action_new_stateful ("orca", G_VARIANT_TYPE_BOOLEAN, pOrca);
    g_settings_bind_with_mapping (self->pPrivate->pOrcaSettings, "screen-reader-enabled", pAction, "state", G_SETTINGS_BIND_DEFAULT, valueFromVariant, valueToVariant, NULL, NULL);
    g_action_map_add_action (G_ACTION_MAP (self->pPrivate->pActionGroup), G_ACTION (pAction));
    g_signal_connect (pAction, "change-state", G_CALLBACK (onOrcaState), self);
    g_object_unref (G_OBJECT (pAction));

    // Add sections to the submenu
    self->pPrivate->pSubmenu = g_menu_new();
    GMenu *pSection = g_menu_new();
    GMenuItem *pItem = NULL;

    pItem = g_menu_item_new (_("High Contrast"), "indicator.contrast");
    g_menu_item_set_attribute (pItem, "x-ayatana-type", "s", "org.ayatana.indicator.switch");
    g_menu_append_item (pSection, pItem);
    g_object_unref (pItem);

    pItem = g_menu_item_new (_("On-Screen Keyboard"), "indicator.onboard");
    g_menu_item_set_attribute (pItem, "x-ayatana-type", "s", "org.ayatana.indicator.switch");
    g_menu_append_item (pSection, pItem);
    g_object_unref (pItem);

    pItem = g_menu_item_new (_("Screen Reader"), "indicator.orca");
    g_menu_item_set_attribute (pItem, "x-ayatana-type", "s", "org.ayatana.indicator.switch");
    g_menu_append_item (pSection, pItem);
    g_object_unref (pItem);

    g_menu_append_section (self->pPrivate->pSubmenu, NULL, G_MENU_MODEL (pSection));
    g_object_unref (pSection);

    // Add submenu to the header
    pItem = g_menu_item_new (NULL, "indicator._header-desktop");
    g_menu_item_set_attribute (pItem, "x-ayatana-type", "s", "org.ayatana.indicator.root");
    g_menu_item_set_submenu (pItem, G_MENU_MODEL (self->pPrivate->pSubmenu));
    g_object_unref (self->pPrivate->pSubmenu);

    // Add header to the menu
    self->pPrivate->pMenu = g_menu_new ();
    g_menu_append_item (self->pPrivate->pMenu, pItem);
    g_object_unref (pItem);

    self->pPrivate->bMenusBuilt = TRUE;
    self->pPrivate->nOwnId = g_bus_own_name (G_BUS_TYPE_SESSION, BUS_NAME, G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT, onBusAcquired, NULL, onNameLost, self, NULL);
}

static void indicator_a11y_service_class_init (IndicatorA11yServiceClass *klass)
{
    GObjectClass *pClass = G_OBJECT_CLASS(klass);
    pClass->dispose = onDispose;
    m_nSignal = g_signal_new ("name-lost", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (IndicatorA11yServiceClass, pNameLost), NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
}

IndicatorA11yService *indicator_a11y_service_new ()
{
    GObject *pObject = g_object_new (INDICATOR_TYPE_A11Y_SERVICE, NULL);

    return INDICATOR_A11Y_SERVICE (pObject);
}
