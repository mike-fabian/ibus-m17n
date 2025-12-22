/* vim:set et sts=4: */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gio/gio.h>
#include <gtk/gtk.h>
#include <ibus.h>
#include <m17n.h>
#include <string.h>
#include <errno.h>
#include "m17nutil.h"

enum {
    COLUMN_KEY,
    COLUMN_VALUE,
    COLUMN_DESCRIPTION,
    NUM_COLS
};

struct _SetupDialog {
    GtkWindow *dialog;
#if GTK_API_MAJOR >= 4
    GtkButton       *button_close;
    GtkDropDown     *combobox_underline;
    GtkDropDown     *combobox_orientation;
    GtkCheckButton  *checkbutton_foreground;
    GtkCheckButton  *checkbutton_background;
    GtkCheckButton  *checkbutton_use_us_layout;
#else
    GtkComboBox     *combobox_underline;
    GtkComboBox     *combobox_orientation;
    GtkToggleButton *checkbutton_foreground;
    GtkToggleButton *checkbutton_background;
    GtkToggleButton *checkbutton_use_us_layout;
#endif
    GtkColorButton *colorbutton_background;
    GtkColorButton *colorbutton_foreground;

    GtkTreeView *treeview;
    GtkListStore *store;

    MSymbol lang;
    MSymbol name;

    GSettings *gsettings;
};
typedef struct _SetupDialog SetupDialog;

static gchar *opt_name = NULL;
static const GOptionEntry options[] = {
    {"name", '\0', 0, G_OPTION_ARG_STRING, &opt_name,
     "IBus engine name like \"m17n:si:wijesekara\"."},
    {NULL}
};

static gchar *
format_m17n_value (MPlist *plist)
{
    if (mplist_key (plist) == Msymbol)
        return g_strdup (msymbol_name ((MSymbol) mplist_value (plist)));

    if (mplist_key (plist) == Mtext)
        return g_strdup (mtext_data ((MText *) mplist_value (plist),
                                     NULL, NULL, NULL, NULL));

    if (mplist_key (plist) == Minteger)
        return g_strdup_printf ("%d", (gint) (long) mplist_value (plist));

    return NULL;
}

static MPlist *
parse_m17n_value (MPlist *plist, gchar *text)
{
    MPlist *value;

    if (mplist_key (plist) == Msymbol) {
        value = mplist ();
        mplist_add (value, Msymbol, msymbol (text));
        return value;
    }

    if (mplist_key (plist) == Mtext) {
        MText *mtext;

        mtext = mconv_decode_buffer (Mcoding_utf_8,
                                     (const unsigned char *) text,
                                     strlen (text));
        value = mplist ();
        mplist_add (value, Mtext, mtext);
        return value;
    }

    if (mplist_key (plist) == Minteger) {
        long val;

        errno = 0;
        val = strtol (text, NULL, 10);
        if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN))
            || (errno != 0 && val == 0))
            return NULL;
        value = mplist ();
        mplist_add (value, Minteger, (void *)val);
        return value;
    }

    return NULL;
}

static void
insert_m17n_items (GtkListStore *store, MSymbol language, MSymbol name)
{
    MPlist *plist;

    plist = minput_get_variable (language, name, Mnil);

    for (; plist && mplist_key (plist) == Mplist; plist = mplist_next (plist)) {
        GtkTreeIter iter;
        MSymbol key;
        MPlist *p, *mvalue;
        gchar *description, *value;

        p = mplist_value (plist);
        key = mplist_value (p); /* name */

        p = mplist_next (p);  /* description */
        description = ibus_m17n_mtext_to_utf8 ((MText *) mplist_value (p));
        p = mplist_next (p);  /* status */
        mvalue = mplist_next (p);
        value = format_m17n_value (mvalue);

        gtk_list_store_append (store, &iter);
        gtk_list_store_set (store, &iter,
                            COLUMN_KEY, msymbol_name (key),
                            COLUMN_DESCRIPTION, description,
                            COLUMN_VALUE, value,
                            -1);
        g_free (description);
        g_free (value);
    }
}

