/*
 *  Copyright (C) 2004, 2005, 2006, 2008, 2009, 2010, 2011 Savoir-Faire Linux Inc.
 *  Author: Emmanuel Milou <emmanuel.milou@savoirfairelinux.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Additional permission under GNU GPL version 3 section 7:
 *
 *  If you modify this program, or any covered work, by linking or
 *  combining it with the OpenSSL project's OpenSSL library (or a
 *  modified version of that library), containing parts covered by the
 *  terms of the OpenSSL or SSLeay licenses, Savoir-Faire Linux Inc.
 *  grants you additional permission to convey the resulting work.
 *  Corresponding Source for a non-source form of such a combination
 *  shall include the source code for the parts of OpenSSL used as well
 *  as that of the covered work.
 */


#include <audioconf.h>
#include <utils.h>
#include <string.h>
#include <eel-gconf-extensions.h>
#include "dbus/dbus.h"
#include "uimanager.h"
#include "mainwindow.h"

/* FIXME: these should be in a struct rather than at file scope */
static GtkListStore *pluginlist;
static GtkListStore *outputlist;
static GtkListStore *inputlist;
static GtkListStore *ringtonelist;

static GtkWidget *output;
static GtkWidget *input;
static GtkWidget *ringtone;
static GtkWidget *plugin;
static GtkWidget *codecMoveUpButton;
static GtkWidget *codecMoveDownButton;
static GtkWidget *codecTreeView;		// View used instead of store to get access to selection
static GtkWidget *pulse;
static GtkWidget *alsabox;
static GtkWidget *alsa_conf;

// Codec properties ID
enum {
    COLUMN_CODEC_ACTIVE,
    COLUMN_CODEC_NAME,
    COLUMN_CODEC_FREQUENCY,
    COLUMN_CODEC_BITRATE,
    CODEC_COLUMN_COUNT
};


static void active_is_always_recording (void);

/**
 * Fills the tree list with supported codecs
 */
static void preferences_dialog_fill_codec_list (account_t *a)
{
    GtkListStore *codecStore;
    GtkTreeIter iter;
    GQueue *current;

    // Get model of view and clear it
    codecStore = GTK_LIST_STORE (gtk_tree_view_get_model (GTK_TREE_VIEW (codecTreeView)));
    gtk_list_store_clear (codecStore);

    current = a ? a->codecs : get_system_codec_list ();
    if (!a) DEBUG("Using system codec list");

    // Insert codecs
    unsigned int i;

    for (i = 0; i < g_queue_get_length(current); i++) {
        codec_t *c = codec_list_get_nth (i, current);

        if (c) {
            DEBUG ("%s", c->name);
            gtk_list_store_append (codecStore, &iter);
            gtk_list_store_set (codecStore, &iter,
                                COLUMN_CODEC_ACTIVE,	c->is_active,									// Active
                                COLUMN_CODEC_NAME,		c->name,										// Name
                                COLUMN_CODEC_FREQUENCY,	g_strdup_printf ("%d kHz", c->sample_rate/1000),	// Frequency (kHz)
                                COLUMN_CODEC_BITRATE,	g_strdup_printf ("%.1f kbps", c->_bitrate),		// Bitrate (kbps)
                                -1);
        }
    }
}

/**
 * Fill store with output audio plugins
 */
void
preferences_dialog_fill_audio_plugin_list()
{
    GtkTreeIter iter;
    gchar** list;
    gchar* managerName;

    gtk_list_store_clear (pluginlist);

    // Call dbus to retreive list
    list = dbus_get_audio_plugin_list();
    // For each API name included in list
    int c = 0;

    if (list != NULL) {
        for (managerName = list[c]; managerName != NULL; managerName = list[c]) {
            c++;
            gtk_list_store_append (pluginlist, &iter);
            gtk_list_store_set (pluginlist, &iter, 0 , managerName, -1);
        }
    }

    list = NULL;
}


/**
 * Fill output audio device store
 */
void
preferences_dialog_fill_output_audio_device_list()
{

    GtkTreeIter iter;
    gchar** list;
    int index;

    gtk_list_store_clear (outputlist);

    // Call dbus to retreive list
    list = dbus_get_audio_output_device_list();

    // For each device name included in list
    int c = 0;

    for (; *list ; list++) {
        index = dbus_get_audio_device_index (*list);
        gtk_list_store_append (outputlist, &iter);
        gtk_list_store_set (outputlist, &iter, 0, *list, 1, index, -1);
        c++;
    }
}


/**
 * Fill rigntone audio device store
 */

