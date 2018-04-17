/* vim: set et ts=8 sw=8: */
/* gclue-min-uint.c
 *
 * Copyright (C) 2018 Collabora Ltd.
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
 * Author: Zeeshan Ali (Khattak) <zeeshanak@gnome.org>
 */

#include "gclue-min-uint.h"

/**
 * SECTION:gclue-min-uint
 * @short_description: Easy way to keep track of minimum of a bunch of values
 * @include: gclue-glib/gclue-location-source.h
 *
 * This is a helper class that keeps a list of guint values and the minimum
 * value from this list. It is used by location sources to use the minimum
 * time-threshold (location update rate) from all the time-thresholds requested
 * by different applications.
 **/

G_DEFINE_TYPE (GClueMinUINT, gclue_min_uint, G_TYPE_OBJECT)

struct _GClueMinUINTPrivate
{
        GHashTable *all_values;

        gboolean notify_value;
};

enum
{
        PROP_0,
        PROP_VALUE,
        LAST_PROP
};

static GParamSpec *gParamSpecs[LAST_PROP];

static void
gclue_min_uint_finalize (GObject *object)
{
        g_clear_pointer (&GCLUE_MIN_UINT (object)->priv->all_values,
                         g_hash_table_unref);

        /* Chain up to the parent class */
        G_OBJECT_CLASS (gclue_min_uint_parent_class)->finalize (object);
}

static void
gclue_min_uint_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
        GClueMinUINT *muint = GCLUE_MIN_UINT (object);

        switch (prop_id) {
        case PROP_VALUE:
                g_value_set_uint (value, gclue_min_uint_get_value (muint));
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
}

static void
gclue_min_uint_class_init (GClueMinUINTClass *klass)
{
        GObjectClass *object_class;

        object_class = G_OBJECT_CLASS (klass);
        object_class->finalize = gclue_min_uint_finalize;
        object_class->get_property = gclue_min_uint_get_property;

        g_type_class_add_private (object_class, sizeof (GClueMinUINTPrivate));

        gParamSpecs[PROP_VALUE] = g_param_spec_uint ("value",
                                                     "Value",
                                                     "The mininum value",
                                                     0,
                                                     G_MAXINT,
                                                     0,
                                                     G_PARAM_READABLE);
        g_object_class_install_property (object_class,
                                         PROP_VALUE,
                                         gParamSpecs[PROP_VALUE]);
}

static void
gclue_min_uint_init (GClueMinUINT *muint)
{
        muint->priv = G_TYPE_INSTANCE_GET_PRIVATE (muint,
                                                   GCLUE_TYPE_MIN_UINT,
                                                   GClueMinUINTPrivate);
        muint->priv->all_values = g_hash_table_new (g_direct_hash,
                                                    g_direct_equal);
        muint->priv->notify_value = TRUE;
}

/**
 * gclue_min_uint_new
 *
 * Returns: A new #GClueMinUINT instance.
 **/
GClueMinUINT *
gclue_min_uint_new (void)
{
        return g_object_new (GCLUE_TYPE_MIN_UINT, NULL);
}

/**
 * gclue_min_uint_get_value
 * @muint: a #GClueMinUINT
 *
 * Returns: The current mininum value from the list.
 **/
guint
gclue_min_uint_get_value (GClueMinUINT *muint)
{
        guint value;
        GList *keys, *l;

        g_return_val_if_fail (GCLUE_IS_MIN_UINT(muint), 0);

        if (g_hash_table_size (muint->priv->all_values) == 0)
                return 0;

        keys = g_hash_table_get_keys (muint->priv->all_values);
        value = GPOINTER_TO_UINT (keys->data);

        for (l = keys->next; l; l = l->next) {
                guint i = GPOINTER_TO_UINT (l->data);

                if (value > i) {
                        value = i;
                }
        }

        return value;
}

/**
 * gclue_min_uint_add_value
 * @muint: a #GClueMinUINT
 * @value: A value to add to the list
 **/
void
gclue_min_uint_add_value (GClueMinUINT *muint,
                          guint         value)
{
        gpointer key, hash_value;
        guint cur_value, new_value;
        guint num_users = 1;

        g_return_if_fail (GCLUE_IS_MIN_UINT(muint));

        key = GUINT_TO_POINTER (value);
        hash_value = g_hash_table_lookup (muint->priv->all_values, key);
        if (hash_value != NULL) {
                num_users = GPOINTER_TO_UINT (hash_value) + 1;
        }

        cur_value = gclue_min_uint_get_value (muint);
        g_hash_table_replace (muint->priv->all_values,
                              key,
                              GUINT_TO_POINTER (num_users));
        new_value = gclue_min_uint_get_value (muint);

        if (cur_value != new_value && muint->priv->notify_value) {
                g_object_notify_by_pspec (G_OBJECT (muint),
                                          gParamSpecs[PROP_VALUE]);
        }
}

/**
 * gclue_min_uint_drop_value
 * @muint: a #GClueMinUINT
 * @value: A value to drop from the list
 **/
void
gclue_min_uint_drop_value (GClueMinUINT *muint,
                           guint         value)
{
        gpointer key, hash_value;
        guint cur_value, new_value;
        guint num_users;

        g_return_if_fail (GCLUE_IS_MIN_UINT(muint));

        key = GUINT_TO_POINTER (value);
        hash_value = g_hash_table_lookup (muint->priv->all_values, key);
        if (hash_value == NULL) {
                return;
        }

        cur_value = gclue_min_uint_get_value (muint);
        num_users = GPOINTER_TO_UINT (hash_value) - 1;
        if (num_users == 0) {
                g_hash_table_remove (muint->priv->all_values, key);
        } else {
                g_hash_table_replace (muint->priv->all_values,
                                      key,
                                      GUINT_TO_POINTER (num_users));
        }
        new_value = gclue_min_uint_get_value (muint);

        if (cur_value != new_value && muint->priv->notify_value) {
                g_object_notify_by_pspec (G_OBJECT (muint),
                                          gParamSpecs[PROP_VALUE]);
        }
}

/**
 * gclue_min_uint_exchange_value
 * @muint: a #GClueMinUINT
 * @to_drop: A value to drop from the list
 * @to_add: A value to add to the list
 *
 * Use this method instead of #gclue_min_uint_drop_value and
 * #gclue_min_uint_add_value to ensure #GClueMinUINT:value property is only
 * notified once.
 **/
void
gclue_min_uint_exchage_value (GClueMinUINT *muint,
                              guint         to_drop,
                              guint         to_add)
{
        g_return_if_fail (GCLUE_IS_MIN_UINT(muint));

        muint->priv->notify_value = FALSE;
        gclue_min_uint_drop_value (muint, to_drop);
        muint->priv->notify_value = TRUE;

        gclue_min_uint_add_value (muint, to_add);
}