static gboolean
on_query_tooltip (GtkTreeView *treeview,
                  gint        x,
                  gint        y,
                  gboolean    keyboard_tip,
                  GtkTooltip *tooltip,
                  gpointer    user_data)
{
    GtkTreeModel *model = gtk_tree_view_get_model (treeview);
    GtkTreePath *path = NULL;
    GtkTreeIter iter;
    gchar *description;
#if GTK_API_MAJOR >= 4
    if (!gtk_tree_view_get_tooltip_context (treeview, x, y,
                                            keyboard_tip,
                                            &model, &path, &iter))
        return FALSE;
#else
    if (!gtk_tree_view_get_tooltip_context (treeview,
                                            &x, &y,
                                            keyboard_tip,
                                            &model, &path, &iter))
        return FALSE;
#endif
    gtk_tree_model_get (model, &iter, COLUMN_DESCRIPTION, &description, -1);
    gtk_tooltip_set_text (tooltip, description);
    gtk_tree_view_set_tooltip_row (treeview, tooltip, path);
    gtk_tree_path_free (path);
    g_free (description);
    return TRUE;
}

static void
on_edited (GtkCellRendererText *cell,
           gchar               *path_string,
           gchar               *new_text,
           gpointer             data)
{
    SetupDialog *dialog = data;
    GtkTreeModel *model = GTK_TREE_MODEL (dialog->store);
    GtkTreeIter iter;
    GtkTreePath *path = gtk_tree_path_new_from_string (path_string);

    gtk_tree_model_get_iter (model, &iter, path);

    gtk_list_store_set (dialog->store, &iter,
                        COLUMN_VALUE, new_text,
                        -1);
    gtk_tree_path_free (path);
}

static void
toggle_colorbutton_sensitive (
#if GTK_API_MAJOR >= 4
    GtkCheckButton *button,
#else
    GtkToggleButton *button,
#endif
    GtkColorButton  *colorbutton)
{
#if GTK_API_MAJOR >= 4
    gtk_widget_set_sensitive (GTK_WIDGET (colorbutton),
                              gtk_check_button_get_active (button));
#else
    gtk_widget_set_sensitive (GTK_WIDGET (colorbutton),
                              gtk_toggle_button_get_active (button));
#endif
}

static void
on_foreground_toggled (
#if GTK_API_MAJOR >= 4
    GtkCheckButton *button,
#else
    GtkToggleButton *button,
#endif
    gpointer         user_data)
{
    SetupDialog *dialog = user_data;
    toggle_colorbutton_sensitive (button, dialog->colorbutton_foreground);
}

static void
on_background_toggled (
#if GTK_API_MAJOR >= 4
    GtkCheckButton *button,
#else
    GtkToggleButton *button,
#endif
    gpointer         user_data)
{
    SetupDialog *dialog = user_data;
    toggle_colorbutton_sensitive (button, dialog->colorbutton_background);
}

static gint
get_combo_box_index_by_value (GtkComboBox *combobox,
                              gint         value)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    gint index;

    index = 0;
    model = gtk_combo_box_get_model (combobox);
    if (!gtk_tree_model_get_iter_first (model, &iter))
        return -1;

    do {
        gint _value;
        gtk_tree_model_get (model, &iter, COLUMN_VALUE, &_value, -1);
        if (_value == value)
            return index;
        index++;
    } while (gtk_tree_model_iter_next (model, &iter));
    return -1;
}

#if GTK_CHECK_VERSION(3,0,0)
static void
_gdk_rgba_from_uint (GdkRGBA *rgba,
                     guint    color)
{
    rgba->red = ((color >> 8) & 0xFF00) / 65535.;
    rgba->green = (color & 0xFF00) / 65535.;
    rgba->blue = ((color & 0xFF) << 8) / 65535.;
    rgba->alpha = 1.0;
}

