/* vim: set et ts=8 sw=8: */
/* gclue-locator.c
 *
 * Copyright (C) 2013 Red Hat, Inc.
 *
 * Geoclue is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * Geoclue is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along
 * with Geoclue; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Authors: Zeeshan Ali (Khattak) <zeeshanak@gnome.org>
 */

#include <glib/gi18n.h>

#include "gclue-locator.h"
#include "gclue-ipclient.h"
#include "gclue-wifi.h"
#include "gclue-enum-types.h"

/* This class will be responsible for doing the actual geolocating. */

G_DEFINE_TYPE (GClueLocator, gclue_locator, G_TYPE_OBJECT)

struct _GClueLocatorPrivate
{
        GList *sources;

        GeocodeLocation *location;

        GCancellable *cancellable;

        gulong network_changed_id;

        GClueAccuracyLevel accuracy_level;
};

enum
{
        PROP_0,
        PROP_LOCATION,
        PROP_ACCURACY_LEVEL,
        LAST_PROP
};

static GParamSpec *gParamSpecs[LAST_PROP];

static void
gclue_locator_finalize (GObject *object)
{
        gclue_locator_stop (GCLUE_LOCATOR (object));

        G_OBJECT_CLASS (gclue_locator_parent_class)->finalize (object);
}

static void
gclue_locator_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
        GClueLocator *locator = GCLUE_LOCATOR (object);

        switch (prop_id) {
        case PROP_LOCATION:
                g_value_set_object (value, locator->priv->location);
                break;

        case PROP_ACCURACY_LEVEL:
                g_value_set_enum (value, locator->priv->accuracy_level);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
}

static void
gclue_locator_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
        GClueLocator *locator = GCLUE_LOCATOR (object);

        switch (prop_id) {
        case PROP_ACCURACY_LEVEL:
                locator->priv->accuracy_level = g_value_get_enum (value);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
}

static void
gclue_locator_class_init (GClueLocatorClass *klass)
{
        GObjectClass *object_class;

        object_class = G_OBJECT_CLASS (klass);
        object_class->finalize = gclue_locator_finalize;
        object_class->get_property = gclue_locator_get_property;
        object_class->set_property = gclue_locator_set_property;
        g_type_class_add_private (object_class, sizeof (GClueLocatorPrivate));

        gParamSpecs[PROP_LOCATION] = g_param_spec_object ("location",
                                                          "Location",
                                                          "Location",
                                                          GEOCODE_TYPE_LOCATION,
                                                          G_PARAM_READABLE);
        g_object_class_install_property (object_class,
                                         PROP_LOCATION,
                                         gParamSpecs[PROP_LOCATION]);

        gParamSpecs[PROP_ACCURACY_LEVEL] = g_param_spec_enum ("accuracy-level",
                                                              "AccuracyLevel",
                                                              "Accuracy level",
                                                              GCLUE_TYPE_ACCURACY_LEVEL,
                                                              GCLUE_ACCURACY_LEVEL_CITY,
                                                              G_PARAM_READWRITE);
        g_object_class_install_property (object_class,
                                         PROP_ACCURACY_LEVEL,
                                         gParamSpecs[PROP_ACCURACY_LEVEL]);
}

static void
gclue_locator_init (GClueLocator *locator)
{
        locator->priv =
                G_TYPE_INSTANCE_GET_PRIVATE (locator,
                                            GCLUE_TYPE_LOCATOR,
                                            GClueLocatorPrivate);
        locator->priv->cancellable = g_cancellable_new ();
}

GClueLocator *
gclue_locator_new (void)
{
        return g_object_new (GCLUE_TYPE_LOCATOR, NULL);
}

static void
gclue_locator_update_location (GClueLocator    *locator,
                               GeocodeLocation *location)
{
        GClueLocatorPrivate *priv = locator->priv;

        if (priv->location == NULL)
                priv->location = g_object_new (GEOCODE_TYPE_LOCATION, NULL);
        else if (geocode_location_get_accuracy (location) >=
                 geocode_location_get_accuracy (priv->location)) {
                /* We only take the new location if its more or as accurate as
                 * the previous one.
                 */
                g_debug ("Ignoring less accurate new location");
                return;
        }

        g_object_set (priv->location,
                      "latitude", geocode_location_get_latitude (location),
                      "longitude", geocode_location_get_longitude (location),
                      "accuracy", geocode_location_get_accuracy (location),
                      "description", geocode_location_get_description (location),
                      NULL);

        g_object_notify (G_OBJECT (locator), "location");
}

