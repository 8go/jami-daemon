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

#include "addressbook-config.h"
#include "gtk2_wrappers.h"
#include "str_utils.h"
#include "dbus.h"
#include "unused.h"
#include "logger.h"
#include "searchbar.h"
#include "contacts/addrbookfactory.h"
#include <glib/gi18n.h>
#include <string.h>
#include <stdlib.h>

static AddressBook_Config *addressbook_config;
static GtkWidget *book_tree_view;

static GtkWidget *photo;
static GtkWidget *cards_label;
static GtkWidget *scale_label;
static GtkWidget *scrolled_label;
static GtkWidget *scrolled_window;
static GtkWidget *scale_button;
static GtkWidget *business;
static GtkWidget *mobile;
static GtkWidget *home;

enum {
    COLUMN_BOOK_ACTIVE, COLUMN_BOOK_NAME, COLUMN_BOOK_UID
};

AddressBook_Config *addressbook_config_load_parameters()
{
    static AddressBook_Config defconfig = {
        .enable = 1,
        .max_results = 30,
        .display_contact_photo = 0,
        .search_phone_business = 1,
        .search_phone_home = 1,
        .search_phone_mobile = 1,
    };

    static AddressBook_Config config;

    GHashTable *params = dbus_get_addressbook_settings();

    if (params) {
        config.enable = (size_t)(g_hash_table_lookup(params, ADDRESSBOOK_ENABLE));
        config.max_results = (size_t)(g_hash_table_lookup(params, ADDRESSBOOK_MAX_RESULTS));
        config.display_contact_photo = (size_t)(g_hash_table_lookup(params, ADDRESSBOOK_DISPLAY_CONTACT_PHOTO));
        config.search_phone_business = (size_t)(g_hash_table_lookup(params, ADDRESSBOOK_DISPLAY_PHONE_BUSINESS));
        config.search_phone_home = (size_t)(g_hash_table_lookup(params, ADDRESSBOOK_DISPLAY_PHONE_HOME));
        config.search_phone_mobile = (size_t)(g_hash_table_lookup(params, ADDRESSBOOK_DISPLAY_PHONE_MOBILE));
    } else
        config = defconfig;

    return &config;
}

void
addressbook_config_save_parameters(void)
{

    GHashTable *params = NULL;

    params = g_hash_table_new(NULL, g_str_equal);
    g_hash_table_replace(params, (gpointer) ADDRESSBOOK_ENABLE,
                         (gpointer)(size_t) addressbook_config->enable);
    g_hash_table_replace(params, (gpointer) ADDRESSBOOK_MAX_RESULTS,
                         (gpointer)(size_t) addressbook_config->max_results);
    g_hash_table_replace(params, (gpointer) ADDRESSBOOK_DISPLAY_CONTACT_PHOTO,
                         (gpointer)(size_t) addressbook_config->display_contact_photo);
    g_hash_table_replace(params, (gpointer) ADDRESSBOOK_DISPLAY_PHONE_BUSINESS,
                         (gpointer)(size_t) addressbook_config->search_phone_business);
    g_hash_table_replace(params, (gpointer) ADDRESSBOOK_DISPLAY_PHONE_HOME,
                         (gpointer)(size_t) addressbook_config->search_phone_home);
    g_hash_table_replace(params, (gpointer) ADDRESSBOOK_DISPLAY_PHONE_MOBILE,
                         (gpointer)(size_t) addressbook_config->search_phone_mobile);

    dbus_set_addressbook_settings(params);

    update_searchbar_addressbook_list();

    // Decrement the reference count
    g_hash_table_unref(params);
}