static gchar *
_gdk_rgba_to_string (GdkRGBA *rgba)
{
    return g_strdup_printf ("#%02X%02X%02X",
                            (gint) (rgba->red * 255),
                            (gint) (rgba->green * 255),
                            (gint) (rgba->blue * 255));
}

static void
set_color_string (GtkColorButton *colorbutton, const gchar *color)
{
    GdkRGBA rgba;
    gdk_rgba_parse (&rgba, color);
    gtk_color_chooser_set_rgba (GTK_COLOR_CHOOSER (colorbutton), &rgba);
}

static void
set_color_uint (GtkColorButton *colorbutton, guint color)
{
    GdkRGBA rgba;
    _gdk_rgba_from_uint (&rgba, color);
    gtk_color_chooser_set_rgba (GTK_COLOR_CHOOSER (colorbutton), &rgba);
}

static gchar *
get_color_string (GtkColorButton *colorbutton)
{
    GdkRGBA rgba;
    gtk_color_chooser_get_rgba (GTK_COLOR_CHOOSER (colorbutton), &rgba);
    return _gdk_rgba_to_string (&rgba);
}
#else
static void
_gdk_color_from_uint (GdkColor *color_gdk,
                      guint     color)
{
    color_gdk->pixel = 0;
    color_gdk->red = (color >> 8) & 0xFF00;
    color_gdk->green = color & 0xFF00;
    color_gdk->blue = (color & 0xFF) << 8;
}

static gchar *
_gdk_color_to_string (GdkColor *color)
{
    return g_strdup_printf ("#%02X%02X%02X",
                            (color->red & 0xFF00) >> 8,
                            (color->green & 0xFF00) >> 8,
                            (color->blue & 0xFF00) >> 8);
}

static void
set_color_string (GtkColorButton *colorbutton, const gchar *color)
{
    GdkColor cvalue;
    gdk_color_parse (color, &cvalue);
    gtk_color_button_set_color (colorbutton, &cvalue);
}

static void
set_color_uint (GtkColorButton *colorbutton, guint color)
{
    GdkColor cvalue;
    _gdk_color_from_uint (&cvalue, color);
    gtk_color_button_set_color (colorbutton, &cvalue);
}

static gchar *
get_color_string (GtkColorButton *colorbutton)
{
    GdkColor color;
    gtk_color_button_get_color (colorbutton, &color);
    return _gdk_color_to_string (&color);
}
#endif

static void
load_color (GSettings       *gsettings,
#if GTK_API_MAJOR >= 4
            GtkCheckButton *button,
#else
            GtkToggleButton *button,
#endif
            GtkColorButton  *colorbutton,
            const gchar     *key,
            guint            defcol)
{
    GVariant *value;
    gboolean bvalue;

    bvalue = FALSE;
    value = g_settings_get_value (gsettings, key);
    if (value != NULL) {
        const gchar *svalue = g_variant_get_string (value, NULL);
        if (g_strcmp0 (svalue, "none") != 0) {
            set_color_string (colorbutton, svalue);
            bvalue = TRUE;
        }
        g_variant_unref (value);
    }
    if (!bvalue) {
        set_color_uint (colorbutton, defcol);
    }
#if GTK_API_MAJOR >= 4
    gtk_check_button_set_active (button, bvalue);
#else
    gtk_toggle_button_set_active (button, bvalue);
#endif
    gtk_widget_set_sensitive (GTK_WIDGET (colorbutton), bvalue);
}

#if GTK_API_MAJOR >= 4
static void
load_choice (GSettings   *gsettings,
             GtkDropDown *dropdown,
             const gchar *key,
             gint         defval)
{
    GVariant *value;
    gint ivalue;

    ivalue = defval;
    value = g_settings_get_value (gsettings, key);
    if (value != NULL) {
        ivalue = g_variant_get_int32 (value);
        g_variant_unref (value);
    }
    /* enum values map directly to GtkStringList order */
    gtk_drop_down_set_selected (dropdown, ivalue);
}
#endif