void
preferences_dialog_fill_ringtone_audio_device_list()
{

    GtkTreeIter iter;
    gchar** list;
    int index;

    gtk_list_store_clear (ringtonelist);

    // Call dbus to retreive output device
    list = dbus_get_audio_output_device_list();

    // For each device name in the list
    int c = 0;

    for (; *list; list++) {
        index = dbus_get_audio_device_index (*list);
        gtk_list_store_append (ringtonelist, &iter);
        gtk_list_store_set (ringtonelist, &iter, 0, *list, 1, index, -1);
        c++;
    }
}



/**
 * Select active output audio device
 */
void
select_active_output_audio_device()
{
    if (SHOW_ALSA_CONF) {

        GtkTreeModel* model;
        GtkTreeIter iter;
        gchar** devices;
        int currentDeviceIndex;
        int deviceIndex;

        // Select active output device on server
        devices = dbus_get_current_audio_devices_index();
        currentDeviceIndex = atoi (devices[0]);
        DEBUG ("audio device index for output = %d", currentDeviceIndex);
        model = gtk_combo_box_get_model (GTK_COMBO_BOX (output));

        // Find the currently set output device
        gtk_tree_model_get_iter_first (model, &iter);

        do {
            gtk_tree_model_get (model, &iter, 1, &deviceIndex, -1);

            if (deviceIndex == currentDeviceIndex) {
                // Set current iteration the active one
                gtk_combo_box_set_active_iter (GTK_COMBO_BOX (output), &iter);
                return;
            }
        } while (gtk_tree_model_iter_next (model, &iter));

        // No index was found, select first one
        WARN ("Warning : No active output device found");
        gtk_combo_box_set_active (GTK_COMBO_BOX (output), 0);
    }
}


/**
 * Select active output audio device
 */
void
select_active_ringtone_audio_device()
{
    if (SHOW_ALSA_CONF) {

        GtkTreeModel* model;
        GtkTreeIter iter;
        gchar** devices;
        int currentDeviceIndex;
        int deviceIndex;

        // Select active ringtone device on server
        devices = dbus_get_current_audio_devices_index();
        currentDeviceIndex = atoi (devices[2]);
        DEBUG ("audio device index for ringtone = %d", currentDeviceIndex);
        model = gtk_combo_box_get_model (GTK_COMBO_BOX (ringtone));

        // Find the currently set ringtone device
        gtk_tree_model_get_iter_first (model, &iter);

        do {
            gtk_tree_model_get (model, &iter, 1, &deviceIndex, -1);

            if (deviceIndex == currentDeviceIndex) {
                // Set current iteration the active one
                gtk_combo_box_set_active_iter (GTK_COMBO_BOX (ringtone), &iter);
                return;
            }
        } while (gtk_tree_model_iter_next (model, &iter));

        // No index was found, select first one
        WARN ("Warning : No active ringtone device found");
        gtk_combo_box_set_active (GTK_COMBO_BOX (ringtone), 0);
    }
}

/**
 * Fill input audio device store
 */
void
preferences_dialog_fill_input_audio_device_list()
{

    GtkTreeIter iter;
    gchar** list;
    int index ;
    gtk_list_store_clear (inputlist);

    // Call dbus to retreive list
    list = dbus_get_audio_input_device_list();

    // For each device name included in list
    //int c = 0;
    for (; *list; list++) {
        index = dbus_get_audio_device_index (*list);
        gtk_list_store_append (inputlist, &iter);
        gtk_list_store_set (inputlist, &iter, 0, *list, 1, index, -1);
        //c++;
    }

}

/**
 * Select active input audio device
 */
void
select_active_input_audio_device()
{
    if (SHOW_ALSA_CONF) {

        GtkTreeModel* model;
        GtkTreeIter iter;
        gchar** devices;
        int currentDeviceIndex;
        int deviceIndex;

        // Select active input device on server
        devices = dbus_get_current_audio_devices_index();
        currentDeviceIndex = atoi (devices[1]);
        model = gtk_combo_box_get_model (GTK_COMBO_BOX (input));

        // Find the currently set input device
        gtk_tree_model_get_iter_first (model, &iter);

        do {
            gtk_tree_model_get (model, &iter, 1, &deviceIndex, -1);

            if (deviceIndex == currentDeviceIndex) {
                // Set current iteration the active one
                gtk_combo_box_set_active_iter (GTK_COMBO_BOX (input), &iter);
                return;
            }
        } while (gtk_tree_model_iter_next (model, &iter));

        // No index was found, select first one
        WARN ("Warning : No active input device found");
        gtk_combo_box_set_active (GTK_COMBO_BOX (input), 0);
    }
}

void
update_device_widget (gchar *pluginName)
{
    if (g_strcasecmp (pluginName, "default") == 0) {
        gtk_widget_set_sensitive (output, FALSE);
        gtk_widget_set_sensitive (input, FALSE);
        gtk_widget_set_sensitive (ringtone, FALSE);
    } else {
        gtk_widget_set_sensitive (output, TRUE);
        gtk_widget_set_sensitive (input, TRUE);
        gtk_widget_set_sensitive (ringtone, TRUE);
    }
}

