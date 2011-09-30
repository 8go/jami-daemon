/*
 *  Copyright (C) 2004, 2005, 2006, 2008, 2009, 2010, 2011 Savoir-Faire Linux Inc.
 *
 *  Author: Emmanuel Milou <emmanuel.milou@savoirfairelinux.com>
 *  Author: Alexandre Savard <alexandre.savard@savoirfairelinux.com> 
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

#include <historymanager.h>
#include <errno.h>
#include <cc++/file.h>
#include <time.h>

namespace {
static void free_history(HistoryItemMap &history_items)
{
    HistoryItemMap::iterator iter;
    for (iter = history_items.begin(); iter != history_items.end(); ++iter)
        delete *iter;

    history_items.clear ();
}
} // end anonymous namespace

HistoryManager::HistoryManager ()
    :	_history_loaded (false),
        _history_path ("")
{
}

HistoryManager::~HistoryManager ()
{
    free_history(_history_items);
}

int HistoryManager::load_history (int limit, std::string path)
{
    Conf::ConfigTree history_list;

    _debug("HistoryManager: Load history");

    create_history_path (path);
    load_history_from_file (&history_list);
    return load_history_items_map (&history_list, limit);
}

bool HistoryManager::save_history (void)
{
    Conf::ConfigTree history_list;

    _debug("HistoryManager: Save history");

    save_history_items_map (&history_list);
    return save_history_to_file (&history_list);
}

bool HistoryManager::load_history_from_file (Conf::ConfigTree *history_list)
{
    int exist;

    _debug ("HistoryManager: Load history from file %s", _history_path.c_str());

    exist = history_list->populateFromFile (_history_path.c_str());
    _history_loaded = (exist == 2) ? false : true;

    return _history_loaded;
}

int HistoryManager::load_history_items_map (Conf::ConfigTree *history_list, int limit)
{

    short nb_items = 0;
    Conf::TokenList sections;
    HistoryItem *item;
    Conf::TokenList::iterator iter;
    std::string number, name, callID, accountID, timestamp_start, timestamp_stop, recording_file, confID, time_added;
    CallType type;
    int history_limit;
    time_t current_timestamp;

    _debug ("HistoryManager: Load history items");

    // We want to save only the items recent enough (ie compared to CONFIG_HISTORY_LIMIT)
    // Get the current timestamp
    (void) time (&current_timestamp);
    history_limit = get_unix_timestamp_equivalent (limit);

    sections = history_list->getSections();
    iter = sections.begin();

    while (iter != sections.end()) {

        type = (CallType) getConfigInt (*iter, "type", history_list);
	timestamp_start = getConfigString (*iter, "timestamp_start", history_list); 
        timestamp_stop = getConfigString (*iter, "timestamp_stop", history_list);
        name = getConfigString (*iter, "name", history_list);
        number = getConfigString (*iter, "number", history_list);
	callID = getConfigString(*iter, "id", history_list);
        accountID = getConfigString (*iter, "accountid", history_list);
        recording_file = getConfigString(*iter, "recordfile", history_list);
	confID = getConfigString(*iter, "confid", history_list);
        time_added = getConfigString(*iter, "timeadded", history_list);

        // Make a check on the start timestamp to know it is loadable according to CONFIG_HISTORY_LIMIT

        if (atoi (timestamp_start.c_str ()) >= ( (int) current_timestamp - history_limit)) {
            item = new HistoryItem (timestamp_start, type, timestamp_stop, name, number, callID, accountID, recording_file, confID, time_added);
            add_new_history_entry (item);
            nb_items ++;
        }

        iter ++;
    }

    return nb_items;
}


bool HistoryManager::save_history_to_file (Conf::ConfigTree *history_list)
{
    _debug ("HistoryManager: Saving history in XDG directory: %s", _history_path.data());
    return  history_list->saveConfigTree (_history_path.data());
}


int HistoryManager::save_history_items_map (Conf::ConfigTree *history_list)
{
    HistoryItemMap::iterator iter;
    HistoryItem *item;
    int items_saved = 0;

    _debug("HistoryManager: Save history items map");

    iter = _history_items.begin ();

    while (iter != _history_items.end ()) {
        item = *iter;

        if (item) {
            if (item->save (&history_list))
                items_saved ++;
        } else {
            _debug("can't save NULL history item\n");
        }

        iter ++;
    }

    return items_saved;
}

void HistoryManager::add_new_history_entry (HistoryItem *new_item)
{
    // Add it in the map
    _history_items.push_back(new_item);
}

int HistoryManager::create_history_path (std::string path)
{

    std::string userdata, xdg_env, xdg_data;

    xdg_data = std::string (HOMEDIR) + DIR_SEPARATOR_STR + ".local/share/sflphone";

    if (path == "") {

        // If the environment variable is set (not null and not empty), we'll use it to save the history
        // Else we 'll the standard one, ie: XDG_DATA_HOME = $HOMEDIR/.local/share/sflphone
        if (XDG_DATA_HOME != NULL) {
            xdg_env = std::string (XDG_DATA_HOME);
            (xdg_env.length() > 0) ?	userdata = xdg_env
                                                   : userdata = xdg_data;
        } else
            userdata = xdg_data;

        if (mkdir (userdata.data(), 0755) != 0) {
            // If directory	creation failed
            if (errno != EEXIST) {
                _debug ("HistoryManager: Cannot create directory: %m");
                return -1;
            }
        }

        // Load user's history
        _history_path = userdata + DIR_SEPARATOR_STR + "history";
    } else
        set_history_path (path);


    return 0;
}

// throw an Conf::ConfigTreeItemException if not found
int
HistoryManager::getConfigInt (const std::string& section, const std::string& name, Conf::ConfigTree *history_list)
{
    try {
        return history_list->getConfigTreeItemIntValue (section, name);
    } catch (Conf::ConfigTreeItemException& e) {
        throw e;
    }

    return 0;
}

std::string
HistoryManager::getConfigString (const std::string& section, const std::string& name, Conf::ConfigTree *history_list)
{
    try {
        return history_list->getConfigTreeItemValue (section, name);
    } catch (Conf::ConfigTreeItemException& e) {
        throw e;
    }

    return "";
}

std::vector<std::string> HistoryManager::get_history_serialized (void)
{
    std::vector<std::string> serialized;
    HistoryItemMap::iterator iter;

    _debug("HistoryManager: Get history serialized");

    for (iter = _history_items.begin (); iter != _history_items.end(); ++iter) {
        HistoryItem *current = *iter;
        if (current)
            serialized.push_back(current->serialize ());
    }

    return serialized;
}


int HistoryManager::set_serialized_history (std::vector<std::string> history, int limit)
{
    std::vector<std::string>::iterator iter;
    HistoryItem *new_item;
    int items_added = 0;
    int history_limit;
    time_t current_timestamp;

    _debug("HistoryManager: Set serialized history");

    // Clear the existing history
    free_history(_history_items);

    // We want to save only the items recent enough (ie compared to CONFIG_HISTORY_LIMIT)
    // Get the current timestamp
    (void) time (&current_timestamp);
    history_limit = get_unix_timestamp_equivalent (limit);

    for (iter = history.begin () ; iter != history.end () ; iter ++) {
        new_item = new HistoryItem(*iter);
        if(new_item == NULL) {
            _error("HistoryManager: Error: Could not create history item");
        }
        int item_timestamp = atoi(new_item->get_timestamp().c_str());
        if (item_timestamp >= ( (int) current_timestamp - history_limit)) {
            add_new_history_entry (new_item);
            items_added ++;
        } else {
            delete new_item;
        }
    }

    return items_added;
}