#if GTK_API_MAJOR < 4
static void
load_choice (GSettings   *gsettings,
             GtkComboBox *combo,
             const gchar *key,
             gint         defval)
{
    GVariant *value;
    gint ivalue, index;
    GtkCellRenderer *renderer;

    renderer = gtk_cell_renderer_text_new ();
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo), renderer, TRUE);
    gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combo),
                                    renderer, "text", 0, NULL);

    ivalue = defval;
    value = g_settings_get_value (gsettings, key);
    if (value != NULL) {
        ivalue = g_variant_get_int32 (value);
        g_variant_unref (value);
    }

    index = get_combo_box_index_by_value (GTK_COMBO_BOX (combo), ivalue);
    gtk_combo_box_set_active (GTK_COMBO_BOX (combo), index);
}
#endif

static void
load_toggle (GSettings       *gsettings,
#if GTK_API_MAJOR >= 4
             GtkCheckButton *button,
#else
             GtkToggleButton *button,
#endif
             const gchar     *key,
             gboolean         defval)
{
    GVariant *value;
    gboolean bvalue;

    bvalue = defval;
    value = g_settings_get_value (gsettings, key);
    if (value != NULL) {
        bvalue = g_variant_get_boolean (value);
        g_variant_unref (value);
    }
#if GTK_API_MAJOR >= 4
    gtk_check_button_set_active(button, bvalue);
#else
    gtk_toggle_button_set_active (button, bvalue);
#endif
}

static void
setup_dialog_load_config (SetupDialog *dialog)
{
    GtkCellRenderer *renderer;

    /* General -> Pre-edit Appearance */
    /* foreground color of pre-edit buffer */
    load_color (dialog->gsettings,
                dialog->checkbutton_foreground,
                dialog->colorbutton_foreground,
                "preedit-foreground",
                PREEDIT_FOREGROUND);
    g_signal_connect (dialog->checkbutton_foreground, "toggled",
                      G_CALLBACK (on_foreground_toggled), dialog);

    /* background color of pre-edit buffer */
    load_color (dialog->gsettings,
                dialog->checkbutton_background,
                dialog->colorbutton_background,
                "preedit-background",
                PREEDIT_BACKGROUND);
    g_signal_connect (dialog->checkbutton_background, "toggled",
                      G_CALLBACK (on_background_toggled), dialog);

    /* underline of pre-edit buffer */
    load_choice (dialog->gsettings,
                 dialog->combobox_underline,
                 "preedit-underline",
                 IBUS_ATTR_UNDERLINE_NONE);

    /* General -> Other */
    /* lookup table orientation */
    load_choice (dialog->gsettings,
                 dialog->combobox_orientation,
                 "lookup-table-orientation",
                 IBUS_ORIENTATION_SYSTEM);

    /* Use US keyboard layout */
    load_toggle (dialog->gsettings,
                 dialog->checkbutton_use_us_layout,
                 "use-us-layout",
                 FALSE);

    /* Advanced -> m17n-lib configuration */
    dialog->store = gtk_list_store_new (NUM_COLS,
                                        G_TYPE_STRING,
                                        G_TYPE_STRING,
                                        G_TYPE_STRING);
    insert_m17n_items (dialog->store, dialog->lang, dialog->name);

    gtk_tree_view_set_model (dialog->treeview,
                             GTK_TREE_MODEL (dialog->store));

    renderer = gtk_cell_renderer_text_new ();
    gtk_tree_view_insert_column_with_attributes
        (dialog->treeview, -1,
         "Key",
         renderer,
         "text", COLUMN_KEY,
         NULL);
    renderer = gtk_cell_renderer_text_new ();
    gtk_tree_view_insert_column_with_attributes
        (dialog->treeview, -1,
         "Value",
         renderer,
         "text", COLUMN_VALUE,
         NULL);
    g_object_set (renderer, "editable", TRUE, NULL);
    g_signal_connect (renderer, "edited", G_CALLBACK (on_edited), dialog);

    g_signal_connect (dialog->treeview, "query-tooltip",
                      G_CALLBACK (on_query_tooltip), NULL);
}

