/*
 *  Copyright (C) 2004, 2005, 2006, 2008, 2009, 2010, 2011 Savoir-Faire Linux Inc.
 *  Author: Pierre-Luc Beaudoin <pierre-luc.beaudoin@savoirfairelinux.com>
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

#include <dbusmanager.h>
#include "global.h"
#include "manager.h"
#include "instance.h"

#include "callmanager.h"
#include "configurationmanager.h"
#include "networkmanager.h"

DBusManager::DBusManager()
    : _connected(false)
	, _callManager(NULL)
    , _configurationManager(NULL)
    , _instanceManager(NULL)
#ifdef USE_NETWORKMANAGER
    , _networkManager(NULL)
#endif
{
    try {
        DBus::_init_threading();
        DBus::default_dispatcher = &_dispatcher;

        DBus::Connection sessionConnection = DBus::Connection::SessionBus();
        sessionConnection.request_name ("org.sflphone.SFLphone");

        _callManager = new CallManager (sessionConnection);
        _configurationManager = new ConfigurationManager (sessionConnection);
        _instanceManager = new Instance (sessionConnection);

#ifdef USE_NETWORKMANAGER
        DBus::Connection systemConnection = DBus::Connection::SystemBus();
        _networkManager = new NetworkManager (systemConnection, "/org/freedesktop/NetworkManager", "");
#endif

        _connected = true;
    } catch (const DBus::Error &err) {
        _error("%s: %s\n", err.name(), err.what());
    }
}

DBusManager::~DBusManager()
{
#ifdef USE_NETWORKMANAGER
    delete _networkManager;
#endif
    delete _instanceManager;
    delete _configurationManager;
    delete _callManager;
}

void DBusManager::exec()
{
    _dispatcher.enter();
}

void
DBusManager::exit()
{
    _dispatcher.leave();
}