void
enable_options()
{

    if (!addressbook_config->enable) {
        DEBUG("Disable addressbook options\n");
        gtk_widget_set_sensitive(photo, FALSE);
        gtk_widget_set_sensitive(scrolled_label, FALSE);
        gtk_widget_set_sensitive(cards_label, FALSE);
        gtk_widget_set_sensitive(scrolled_window, FALSE);
        gtk_widget_set_sensitive(scale_button, FALSE);
        gtk_widget_set_sensitive(scale_label, FALSE);
        gtk_widget_set_sensitive(business, FALSE);
        gtk_widget_set_sensitive(mobile, FALSE);
        gtk_widget_set_sensitive(home, FALSE);
        gtk_widget_set_sensitive(book_tree_view, FALSE);


    } else {
        DEBUG("Enable addressbook options\n");
        gtk_widget_set_sensitive(photo, TRUE);
        gtk_widget_set_sensitive(scrolled_label, TRUE);
        gtk_widget_set_sensitive(cards_label, TRUE);
        gtk_widget_set_sensitive(scrolled_window, TRUE);
        gtk_widget_set_sensitive(scale_button, TRUE);
        gtk_widget_set_sensitive(scale_label, TRUE);
        gtk_widget_set_sensitive(business, TRUE);
        gtk_widget_set_sensitive(mobile, TRUE);
        gtk_widget_set_sensitive(home, TRUE);
        gtk_widget_set_sensitive(book_tree_view, TRUE);
    }
}

static void
enable_cb(GtkWidget *widget)
{

    addressbook_config->enable
    = (guint) gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

    enable_options();


}

static void
max_results_cb(GtkSpinButton *button)
{
    addressbook_config->max_results = gtk_spin_button_get_value_as_int(button);
}

static void
display_contact_photo_cb(GtkWidget *widget)
{

    addressbook_config->display_contact_photo
    = (guint) gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void
search_phone_business_cb(GtkWidget *widget)
{

    addressbook_config->search_phone_business
    = (guint) gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void
search_phone_home_cb(GtkWidget *widget)
{

    addressbook_config->search_phone_home = (guint) gtk_toggle_button_get_active(
            GTK_TOGGLE_BUTTON(widget));
}

static void
search_phone_mobile_cb(GtkWidget *widget)
{

    addressbook_config->search_phone_mobile
    = (guint) gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

/**
 * Toggle active value of book on click and update changes to the deamon
 * and in configuration files
 */
static void
addressbook_config_book_active_toggled(
    GtkCellRendererToggle *renderer UNUSED, gchar *path, gpointer data)
{
    GtkTreeIter iter;
    GtkTreePath *treePath;
    GtkTreeModel *model;
    gboolean active;
    book_data_t *book_data;
    gchar* name;
    gchar* uid;

    if (!addrbook)
        return;

    // Get path of clicked book active toggle box
    treePath = gtk_tree_path_new_from_string(path);

    if (!(model = gtk_tree_view_get_model(GTK_TREE_VIEW(data)))) {
        DEBUG("Addressbook: No valid model (%s:%d)", __FILE__, __LINE__);
        return;
    }

    gtk_tree_model_get_iter(model, &iter, treePath);

    // Get active value  at iteration
    gtk_tree_model_get(model, &iter, COLUMN_BOOK_ACTIVE, &active,
                       COLUMN_BOOK_UID, &uid, COLUMN_BOOK_NAME, &name, -1);

    // Toggle active value
    active = !active;

    // Store value
    gtk_list_store_set(GTK_LIST_STORE(model), &iter, COLUMN_BOOK_ACTIVE, active, -1);

    gtk_tree_path_free(treePath);

    // Update current memory stored books data
    book_data = addrbook->get_book_data_by_uid(uid);

    if (book_data == NULL) {
        ERROR("Addressbook: Error: Could not find addressbook %s", uid);
    }

    book_data->active = active;

    // Save data
    gboolean valid;

    // Initiate double array char list for one string
    const gchar** list = (void*) malloc(sizeof(void*));
    int c = 0;

    /* Get the first iter in the list */
    valid = gtk_tree_model_get_iter_first(model, &iter);

    while (valid) {
        // Get active value at iteration
        gtk_tree_model_get(model, &iter, COLUMN_BOOK_ACTIVE, &active,
                           COLUMN_BOOK_UID, &uid, COLUMN_BOOK_NAME, &name, -1);

        if (active) {
            // Reallocate memory each time
            if (c != 0)
                list = (void*) realloc(list, (c + 1) * sizeof(void*));

            *(list + c) = uid;
            c++;
        }

        valid = gtk_tree_model_iter_next(model, &iter);
    }

    // Allocate NULL array at the end for Dbus
    list = (void*) realloc(list, (c + 1) * sizeof(void*));
    *(list + c) = NULL;

    // Call daemon to store in config file
    dbus_set_addressbook_list(list);

    free(list);
}

static void
addressbook_config_fill_book_list()
{
    GtkTreeIter list_store_iterator;
    GSList *book_list_iterator;
    GtkListStore *store;
    book_data_t *book_data;
    gchar **book_list;

    if (!addrbook)
        return;

    book_list = dbus_get_addressbook_list();
    GSList *books_data = addrbook->get_books_data(book_list);

    if (!books_data) {
        DEBUG("Addressbook: No valid books data (%s:%d)", __FILE__, __LINE__);
    }

    // Get model of view and clear it
    if (!(store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(book_tree_view))))) {
        DEBUG("Addressbook: Could not find model from treeview (%s:%d)", __FILE__, __LINE__);
        return;
    }

    gtk_list_store_clear(store);

    // Populate window
    for (book_list_iterator = books_data; book_list_iterator != NULL; book_list_iterator
            = book_list_iterator->next) {
        book_data = (book_data_t *) book_list_iterator->data;
        gtk_list_store_append(store, &list_store_iterator);
        DEBUG("-----------------------------------: %s, %s", book_data->name, book_data->active ? "active" : "not-active");
        gtk_list_store_set(store, &list_store_iterator, COLUMN_BOOK_ACTIVE,
                           book_data->active, COLUMN_BOOK_UID, book_data->uid, COLUMN_BOOK_NAME,
                           book_data->name, -1);
    }

    store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(book_tree_view)));
}