/**
 * Select the output audio plugin by calling the server
 */
static void
select_output_audio_plugin (GtkComboBox* widget, gpointer data UNUSED)
{
    GtkTreeModel* model;
    GtkTreeIter iter;
    int comboBoxIndex;
    gchar* pluginName;

    comboBoxIndex = gtk_combo_box_get_active (widget);

    if (comboBoxIndex >= 0) {
        model = gtk_combo_box_get_model (widget);
        gtk_combo_box_get_active_iter (widget, &iter);
        gtk_tree_model_get (model, &iter, 0, &pluginName, -1);
        dbus_set_audio_plugin (pluginName);
        update_device_widget (pluginName);
    }
}

/**
 * Select active output audio plugin
 */
void
select_active_output_audio_plugin()
{
    GtkTreeModel* model;
    GtkTreeIter iter;
    gchar* pluginname;
    gchar* tmp;

    // Select active output device on server
    pluginname = dbus_get_current_audio_output_plugin();
    tmp = pluginname;
    model = gtk_combo_box_get_model (GTK_COMBO_BOX (plugin));

    // Find the current alsa plugin
    gtk_tree_model_get_iter_first (model, &iter);

    do {
        gtk_tree_model_get (model, &iter, 0, &pluginname , -1);

        if (g_strcasecmp (tmp , pluginname) == 0) {
            // Set current iteration the active one
            gtk_combo_box_set_active_iter (GTK_COMBO_BOX (plugin), &iter);
            return;
        }
    } while (gtk_tree_model_iter_next (model, &iter));

    // No index was found, select first one
    WARN ("Warning : No active output device found");
    gtk_combo_box_set_active (GTK_COMBO_BOX (plugin), 0);
}


/**
 * Set the audio output device on the server with its index
 */
static void
select_audio_output_device (GtkComboBox* comboBox, gpointer data UNUSED)
{
    GtkTreeModel* model;
    GtkTreeIter iter;
    int comboBoxIndex;
    int deviceIndex;

    comboBoxIndex = gtk_combo_box_get_active (comboBox);

    if (comboBoxIndex >= 0) {
        model = gtk_combo_box_get_model (comboBox);
        gtk_combo_box_get_active_iter (comboBox, &iter);
        gtk_tree_model_get (model, &iter, 1, &deviceIndex, -1);

        dbus_set_audio_output_device (deviceIndex);
    }
}

/**
 * Set the audio input device on the server with its index
 */
static void
select_audio_input_device (GtkComboBox* comboBox, gpointer data UNUSED)
{
    GtkTreeModel* model;
    GtkTreeIter iter;
    int comboBoxIndex;
    int deviceIndex;

    comboBoxIndex = gtk_combo_box_get_active (comboBox);

    if (comboBoxIndex >= 0) {
        model = gtk_combo_box_get_model (comboBox);
        gtk_combo_box_get_active_iter (comboBox, &iter);
        gtk_tree_model_get (model, &iter, 1, &deviceIndex, -1);

        dbus_set_audio_input_device (deviceIndex);
    }
}


/**
+ * Set the audio ringtone device on the server with its index
+ */
static void
select_audio_ringtone_device (GtkComboBox *comboBox, gpointer data UNUSED)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    int comboBoxIndex;
    int deviceIndex;

    comboBoxIndex = gtk_combo_box_get_active (comboBox);

    if (comboBoxIndex >= 0) {
        model = gtk_combo_box_get_model (comboBox);
        gtk_combo_box_get_active_iter (comboBox, &iter);

        gtk_tree_model_get (model, &iter, 1, &deviceIndex, -1);

        dbus_set_audio_ringtone_device (deviceIndex);
    }
}


/**
 * Toggle move buttons on if a codec is selected, off elsewise
 */
static void
select_codec (GtkTreeSelection *selection, GtkTreeModel *model)
{
    GtkTreeIter iter;

    if (!gtk_tree_selection_get_selected (selection, &model, &iter)) {
        gtk_widget_set_sensitive (GTK_WIDGET (codecMoveUpButton), FALSE);
        gtk_widget_set_sensitive (GTK_WIDGET (codecMoveDownButton), FALSE);
    } else {
        gtk_widget_set_sensitive (GTK_WIDGET (codecMoveUpButton), TRUE);
        gtk_widget_set_sensitive (GTK_WIDGET (codecMoveDownButton), TRUE);
    }
}

/**
 * Toggle active value of codec on click and update changes to the deamon
 * and in configuration files
 */