static void
save_color (SetupDialog     *dialog,
#if GTK_API_MAJOR >= 4
            GtkCheckButton *button,
#else
            GtkToggleButton *button,
#endif
            GtkColorButton  *colorbutton,
            const gchar     *key)
{
    GVariant *value;
    gboolean is_active;

#if GTK_API_MAJOR >= 4
    is_active = gtk_check_button_get_active (button);
#else
    is_active = gtk_toggle_button_get_active (button);
#endif
    if (is_active) {
        gchar *color = get_color_string (colorbutton);
        value = g_variant_new_string (color);
        g_free (color);
    } else {
        value = g_variant_new_string ("none");
    }
    g_settings_set_value (dialog->gsettings, key, value);
}

#if GTK_API_MAJOR >= 4
static void
save_choice (SetupDialog *dialog,
             GtkDropDown *dropdown,
             const gchar *key)
{
    guint index;
    GVariant *value;

    index = gtk_drop_down_get_selected (dropdown);

    value = g_variant_new_int32 ((gint) index);
    g_settings_set_value (dialog->gsettings, key, value);
}
#endif

#if GTK_API_MAJOR < 4
static void
save_choice (SetupDialog *dialog,
             GtkComboBox *combo,
             const gchar *key)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    gint active;
    GVariant *value;

    model = gtk_combo_box_get_model (combo);
    gtk_combo_box_get_active_iter (combo, &iter);
    gtk_tree_model_get (model, &iter, COLUMN_VALUE, &active, -1);

    value = g_variant_new_int32 (active);
    g_settings_set_value (dialog->gsettings, key, value);
}
#endif

static void
save_toggle (SetupDialog     *dialog,
#if GTK_API_MAJOR >= 4
             GtkCheckButton *button,
#else
             GtkToggleButton *button,
#endif
             const gchar     *key)
{
    GVariant *value;
#if GTK_API_MAJOR >= 4
    value = g_variant_new_boolean (gtk_check_button_get_active (button));
#else
    value = g_variant_new_boolean (gtk_toggle_button_get_active (button));
#endif
    g_settings_set_value (dialog->gsettings, key, value);
}

static gboolean
save_m17n_options (SetupDialog *dialog)
{
    GtkTreeModel *model = GTK_TREE_MODEL (dialog->store);
    GtkTreeIter iter;
    MPlist *plist, *p, *mvalue = NULL;
    gchar *key = NULL, *value = NULL;
    gboolean retval = TRUE;

    if (!gtk_tree_model_get_iter_first (model, &iter))
        return FALSE;

    do {
        gtk_tree_model_get (model, &iter,
                            COLUMN_KEY, &key,
                            COLUMN_VALUE, &value,
                            -1);

        plist = minput_get_variable (dialog->lang, dialog->name, msymbol (key));
        if (!plist) {
            retval = FALSE;
            break;
        }

        p = mplist_next (mplist_next (mplist_next (mplist_value (plist))));
        if (!p) {
            retval = FALSE;
            break;
        }

        mvalue = parse_m17n_value (p, value);
        if (!mvalue) {
            retval = FALSE;
            break;
        }

        if (minput_config_variable (dialog->lang,
                                    dialog->name,
                                    msymbol (key),
                                    mvalue) != 0) {
            retval = FALSE;
            break;
        }

        if (mvalue)
            m17n_object_unref (mvalue);
        g_free (key);
        g_free (value);
        mvalue = NULL;
        key = NULL;
        value = NULL;
    } while (gtk_tree_model_iter_next (model, &iter));

    if (retval && minput_save_config () != 1)
        retval = FALSE;

    if (mvalue)
        m17n_object_unref (mvalue);
    g_free (key);
    g_free (value);

    return retval;
}