GtkWidget*
create_addressbook_settings()
{
    GtkWidget *result_frame, *table, *value, *item;

    GtkListStore *store;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *tree_view_column;

    // Load the user value
    addressbook_config = addressbook_config_load_parameters();

    GtkWidget *ret = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(ret), 10);

    gnome_main_section_new_with_table(_("General"), &result_frame, &table, 3, 3);
    gtk_box_pack_start(GTK_BOX(ret), result_frame, FALSE, FALSE, 0);

    // PHOTO DISPLAY
    item = gtk_check_button_new_with_mnemonic(_("_Use Evolution address books"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(item), addressbook_config->enable);
    g_signal_connect(G_OBJECT(item) , "clicked" , G_CALLBACK(enable_cb), NULL);
    gtk_table_attach(GTK_TABLE(table), item, 1, 3, 0, 1, GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

    // SCALE BUTTON - NUMBER OF RESULTS
    scale_button = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    scale_label = gtk_label_new(_("Download limit :"));
    gtk_box_pack_start(GTK_BOX(scale_button),scale_label,FALSE,FALSE,0);
    value = gtk_spin_button_new_with_range(1, G_MAXINT, 1);
    gtk_label_set_mnemonic_widget(GTK_LABEL(scale_label), value);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(value) , addressbook_config->max_results);
    g_signal_connect(G_OBJECT(value) , "value-changed" , G_CALLBACK(max_results_cb), NULL);
    gtk_box_pack_start(GTK_BOX(scale_button),value,TRUE,TRUE,10);
    gtk_table_attach(GTK_TABLE(table), scale_button, 1, 2, 1, 2, GTK_EXPAND | GTK_FILL, GTK_EXPAND |GTK_FILL, 0, 0);
    cards_label = gtk_label_new(_("cards"));
    gtk_table_attach(GTK_TABLE(table), cards_label, 2, 3, 1, 2, GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
    gtk_widget_show_all(scale_button);

    // PHOTO DISPLAY
    photo = gtk_check_button_new_with_mnemonic(_("_Display contact photo if available"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(photo), addressbook_config->display_contact_photo);
    g_signal_connect(G_OBJECT(photo) , "clicked" , G_CALLBACK(display_contact_photo_cb), NULL);
    gtk_table_attach(GTK_TABLE(table), photo, 1, 3, 2, 3, GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

    // Fields
    gnome_main_section_new_with_table(_("Fields from Evolution's address books"), &result_frame, &table, 1, 3);
    gtk_box_pack_start(GTK_BOX(ret), result_frame, FALSE, FALSE, 0);

    business = gtk_check_button_new_with_mnemonic(_("_Work"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(business), addressbook_config->search_phone_business);
    g_signal_connect(G_OBJECT(business) , "clicked" , G_CALLBACK(search_phone_business_cb) , NULL);
    gtk_table_attach(GTK_TABLE(table), business, 0, 1, 0, 1, GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
    gtk_widget_set_sensitive(business, FALSE);

    home = gtk_check_button_new_with_mnemonic(_("_Home"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(home), addressbook_config->search_phone_home);
    g_signal_connect(G_OBJECT(home) , "clicked" , G_CALLBACK(search_phone_home_cb) , NULL);
    gtk_table_attach(GTK_TABLE(table), home, 0, 1, 1, 2, GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
    gtk_widget_set_sensitive(home, FALSE);

    mobile = gtk_check_button_new_with_mnemonic(_("_Mobile"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mobile), addressbook_config->search_phone_mobile);
    g_signal_connect(G_OBJECT(mobile) , "clicked" , G_CALLBACK(search_phone_mobile_cb) , NULL);
    gtk_table_attach(GTK_TABLE(table), mobile, 0, 1, 2, 3, GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

    // Address Book
    gnome_main_section_new_with_table(_("Address Books"), &result_frame, &table, 2, 3);
    gtk_box_pack_start(GTK_BOX(ret), result_frame, TRUE, TRUE, 0);
    gtk_widget_show(result_frame);

    scrolled_label = gtk_label_new(_("Select which Evolution address books to use"));
    gtk_misc_set_alignment(GTK_MISC(scrolled_label), 0.00, 0.2);

    gtk_table_attach(GTK_TABLE(table), scrolled_label, 1, 4, 1, 2, GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
    gtk_widget_set_sensitive(scrolled_label, FALSE);

    scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled_window), GTK_SHADOW_IN);

    gtk_table_attach(GTK_TABLE(table), scrolled_window, 1, 4, 2, 3, GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

    store = gtk_list_store_new(3,
                               G_TYPE_BOOLEAN,             // Active
                               G_TYPE_STRING,             // uid
                               G_TYPE_STRING              // Name
                              );

    // Create tree view with list store
    book_tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));

    // Active column
    renderer = gtk_cell_renderer_toggle_new();
    tree_view_column = gtk_tree_view_column_new_with_attributes("", renderer, "active", COLUMN_BOOK_ACTIVE, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(book_tree_view), tree_view_column);

    // Toggle active property on clicked
    g_signal_connect(G_OBJECT(renderer), "toggled", G_CALLBACK(addressbook_config_book_active_toggled), (gpointer) book_tree_view);

    // Name column
    renderer = gtk_cell_renderer_text_new();
    tree_view_column = gtk_tree_view_column_new_with_attributes(_("Name"), renderer, "markup", COLUMN_BOOK_NAME, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(book_tree_view), tree_view_column);

    g_object_unref(G_OBJECT(store));
    gtk_container_add(GTK_CONTAINER(scrolled_window), book_tree_view);

    addressbook_config_fill_book_list();

    gtk_widget_show_all(ret);

    enable_options();

    return ret;
}

gboolean
addressbook_display(AddressBook_Config *settings, const gchar *field)
{
    gboolean display;

    if (utf8_case_equal(field, ADDRESSBOOK_DISPLAY_CONTACT_PHOTO))
        display = settings->display_contact_photo == 1;
    else if (utf8_case_equal(field, ADDRESSBOOK_DISPLAY_PHONE_BUSINESS))
        display = settings->search_phone_business == 1;
    else if (utf8_case_equal(field, ADDRESSBOOK_DISPLAY_PHONE_HOME))
        display = settings->search_phone_home == 1;
    else if (utf8_case_equal(field, ADDRESSBOOK_DISPLAY_PHONE_MOBILE))
        display = settings->search_phone_mobile == 1;
    else
        display = FALSE;

    return display;
}
