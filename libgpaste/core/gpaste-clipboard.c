/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011-2013 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 *
 *      GPaste is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation, either version 3 of the License, or
 *      (at your option) any later version.
 *
 *      GPaste is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with GPaste.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "gpaste-clipboard-private.h"

#include <gpaste-image-item.h>
#include <gpaste-text-item.h>
#include <gpaste-uris-item.h>

#include <string.h>

struct _GPasteClipboardPrivate
{
    GdkAtom         target;
    GtkClipboard   *real;
    GPasteSettings *settings;
    gchar          *text;
    gchar          *image_checksum;

    gulong          owner_change_signal;
};

G_DEFINE_TYPE_WITH_PRIVATE (GPasteClipboard, g_paste_clipboard, G_TYPE_OBJECT)

enum
{
    OWNER_CHANGE,

    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

/**
 * g_paste_clipboard_get_target:
 * @self: a #GPasteClipboard instance
 *
 * Get the target the #GPasteClipboard points to
 *
 * Returns: (transfer none): the GdkAtom representing the target (Primary, Clipboard, ...)
 */
G_PASTE_VISIBLE GdkAtom
g_paste_clipboard_get_target (const GPasteClipboard *self)
{
    g_return_val_if_fail (G_PASTE_IS_CLIPBOARD (self), NULL);

    GPasteClipboardPrivate *priv = g_paste_clipboard_get_instance_private ((GPasteClipboard *) self);

    return priv->target;
}

/**
 * g_paste_clipboard_get_real:
 * @self: a #GPasteClipboard instance
 *
 * Get the GtkClipboard linked to the #GPasteClipboard
 *
 * Returns: (transfer none): the GtkClipboard used in the #GPasteClipboard
 */
G_PASTE_VISIBLE GtkClipboard *
g_paste_clipboard_get_real (const GPasteClipboard *self)
{
    g_return_val_if_fail (G_PASTE_IS_CLIPBOARD (self), NULL);

    GPasteClipboardPrivate *priv = g_paste_clipboard_get_instance_private ((GPasteClipboard *) self);

    return priv->real;
}

/**
 * g_paste_clipboard_get_text:
 * @self: a #GPasteClipboard instance
 *
 * Get the text stored in the #GPasteClipboard
 *
 * Returns: read-only string containing the text or NULL
 */
G_PASTE_VISIBLE const gchar *
g_paste_clipboard_get_text (const GPasteClipboard *self)
{
    g_return_val_if_fail (G_PASTE_IS_CLIPBOARD (self), NULL);

    GPasteClipboardPrivate *priv = g_paste_clipboard_get_instance_private ((GPasteClipboard *) self);

    return priv->text;
}

static void
g_paste_clipboard_private_set_text (GPasteClipboardPrivate *priv,
                                    const gchar            *text)
{
    g_free (priv->text);
    g_free (priv->image_checksum);

    priv->text = g_strdup (text);
    priv->image_checksum = NULL;
}

/**
 * g_paste_clipboard_set_text:
 * @self: a #GPasteClipboard instance
 *
 * Put the text from the intern GtkClipboard in the #GPasteClipboard
 *
 * Returns: The new text if it was modified, or NULL
 */
G_PASTE_VISIBLE const gchar *
g_paste_clipboard_set_text (GPasteClipboard *self)
{
    g_return_val_if_fail (G_PASTE_IS_CLIPBOARD (self), NULL);

    GPasteClipboardPrivate *priv = g_paste_clipboard_get_instance_private (self);
    G_PASTE_CLEANUP_FREE gchar *text = gtk_clipboard_wait_for_text (priv->real);

    if (!text)
        return NULL;

    GPasteSettings *settings = priv->settings;
    G_PASTE_CLEANUP_FREE gchar *stripped = g_strstrip (g_strdup (text));
    gboolean trim_items = g_paste_settings_get_trim_items (settings);
    const gchar *to_add = trim_items ? stripped : text;
    gsize length = strlen (to_add);

    if (length < g_paste_settings_get_min_text_item_size (settings) ||
        length > g_paste_settings_get_max_text_item_size (settings) ||
        !strlen (stripped))
            return NULL;
    if (priv->text && !g_strcmp0 (priv->text, to_add))
        return NULL;

    if (trim_items &&
        priv->target == GDK_SELECTION_CLIPBOARD &&
        g_strcmp0 (text, stripped))
            g_paste_clipboard_select_text (self, stripped);
    else
        g_paste_clipboard_private_set_text (priv, to_add);

    return priv->text;
}

/**
 * g_paste_clipboard_select_text:
 * @self: a #GPasteClipboard instance
 * @text: the text to select
 *
 * Put the text into the #GPasteClipbaord and the intern GtkClipboard
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_clipboard_select_text (GPasteClipboard *self,
                               const gchar     *text)
{
    g_return_if_fail (G_PASTE_IS_CLIPBOARD (self));
    g_return_if_fail (text);
    g_return_if_fail (g_utf8_validate (text, -1, NULL));

    GPasteClipboardPrivate *priv = g_paste_clipboard_get_instance_private (self);
    GtkClipboard *real = priv->real;

    /* Let the clipboards manager handle our internal text */
    gtk_clipboard_set_text (real, text, -1);
    gtk_clipboard_store (real);
}