static void
codec_active_toggled (GtkCellRendererToggle *renderer UNUSED, gchar *path, gpointer data)
{
    GtkTreeIter iter;
    GtkTreePath *treePath;
    GtkTreeModel *model;
    gboolean active;
    char* name;
    char* srate;
    codec_t* codec;
    account_t *acc;

    // Get path of clicked codec active toggle box
    treePath = gtk_tree_path_new_from_string (path);
    model = gtk_tree_view_get_model (GTK_TREE_VIEW (codecTreeView));
    gtk_tree_model_get_iter (model, &iter, treePath);

    // Retrieve userdata
    acc = (account_t*) data;

    if (!acc)
        ERROR ("Aie, no account selected");

    // Get active value and name at iteration
    gtk_tree_model_get (model, &iter,
                        COLUMN_CODEC_ACTIVE, &active,
                        COLUMN_CODEC_NAME, &name,
                        COLUMN_CODEC_FREQUENCY, &srate,
                        -1);

    printf ("%s, %s\n", name, srate);
    printf ("%i\n", g_queue_get_length (acc->codecs));

    if ( (g_strcasecmp (name,"speex") ==0) && (g_strcasecmp (srate,"8 kHz") ==0))
        codec = codec_list_get_by_payload ( (gconstpointer) 110, acc->codecs);
    else if ( (g_strcasecmp (name,"speex") ==0) && (g_strcasecmp (srate,"16 kHz") ==0))
        codec = codec_list_get_by_payload ( (gconstpointer) 111, acc->codecs);
    else if ( (g_strcasecmp (name,"speex") ==0) && (g_strcasecmp (srate,"32 kHz") ==0))
        codec = codec_list_get_by_payload ( (gconstpointer) 112, acc->codecs);
    else
        codec = codec_list_get_by_name ( (gconstpointer) name, acc->codecs);

    // Toggle active value
    active = !active;

    // Store value
    gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                        COLUMN_CODEC_ACTIVE, active,
                        -1);

    gtk_tree_path_free (treePath);

    // Modify codec queue to represent change
    if (active)
        codec_set_active (&codec);
    else
        codec_set_inactive (&codec);
}

/**
 * Move codec in list depending on direction and selected codec and
 * update changes in the daemon list and the configuration files
 */
static void codec_move (gboolean moveUp, gpointer data)
{

    GtkTreeIter iter;
    GtkTreeIter *iter2;
    GtkTreeModel *model;
    GtkTreeSelection *selection;
    GtkTreePath *treePath;
    gchar *path;
    account_t *acc;
    GQueue *acc_q;

    // Get view, model and selection of codec store
    model = gtk_tree_view_get_model (GTK_TREE_VIEW (codecTreeView));
    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (codecTreeView));

    // Retrieve the user data
    acc = (account_t*) data;

    if (acc)
        acc_q = acc->codecs;

    // Find selected iteration and create a copy
    gtk_tree_selection_get_selected (GTK_TREE_SELECTION (selection), &model, &iter);
    iter2 = gtk_tree_iter_copy (&iter);

    // Find path of iteration
    path = gtk_tree_model_get_string_from_iter (GTK_TREE_MODEL (model), &iter);
    treePath = gtk_tree_path_new_from_string (path);
    gint *indices = gtk_tree_path_get_indices (treePath);
    gint indice = indices[0];

    // Depending on button direction get new path
    if (moveUp)
        gtk_tree_path_prev (treePath);
    else
        gtk_tree_path_next (treePath);

    gtk_tree_model_get_iter (model, &iter, treePath);

    // Swap iterations if valid
    if (gtk_list_store_iter_is_valid (GTK_LIST_STORE (model), &iter))
        gtk_list_store_swap (GTK_LIST_STORE (model), &iter, iter2);

    // Scroll to new position
    gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (codecTreeView), treePath, NULL, FALSE, 0, 0);

    // Free resources
    gtk_tree_path_free (treePath);
    gtk_tree_iter_free (iter2);
    g_free (path);

    // Perpetuate changes in codec queue
    if (moveUp)
        codec_list_move_codec_up (indice, &acc_q);
    else
        codec_list_move_codec_down (indice, &acc_q);

}

/**
 * Called from move up codec button signal
 */
static void codec_move_up (GtkButton *button UNUSED, gpointer data)
{
    // Change tree view ordering and get indice changed
    codec_move (TRUE, data);
}

/**
 * Called from move down codec button signal
 */
static void codec_move_down (GtkButton *button UNUSED, gpointer data)
{
    // Change tree view ordering and get indice changed
    codec_move (FALSE, data);
}