static void
setup_dialog_save_config (SetupDialog *dialog)
{
    save_color (dialog,
                dialog->checkbutton_foreground,
                dialog->colorbutton_foreground,
                "preedit-foreground");
    save_color (dialog,
                dialog->checkbutton_background,
                dialog->colorbutton_background,
                "preedit-background");
    save_choice (dialog,
                 dialog->combobox_underline,
                 "preedit-underline");
    save_choice (dialog,
                 dialog->combobox_orientation,
                 "lookup-table-orientation");
    save_toggle (dialog,
                 dialog->checkbutton_use_us_layout,
                 "use-us-layout");
    save_m17n_options (dialog);
    g_settings_sync();
}

#if GTK_API_MAJOR >= 4
#define UI_FILE "ibus-m17n-preferences-gtk4.ui"
#else
#define UI_FILE "ibus-m17n-preferences-gtk3.ui"
#endif

#if GTK_API_MAJOR >= 4
static void
on_close_button_clicked (GtkButton *button, gpointer user_data)
{
    SetupDialog *dialog = user_data;
    gtk_window_close (dialog->dialog);
}

static gboolean
on_window_close_request (GtkWindow *window, gpointer user_data)
{
    GMainLoop *loop = user_data;

    g_assert (loop != NULL);
    g_main_loop_quit (loop);
    return FALSE;  /* allow GTK to close/hide the window */
}
#endif

static SetupDialog *
setup_dialog_new (MSymbol     lang,
                  MSymbol     name)
{
    GtkBuilder *builder;
    SetupDialog *dialog;
    GObject *object;
    GError *error;

    dialog = g_slice_new0 (SetupDialog);
    dialog->lang = lang;
    dialog->name = name;
    dialog->gsettings = g_settings_new_with_path(
        "org.freedesktop.ibus.engine.m17n",
        g_strdup_printf ("/org/freedesktop/ibus/engine/m17n/%s/%s/",
                         msymbol_name (lang),
                         msymbol_name (name)));

    builder = gtk_builder_new ();
    gtk_builder_set_translation_domain (builder, "ibus-m17n");

    error = NULL;
    if (!gtk_builder_add_from_file (builder,
                                   PKGDATADIR "/setup/" UI_FILE,
                                   &error)) {
        g_warning ("can't read %s: %s",
                   PKGDATADIR "/setup/" UI_FILE,
                   error->message);
        g_error_free (error);
        return NULL;
    }

    object = gtk_builder_get_object (builder, "dialog");
    dialog->dialog = GTK_WINDOW (object);
#if GTK_API_MAJOR >= 4
    object = gtk_builder_get_object (builder, "buttonClose");
    dialog->button_close = GTK_BUTTON(object);
    g_signal_connect (dialog->button_close,
                      "clicked",
                      G_CALLBACK (on_close_button_clicked),
                      dialog);
    object = gtk_builder_get_object (builder, "checkbutton_foreground");
    dialog->checkbutton_foreground = GTK_CHECK_BUTTON (object);
    object = gtk_builder_get_object (builder, "checkbutton_background");
    dialog->checkbutton_background = GTK_CHECK_BUTTON (object);
    object = gtk_builder_get_object (builder, "combobox_underline");
    dialog->combobox_underline = GTK_DROP_DOWN (object);
    object = gtk_builder_get_object (builder, "combobox_orientation");
    dialog->combobox_orientation = GTK_DROP_DOWN (object);
    object = gtk_builder_get_object (builder, "checkbutton_use_us_layout");
    dialog->checkbutton_use_us_layout = GTK_CHECK_BUTTON (object);
#else
    object = gtk_builder_get_object (builder, "checkbutton_foreground");
    dialog->checkbutton_foreground = GTK_TOGGLE_BUTTON (object);
    object = gtk_builder_get_object (builder, "checkbutton_background");
    dialog->checkbutton_background = GTK_TOGGLE_BUTTON (object);
    object = gtk_builder_get_object (builder, "combobox_underline");
    dialog->combobox_underline = GTK_COMBO_BOX (object);
    object = gtk_builder_get_object (builder, "combobox_orientation");
    dialog->combobox_orientation = GTK_COMBO_BOX (object);
    object = gtk_builder_get_object (builder, "checkbutton_use_us_layout");
    dialog->checkbutton_use_us_layout = GTK_TOGGLE_BUTTON (object);
#endif
    object = gtk_builder_get_object (builder, "colorbutton_foreground");
    dialog->colorbutton_foreground = GTK_COLOR_BUTTON (object);
    object = gtk_builder_get_object (builder, "colorbutton_background");
    dialog->colorbutton_background = GTK_COLOR_BUTTON (object);

    object = gtk_builder_get_object (builder, "treeview_mim_config");
    dialog->treeview = GTK_TREE_VIEW (object);

    return dialog;
}