static void
g_paste_clipboard_get_clipboard_data (GtkClipboard     *clipboard G_GNUC_UNUSED,
                                      GtkSelectionData *selection_data,
                                      guint             info G_GNUC_UNUSED,
                                      gpointer          user_data_or_owner)
{
    g_return_if_fail (G_PASTE_IS_ITEM (user_data_or_owner));

    GPasteItem *item = G_PASTE_ITEM (user_data_or_owner);

    GdkAtom targets[1] = { gtk_selection_data_get_target (selection_data) };

    /* The content is requested as text */
    if (gtk_targets_include_text (targets, 1))
        gtk_selection_data_set_text (selection_data, g_paste_item_get_value (item), -1);
    else if (G_PASTE_IS_IMAGE_ITEM (item))
    {
        if (gtk_targets_include_image (targets, 1, TRUE))
            gtk_selection_data_set_pixbuf (selection_data, g_paste_image_item_get_image (G_PASTE_IMAGE_ITEM (item)));
    }
    /* The content is requested as uris */
    else
    {
        g_return_if_fail (G_PASTE_IS_URIS_ITEM (item));

        const gchar * const *uris = g_paste_uris_item_get_uris (G_PASTE_URIS_ITEM (item));

        if (gtk_targets_include_uri (targets, 1))
            gtk_selection_data_set_uris (selection_data, (GStrv) uris);
        /* The content is requested as special gnome-copied-files by nautilus */
        else
        {
            G_PASTE_CLEANUP_STRING_FREE GString *copy_string = g_string_new ("copy");
            guint length = g_strv_length ((GStrv) uris);

            for (guint i = 0; i < length; ++i)
            {
                g_string_append (g_string_append (copy_string,
                                                  "\n"),
                                 uris[i]);
            }

            gchar *str = copy_string->str;
            length = copy_string->len + 1;
            G_PASTE_CLEANUP_FREE guchar *copy_files_data = g_new (guchar, length);
            for (guint i = 0; i < length; ++i)
                copy_files_data[i] = (guchar) str[i];
            gtk_selection_data_set (selection_data, g_paste_clipboard_copy_files_target, 8, copy_files_data, length);
        }
    }
}

static void
g_paste_clipboard_clear_clipboard_data (GtkClipboard *clipboard G_GNUC_UNUSED,
                                        gpointer      user_data_or_owner)
{
    g_object_unref (user_data_or_owner);
}