static void
on_location_search_ready (GObject      *source_object,
                          GAsyncResult *res,
                          gpointer      user_data)
{
        GClueLocationSource *source = GCLUE_LOCATION_SOURCE (source_object);
        GClueLocator *locator = GCLUE_LOCATOR (user_data);
        GeocodeLocation *location;
        GError *error = NULL;

        location = gclue_location_source_search_finish (source, res, &error);
        if (location == NULL) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_warning ("%s", error->message);
                g_error_free (error);
                return;
        }

        g_debug ("New location available");
        gclue_locator_update_location (locator, location);
        g_object_unref (location);
}

static void
on_network_changed (GNetworkMonitor *monitor,
                    gboolean         available,
                    gpointer         user_data)
{
        GClueLocator *locator = GCLUE_LOCATOR (user_data);
        GClueLocationSource *source;
        GList *node;

        if (!available) {
                g_debug ("Network unreachable");
                return;
        }
        g_debug ("Network changed");

        g_cancellable_cancel (locator->priv->cancellable);
        g_cancellable_reset (locator->priv->cancellable);

        for (node = locator->priv->sources; node != NULL; node = node->next) {
                source = GCLUE_LOCATION_SOURCE (node->data);

                gclue_location_source_search_async (source,
                                                    locator->priv->cancellable,
                                                    on_location_search_ready,
                                                    locator);
        }
}

void
gclue_locator_start (GClueLocator *locator)
{
        GNetworkMonitor *monitor;
        GClueIpclient *ipclient;
        GClueWifi *wifi;

        g_return_if_fail (GCLUE_IS_LOCATOR (locator));

        if (locator->priv->network_changed_id)
                return; /* Already started */

        /* FIXME: Only use sources that provide <= requested accuracy level. */
        ipclient = gclue_ipclient_new ();
        locator->priv->sources = g_list_append (locator->priv->sources,
                                                ipclient);
        wifi = gclue_wifi_new ();
        locator->priv->sources = g_list_append (locator->priv->sources,
                                                wifi);

        monitor = g_network_monitor_get_default ();
        locator->priv->network_changed_id =
                g_signal_connect (monitor,
                                  "network-changed",
                                  G_CALLBACK (on_network_changed),
                                  locator);

        if (g_network_monitor_get_network_available (monitor))
                on_network_changed (monitor, TRUE, locator);
}

void
gclue_locator_stop (GClueLocator *locator)
{
        GClueLocatorPrivate *priv = locator->priv;

        if (priv->network_changed_id) {
                g_signal_handler_disconnect (g_network_monitor_get_default (),
                                             priv->network_changed_id);
                priv->network_changed_id = 0;
        }

        g_cancellable_cancel (priv->cancellable);
        g_cancellable_reset (priv->cancellable);
        g_list_free_full (priv->sources, g_object_unref);
        priv->sources = NULL;
        g_clear_object (&priv->location);
}

GeocodeLocation * gclue_locator_get_location (GClueLocator *locator)
{
        g_return_val_if_fail (GCLUE_IS_LOCATOR (locator), NULL);

        return locator->priv->location;
}

GClueAccuracyLevel
gclue_locator_get_accuracy_level (GClueLocator *locator)
{
        g_return_val_if_fail (GCLUE_IS_LOCATOR (locator),
                              GCLUE_ACCURACY_LEVEL_COUNTRY);

        return locator->priv->accuracy_level;
}

void
gclue_locator_set_accuracy_level (GClueLocator      *locator,
                                  GClueAccuracyLevel level)
{
        g_return_if_fail (GCLUE_IS_LOCATOR (locator));

        locator->priv->accuracy_level = level;
        g_object_notify (G_OBJECT (locator), "accuracy-level");
}