GtkWidget* audiocodecs_box (account_t *a)
{
    GtkWidget *ret;
    GtkWidget *scrolledWindow;
    GtkWidget *buttonBox;

    GtkListStore *codecStore;
    GtkCellRenderer *renderer;
    GtkTreeSelection *treeSelection;
    GtkTreeViewColumn *treeViewColumn;

    ret = gtk_hbox_new (FALSE, 10);
    gtk_container_set_border_width (GTK_CONTAINER (ret), 10);

    scrolledWindow = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolledWindow), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolledWindow), GTK_SHADOW_IN);

    gtk_box_pack_start (GTK_BOX (ret), scrolledWindow, TRUE, TRUE, 0);
    codecStore = gtk_list_store_new (CODEC_COLUMN_COUNT,
                                     G_TYPE_BOOLEAN,		// Active
                                     G_TYPE_STRING,		// Name
                                     G_TYPE_STRING,		// Frequency
                                     G_TYPE_STRING,		// Bit rate
                                     G_TYPE_STRING		// Bandwith
                                    );

    // Create codec tree view with list store
    codecTreeView = gtk_tree_view_new_with_model (GTK_TREE_MODEL (codecStore));

    // Get tree selection manager
    treeSelection = gtk_tree_view_get_selection (GTK_TREE_VIEW (codecTreeView));
    g_signal_connect (G_OBJECT (treeSelection), "changed",
                      G_CALLBACK (select_codec),
                      codecStore);

    // Active column
    renderer = gtk_cell_renderer_toggle_new();
    treeViewColumn = gtk_tree_view_column_new_with_attributes ("", renderer, "active", COLUMN_CODEC_ACTIVE, NULL);
    gtk_tree_view_append_column (GTK_TREE_VIEW (codecTreeView), treeViewColumn);

    // Toggle codec active property on clicked
    g_signal_connect (G_OBJECT (renderer), "toggled", G_CALLBACK (codec_active_toggled), (gpointer) a);

    // Name column
    renderer = gtk_cell_renderer_text_new();
    treeViewColumn = gtk_tree_view_column_new_with_attributes (_ ("Name"), renderer, "markup", COLUMN_CODEC_NAME, NULL);
    gtk_tree_view_append_column (GTK_TREE_VIEW (codecTreeView), treeViewColumn);

    // Bit rate column
    renderer = gtk_cell_renderer_text_new();
    treeViewColumn = gtk_tree_view_column_new_with_attributes (_ ("Frequency"), renderer, "text", COLUMN_CODEC_FREQUENCY, NULL);
    gtk_tree_view_append_column (GTK_TREE_VIEW (codecTreeView), treeViewColumn);

    // Bandwith column
    renderer = gtk_cell_renderer_text_new();
    treeViewColumn = gtk_tree_view_column_new_with_attributes (_ ("Bitrate"), renderer, "text", COLUMN_CODEC_BITRATE, NULL);
    gtk_tree_view_append_column (GTK_TREE_VIEW (codecTreeView), treeViewColumn);

    g_object_unref (G_OBJECT (codecStore));
    gtk_container_add (GTK_CONTAINER (scrolledWindow), codecTreeView);

    // Create button box
    buttonBox = gtk_vbox_new (FALSE, 0);
    gtk_container_set_border_width (GTK_CONTAINER (buttonBox), 10);
    gtk_box_pack_start (GTK_BOX (ret), buttonBox, FALSE, FALSE, 0);

    codecMoveUpButton = gtk_button_new_from_stock (GTK_STOCK_GO_UP);
    gtk_widget_set_sensitive (GTK_WIDGET (codecMoveUpButton), FALSE);
    gtk_box_pack_start (GTK_BOX (buttonBox), codecMoveUpButton, FALSE, FALSE, 0);
    g_signal_connect (G_OBJECT (codecMoveUpButton), "clicked", G_CALLBACK (codec_move_up), a);

    codecMoveDownButton = gtk_button_new_from_stock (GTK_STOCK_GO_DOWN);
    gtk_widget_set_sensitive (GTK_WIDGET (codecMoveDownButton), FALSE);
    gtk_box_pack_start (GTK_BOX (buttonBox), codecMoveDownButton, FALSE, FALSE, 0);
    g_signal_connect (G_OBJECT (codecMoveDownButton), "clicked", G_CALLBACK (codec_move_down), a);

    preferences_dialog_fill_codec_list (a);

    return ret;
}

void
select_audio_manager (void)
{
    if (!SHOW_ALSA_CONF && !gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (pulse))) {
        dbus_set_audio_manager (ALSA);
        alsabox = alsa_box();
        gtk_container_add (GTK_CONTAINER (alsa_conf) , alsabox);
        gtk_widget_show (alsa_conf);
        gtk_widget_set_sensitive (GTK_WIDGET (alsa_conf), TRUE);
        gtk_action_set_sensitive (GTK_ACTION (volumeToggle), TRUE);
    } else if (SHOW_ALSA_CONF && gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (pulse))) {
        dbus_set_audio_manager (PULSEAUDIO);
        gtk_container_remove (GTK_CONTAINER (alsa_conf) , alsabox);
        gtk_widget_hide (alsa_conf);
        if (gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (volumeToggle))) {
            main_window_volume_controls (FALSE);
            eel_gconf_set_integer (SHOW_VOLUME_CONTROLS, FALSE);
            gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (volumeToggle), FALSE);
        }

        gtk_action_set_sensitive (GTK_ACTION (volumeToggle), FALSE);
    }
}