static void
g_paste_clipboard_private_select_uris (GPasteClipboardPrivate *priv,
                                       GPasteUrisItem         *item)
{
    GtkClipboard *real = priv->real;
    G_PASTE_CLEANUP_TARGETS_UNREF GtkTargetList *target_list = gtk_target_list_new (NULL, 0);

    g_paste_clipboard_private_set_text (priv, g_paste_item_get_value (G_PASTE_ITEM (item)));

    gtk_target_list_add_text_targets (target_list, 0);
    gtk_target_list_add_uri_targets (target_list, 0);
    gtk_target_list_add (target_list, g_paste_clipboard_copy_files_target, 0, 0);

    gint n_targets;
    GtkTargetEntry *targets = gtk_target_table_new_from_list (target_list, &n_targets);
    gtk_clipboard_set_with_owner (real,
                                  targets,
                                  n_targets,
                                  g_paste_clipboard_get_clipboard_data,
                                  g_paste_clipboard_clear_clipboard_data,
                                  g_object_ref (item));
    gtk_clipboard_store (real);

    gtk_target_table_free (targets, n_targets);
}

/**
 * g_paste_clipboard_get_image_checksum:
 * @self: a #GPasteClipboard instance
 *
 * Get the checksum of the image stored in the #GPasteClipboard
 *
 * Returns: read-only string containing the checksum of the image stored in the #GPasteClipboard or NULL
 */
G_PASTE_VISIBLE const gchar *
g_paste_clipboard_get_image_checksum (const GPasteClipboard *self)
{
    g_return_val_if_fail (G_PASTE_IS_CLIPBOARD (self), NULL);

    GPasteClipboardPrivate *priv = g_paste_clipboard_get_instance_private ((GPasteClipboard *) self);

    return priv->image_checksum;
}

static void
g_paste_clipboard_private_set_image_checksum (GPasteClipboardPrivate *priv,
                                              const gchar            *image_checksum)
{
    g_free (priv->text);
    g_free (priv->image_checksum);

    priv->text = NULL;
    priv->image_checksum = g_strdup (image_checksum);
}

static void
g_paste_clipboard_private_select_image (GPasteClipboardPrivate *priv,
                                        GdkPixbuf              *image,
                                        const gchar            *checksum)
{
    g_return_if_fail (image);

    GtkClipboard *real = priv->real;

    g_paste_clipboard_private_set_image_checksum (priv, checksum);
    gtk_clipboard_set_image (real, image);
    gtk_clipboard_store (real);
}

/**
 * g_paste_clipboard_set_image:
 * @self: a #GPasteClipboard instance
 *
 * Put the image from the intern GtkClipboard in the #GPasteClipboard
 *
 * Returns: (transfer full): The new image if it was modified, or NULL
 */
G_PASTE_VISIBLE GdkPixbuf *
g_paste_clipboard_set_image (GPasteClipboard *self)
{
    g_return_val_if_fail (G_PASTE_IS_CLIPBOARD (self), NULL);

    GPasteClipboardPrivate *priv = g_paste_clipboard_get_instance_private (self);
    GdkPixbuf *image = gtk_clipboard_wait_for_image (priv->real);
    GdkPixbuf *ret = image;

    if (image)
    {
        G_PASTE_CLEANUP_FREE gchar *checksum = g_compute_checksum_for_data (G_CHECKSUM_SHA256,
                                                                            (guchar *) gdk_pixbuf_get_pixels (image),
                                                                            -1);

        if (g_strcmp0 (checksum, priv->image_checksum))
        {
            g_paste_clipboard_private_select_image (priv,
                                                    image,
                                                    checksum);
        }
        else
            ret = NULL;
    }

    return ret;
}