static void
setup_dialog_free (SetupDialog *dialog)
{
#if GTK_API_MAJOR >= 4
    g_clear_object (&dialog->dialog);
#else
    gtk_widget_destroy (GTK_WIDGET (dialog->dialog));
#endif
    g_object_unref (dialog->gsettings);
    g_object_unref (dialog->store);
    g_slice_free (SetupDialog, dialog);
}

static void
start (const gchar *engine_name)
{
    gchar **strv;
    SetupDialog *dialog;

    ibus_init ();
    ibus_m17n_init_common ();

    strv = g_strsplit (engine_name, ":", 3);
    g_assert (g_strv_length (strv) == 3);
    g_assert (g_strcmp0 (strv[0], "m17n") == 0);

    /* strv == {"m17n", lang, name, NULL} */
    dialog = setup_dialog_new (msymbol (strv[1]), msymbol (strv[2]));
    if (dialog == NULL) {
        fprintf (stderr, "setup_dialog_new failed.\n");
        exit (1);
    }
    g_strfreev (strv);

    setup_dialog_load_config (dialog);

    gtk_window_set_title(
        dialog->dialog,
        g_strdup_printf("%s %s",
                        gtk_window_get_title(dialog->dialog),
                        engine_name));
    gtk_window_present (dialog->dialog);
#if GTK_API_MAJOR >= 4
{
    GMainLoop *loop = g_main_loop_new (NULL, FALSE);

    g_signal_connect (dialog->dialog,
                      "close-request",
                      G_CALLBACK (on_window_close_request),
                      loop);
    g_main_loop_run (loop);
    g_main_loop_unref (loop);
}
#else
    gtk_dialog_run (GTK_DIALOG (dialog->dialog));
#endif
    setup_dialog_save_config (dialog);
    setup_dialog_free (dialog);

    M17N_FINI ();
}

int
main (gint argc, gchar **argv)
{
    GOptionContext *context;

    context = g_option_context_new ("ibus-setup-m17n");
    g_option_context_add_main_entries (context, options, NULL);
    g_option_context_parse (context, &argc, &argv, NULL);
    g_option_context_free (context);

#if GTK_API_MAJOR >= 4
    gtk_init ();
#else
    gtk_init (&argc, &argv);
#endif

    if (!opt_name) {
        opt_name = (gchar *) g_getenv ("IBUS_ENGINE_NAME");
    }

    if (!opt_name) {
        fprintf (stderr, "can't determine IBus engine name; use --name\n");
        exit (1);
    }

    if (strncmp (opt_name, "m17n:", 5) != 0 ||
        strchr (&opt_name[5], ':') == NULL) {
        fprintf (stderr, "wrong format of IBus engine name\n");
        exit (1);
    }

    start (opt_name);

    return 0;
}