void
active_noise_suppress (void)
{

    gchar *state;
    gchar *newstate;

    state = dbus_get_noise_suppress_state();

    if (g_strcmp0 (state, "enabled") == 0) {
        newstate = "disabled";
    } else {
        newstate = "enabled";
    }

    dbus_set_noise_suppress_state (newstate);
}

void
active_echo_cancel(void)
{
    gchar *state;
    gchar *newstate;

    state = dbus_get_echo_cancel_state();

    if(g_strcmp0(state, "enabled") == 0) {
        newstate = "disabled";
    } else {
        newstate = "enabled";
    }

    dbus_set_echo_cancel_state(newstate);
}

void
echo_tail_length_changed(GtkRange *range, gpointer user_data)
{
    (void) user_data; /* UNUSED */
    gint value;
    value = (gint)gtk_range_get_value(range);

    dbus_set_echo_cancel_tail_length(value);
}

void
echo_delay_changed(GtkRange *range, gpointer  user_data)
{
    (void) user_data; /* UNUSED */
    gint value;
    value = (gint)gtk_range_get_value(range);

    dbus_set_echo_cancel_delay(value);
}

void
active_is_always_recording (void)
{
    gboolean enabled = FALSE;

    enabled = dbus_get_is_always_recording();

    if(enabled) {
        enabled = FALSE;
    }
    else {
        enabled = TRUE;
    }

    dbus_set_is_always_recording(enabled);
}

GtkWidget* alsa_box()
{
    GtkWidget *ret;
    GtkWidget *table;
    GtkWidget *item;
    GtkWidget *info_bar;
    GtkCellRenderer *renderer;

    ret = gtk_hbox_new (FALSE, 10);
    gtk_widget_show (ret);

    table = gtk_table_new (6, 3, FALSE);
    gtk_table_set_col_spacing (GTK_TABLE (table), 0, 40);
    gtk_box_pack_start (GTK_BOX (ret) , table , TRUE , TRUE , 1);
    gtk_widget_show (table);

    gchar *message = "<small><i>default</i> plugin always uses internal sound card. Select <i>dmix/dsnoop</i> to use an alternate soundcard.</small>";
    gnome_info_bar (message, GTK_MESSAGE_INFO, &info_bar);
    gtk_table_attach (GTK_TABLE (table), info_bar, 1, 3, 1, 2, GTK_FILL, GTK_SHRINK, 10, 10);

    DEBUG ("Audio: Configuration plugin");
    item = gtk_label_new (_ ("ALSA plugin"));
    gtk_misc_set_alignment (GTK_MISC (item), 0, 0.5);
    gtk_table_attach (GTK_TABLE (table), item, 1, 2, 2, 3, GTK_FILL | GTK_EXPAND, GTK_SHRINK, 0, 0);
    gtk_widget_show (item);
    // Set choices of audio managers
    pluginlist = gtk_list_store_new (1, G_TYPE_STRING);
    preferences_dialog_fill_audio_plugin_list();
    plugin = gtk_combo_box_new_with_model (GTK_TREE_MODEL (pluginlist));
    select_active_output_audio_plugin();
    gtk_label_set_mnemonic_widget (GTK_LABEL (item), plugin);
    g_signal_connect (G_OBJECT (plugin), "changed", G_CALLBACK (select_output_audio_plugin), plugin);

    // Set rendering
    renderer = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (plugin), renderer, TRUE);
    gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (plugin), renderer, "text", 0, NULL);
    gtk_table_attach (GTK_TABLE (table), plugin, 2, 3, 2, 3, GTK_FILL | GTK_EXPAND, GTK_SHRINK, 0, 0);
    gtk_widget_show (plugin);

    // Device : Output device
    // Create title label
    DEBUG ("Audio: Configuration output");
    item = gtk_label_new (_ ("Output"));
    gtk_misc_set_alignment (GTK_MISC (item), 0, 0.5);
    gtk_table_attach (GTK_TABLE (table), item, 1, 2, 3, 4, GTK_FILL | GTK_EXPAND, GTK_SHRINK, 0, 0);
    gtk_widget_show (item);
    // Set choices of output devices
    outputlist = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_INT);
    preferences_dialog_fill_output_audio_device_list();
    output = gtk_combo_box_new_with_model (GTK_TREE_MODEL (outputlist));
    select_active_output_audio_device();
    gtk_label_set_mnemonic_widget (GTK_LABEL (item), output);
    g_signal_connect (G_OBJECT (output), "changed", G_CALLBACK (select_audio_output_device), output);

    // Set rendering
    renderer = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (output), renderer, TRUE);
    gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (output), renderer, "text", 0, NULL);
    gtk_table_attach (GTK_TABLE (table), output, 2, 3, 3, 4, GTK_FILL | GTK_EXPAND, GTK_SHRINK, 0, 0);
    gtk_widget_show (output);

    // Device : Input device
    // Create title label
    DEBUG ("Audio: Configuration input");
    item = gtk_label_new (_ ("Input"));
    gtk_misc_set_alignment (GTK_MISC (item), 0, 0.5);
    gtk_table_attach (GTK_TABLE (table), item, 1, 2, 4, 5, GTK_FILL | GTK_EXPAND, GTK_SHRINK, 0, 0);
    gtk_widget_show (item);

    // Set choices of output devices
    inputlist = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_INT);
    preferences_dialog_fill_input_audio_device_list();
    input = gtk_combo_box_new_with_model (GTK_TREE_MODEL (inputlist));
    select_active_input_audio_device();
    gtk_label_set_mnemonic_widget (GTK_LABEL (item), input);
    g_signal_connect (G_OBJECT (input), "changed", G_CALLBACK (select_audio_input_device), input);

    // Set rendering
    renderer = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (input), renderer, TRUE);
    gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (input), renderer, "text", 0, NULL);
    gtk_table_attach (GTK_TABLE (table), input, 2, 3, 4, 5, GTK_FILL | GTK_EXPAND, GTK_SHRINK, 0, 0);
    gtk_widget_show (input);

    DEBUG ("Audio: Configuration rintgtone");
    item = gtk_label_new (_ ("Ringtone"));
    gtk_misc_set_alignment (GTK_MISC (item), 0, 0.5);
    gtk_table_attach (GTK_TABLE (table), item, 1, 2, 5, 6, GTK_FILL | GTK_EXPAND, GTK_SHRINK, 0, 0);
    gtk_widget_show (item);
    // set choices of ringtone devices
    ringtonelist = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_INT);
    preferences_dialog_fill_ringtone_audio_device_list();
    ringtone = gtk_combo_box_new_with_model (GTK_TREE_MODEL (ringtonelist));
    select_active_ringtone_audio_device();
    gtk_label_set_mnemonic_widget (GTK_LABEL (item), output);
    g_signal_connect (G_OBJECT (ringtone), "changed", G_CALLBACK (select_audio_ringtone_device), output);

    // Set rendering
    renderer = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (ringtone), renderer, TRUE);
    gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (ringtone), renderer, "text", 0, NULL);
    gtk_table_attach (GTK_TABLE (table), ringtone, 2, 3, 5, 6, GTK_FILL | GTK_EXPAND, GTK_SHRINK, 0, 0);
    gtk_widget_show (ringtone);

    gtk_widget_show_all (ret);

    // Update the combo box
    update_device_widget (dbus_get_current_audio_output_plugin());
    return ret;
}