/**
 * g_paste_clipboard_select_item:
 * @self: a #GPasteClipboard instance
 * @item: the item to select
 *
 * Put the value of the item into the #GPasteClipbaord and the intern GtkClipboard
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_clipboard_select_item (GPasteClipboard  *self,
                               const GPasteItem *item)
{
    g_return_if_fail (G_PASTE_IS_CLIPBOARD (self));
    g_return_if_fail (G_PASTE_IS_ITEM (item));

    GPasteClipboardPrivate *priv = g_paste_clipboard_get_instance_private (self);

    if (G_PASTE_IS_IMAGE_ITEM (item))
    {
        GPasteImageItem *image_item = G_PASTE_IMAGE_ITEM (item);
        const gchar *checksum = g_paste_image_item_get_checksum (image_item);

        if (g_strcmp0 (checksum, priv->image_checksum))
        {
            g_paste_clipboard_private_select_image (priv,
                                                    g_paste_image_item_get_image (image_item),
                                                    checksum);
        }
    }
    else
    {
        const gchar *text = g_paste_item_get_value (item);

        if (g_strcmp0 (text, priv->text))
        {
            if (G_PASTE_IS_URIS_ITEM (item))
                g_paste_clipboard_private_select_uris (priv, G_PASTE_URIS_ITEM (item));
            else  if (G_PASTE_IS_TEXT_ITEM (item))
                g_paste_clipboard_select_text (self, text);
            else
                g_assert_not_reached ();
        }
    }
}

static void
g_paste_clipboard_owner_change (GtkClipboard *clipboard G_GNUC_UNUSED,
                                GdkEvent     *event,
                                gpointer      user_data)
{
    GPasteClipboard *self = user_data;

    g_signal_emit (self,
		   signals[OWNER_CHANGE],
                   0, /* detail */
                   event,
                   NULL);
}

static void
g_paste_clipboard_dispose (GObject *object)
{
    GPasteClipboardPrivate *priv = g_paste_clipboard_get_instance_private (G_PASTE_CLIPBOARD (object));

    if (priv->settings)
    {
        g_signal_handler_disconnect (priv->real, priv->owner_change_signal);
        g_clear_object (&priv->settings);
    }

    G_OBJECT_CLASS (g_paste_clipboard_parent_class)->dispose (object);
}

static void
g_paste_clipboard_finalize (GObject *object)
{
    GPasteClipboardPrivate *priv = g_paste_clipboard_get_instance_private (G_PASTE_CLIPBOARD (object));

    g_free (priv->text);
    g_free (priv->image_checksum);

    G_OBJECT_CLASS (g_paste_clipboard_parent_class)->finalize (object);
}

static void
g_paste_clipboard_class_init (GPasteClipboardClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_clipboard_dispose;
    object_class->finalize = g_paste_clipboard_finalize;

    signals[OWNER_CHANGE] = g_signal_new ("owner-change",
                                          G_PASTE_TYPE_CLIPBOARD,
                                          G_SIGNAL_RUN_FIRST,
                                          0,    /* class offset     */
                                          NULL, /* accumulator      */
                                          NULL, /* accumulator data */
                                          g_cclosure_marshal_VOID__BOXED,
                                          G_TYPE_NONE,
                                          1,
                                          GDK_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);
}

static void
g_paste_clipboard_init (GPasteClipboard *self)
{
    GPasteClipboardPrivate *priv = g_paste_clipboard_get_instance_private (self);

    priv->text = NULL;
    priv->image_checksum = NULL;
}

/**
 * g_paste_clipboard_new:
 * @target: the GdkAtom representating the GtkClipboard we're abstracting
 * @settings: a #GPasteSettings instance
 *
 * Create a new instance of #GPasteClipboard
 *
 * Returns: a newly allocated #GPasteClipboard
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteClipboard *
g_paste_clipboard_new (GdkAtom         target,
                       GPasteSettings *settings)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    if (!gdk_display_request_selection_notification (gdk_display_get_default (), target))
        g_error ("Clipboard notifications not supported, GPaste won't work (XFixes not found).");

    GPasteClipboard *self = g_object_new (G_PASTE_TYPE_CLIPBOARD, NULL);
    GPasteClipboardPrivate *priv = g_paste_clipboard_get_instance_private (self);

    priv->target = target;
    priv->settings = g_object_ref (settings);

    GtkClipboard *real = priv->real = gtk_clipboard_get (target);

    priv->owner_change_signal = g_signal_connect (G_OBJECT (real),
                                                  "owner-change",
                                                  G_CALLBACK (g_paste_clipboard_owner_change),
                                                  self);

    return self;
}