static void record_path_changed (GtkFileChooser *chooser , GtkLabel *label UNUSED)
{
    DEBUG ("record_path_changed");

    gchar* path;
    path = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (chooser));
    DEBUG ("path2 %s", path);
    dbus_set_record_path (path);
}

GtkWidget* create_audio_configuration()
{
    // Main widget
    GtkWidget *ret;
    // Sub boxes
    GtkWidget *frame;
    GtkWidget *enableNoiseReduction;
    GtkWidget *enableEchoCancel;
    gboolean noisesuppressActive;
    gboolean echoCancelActive;
    gchar *state;

    ret = gtk_vbox_new (FALSE, 10);
    gtk_container_set_border_width (GTK_CONTAINER (ret), 10);

    GtkWidget *alsa;
    GtkWidget *table;

    gnome_main_section_new_with_table (_ ("Sound Manager"), &frame, &table, 1, 4);
    gtk_box_pack_start (GTK_BOX (ret), frame, FALSE, FALSE, 0);

    int audio_manager = dbus_get_audio_manager();
    gboolean pulse_audio = FALSE;

    if (audio_manager == PULSEAUDIO) {
        pulse_audio = TRUE;
    }

    pulse = gtk_radio_button_new_with_mnemonic (NULL , _ ("_Pulseaudio"));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pulse), pulse_audio);
    gtk_table_attach (GTK_TABLE (table), pulse, 0, 1, 0, 1, GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

    alsa = gtk_radio_button_new_with_mnemonic_from_widget (GTK_RADIO_BUTTON (pulse),  _ ("_ALSA"));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (alsa), !pulse_audio);
    g_signal_connect (G_OBJECT (alsa), "clicked", G_CALLBACK (select_audio_manager), NULL);
    gtk_table_attach (GTK_TABLE (table), alsa, 1, 2, 0, 1, GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

    // Box for the ALSA configuration
    gnome_main_section_new (_ ("ALSA settings"), &alsa_conf);
    gtk_box_pack_start (GTK_BOX (ret), alsa_conf, FALSE, FALSE, 0);
    gtk_widget_show (alsa_conf);

    if (SHOW_ALSA_CONF) {
        // Box for the ALSA configuration
        alsabox = alsa_box();
        gtk_container_add (GTK_CONTAINER (alsa_conf) , alsabox);
        gtk_widget_hide (alsa_conf);
    }

    // Recorded file saving path
    GtkWidget *label;
    GtkWidget *folderChooser;
    gchar *dftPath;

    /* Get the path where to save audio files */
    dftPath = dbus_get_record_path ();
    DEBUG ("AudioConf: Load recording path %s", dftPath);

    gnome_main_section_new_with_table (_ ("Recordings"), &frame, &table, 2, 3);
    gtk_box_pack_start (GTK_BOX (ret), frame, FALSE, FALSE, 0);

    // label
    label = gtk_label_new (_ ("Destination folder"));
    gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1, GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 5);

    // folder chooser button
    folderChooser = gtk_file_chooser_button_new (_ ("Select a folder"), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
    gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (folderChooser), dftPath);
    g_signal_connect (G_OBJECT (folderChooser) , "selection_changed" , G_CALLBACK (record_path_changed) , NULL);
    gtk_table_attach (GTK_TABLE (table), folderChooser, 1, 2, 0, 1, GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 5);

    // isAlwaysRecording functionality checkbox
    GtkWidget *enableIsAlwaysRecording = NULL;
    gboolean isAlwaysRecording = FALSE;

    isAlwaysRecording = dbus_get_is_always_recording();
    enableIsAlwaysRecording = gtk_check_button_new_with_mnemonic(_("_Always recording"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(enableIsAlwaysRecording), isAlwaysRecording);
    g_signal_connect(G_OBJECT(enableIsAlwaysRecording), "clicked", active_is_always_recording, NULL);
    gtk_table_attach(GTK_TABLE(table), enableIsAlwaysRecording, 0, 1, 1, 2, GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 5);
    gtk_widget_show(GTK_WIDGET(enableIsAlwaysRecording));

    // Box for the voice enhancement configuration
    gnome_main_section_new_with_table (_ ("Voice enhancement settings"), &frame, &table, 2, 1);
    gtk_box_pack_start (GTK_BOX (ret), frame, FALSE, FALSE, 0);

    enableNoiseReduction = gtk_check_button_new_with_mnemonic (_ ("_Noise Reduction"));
    state = dbus_get_noise_suppress_state();
    noisesuppressActive = FALSE;
    if (g_strcmp0 (state, "enabled") == 0)
        noisesuppressActive = TRUE;
    else
        noisesuppressActive = FALSE;
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (enableNoiseReduction), noisesuppressActive);
    g_signal_connect (G_OBJECT (enableNoiseReduction), "clicked", active_noise_suppress, NULL);
    gtk_table_attach (GTK_TABLE (table), enableNoiseReduction, 0, 1, 1, 2, GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

    enableEchoCancel = gtk_check_button_new_with_mnemonic(_("_Echo Cancellation"));
    state = dbus_get_echo_cancel_state();
    echoCancelActive = FALSE;
    if (g_strcmp0(state, "enabled") == 0) {
        echoCancelActive = TRUE;
    }
    else {
        echoCancelActive = FALSE;
    }
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(enableEchoCancel), echoCancelActive);
    g_signal_connect(G_OBJECT(enableEchoCancel), "clicked", active_echo_cancel, NULL);
    gtk_table_attach(GTK_TABLE(table), enableEchoCancel, 0, 1, 2, 3, GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

    /*
    gint value = dbus_get_echo_cancel_tail_length();
    echoTailLength = gtk_hscale_new_with_range(100, 500, 5);
    gtk_range_set_value(GTK_RANGE(echoTailLength), (gdouble)value);
    gtk_table_attach(GTK_TABLE(table), echoTailLength, 0, 1, 3, 4, GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
    g_signal_connect(G_OBJECT(echoTailLength), "value-changed", G_CALLBACK(echo_tail_length_changed), NULL);

    value = dbus_get_echo_cancel_delay();
    echoDelay = gtk_hscale_new_with_range(0, 500, 5);
    gtk_range_set_value(GTK_RANGE(echoDelay), (gdouble)value);
    gtk_table_attach(GTK_TABLE(table), echoDelay, 0, 1, 4, 5, GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
    g_signal_connect(G_OBJECT(echoDelay), "value-changed", G_CALLBACK(echo_delay_changed), NULL);
    */

    gtk_widget_show_all (ret);

    if (!pulse_audio) {
        gtk_widget_show (alsa_conf);
    } else {
        gtk_widget_hide (alsa_conf);
    }

    return ret;
}
