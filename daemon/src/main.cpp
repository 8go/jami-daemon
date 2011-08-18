/*
 *  Copyright (C) 2004, 2005, 2006, 2008, 2009, 2010, 2011 Savoir-Faire Linux Inc.
 *  Author: Alexandre Bourget <alexandre.bourget@savoirfairelinux.com>
 *  Author: Yan Morin <yan.morin@savoirfairelinux.com>
 *  Author: Laurielle Lea <laurielle.lea@savoirfairelinux.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
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

#include <libintl.h>
#include <cstring>
#include <iostream>
#include <memory> // for auto_ptr
#include <string>
#include <dirent.h>
#include <sys/stat.h>
#include <cc++/common.h>
#include "global.h"
#include "fileutils.h"

#include "dbus/dbusmanager.h"
#include "manager.h"

#include "audio/audiolayer.h"

ost::CommandOptionNoArg	console (
    "console", "c", "Log in console (instead of syslog)"
);

ost::CommandOptionNoArg	debug (
    "debug", "d", "Debug mode (more verbose)"
);

ost::CommandOptionNoArg	help (
    "help", "h", "Print help"
);

int
main (int argc, char **argv)
{
    set_program_dir(argv[0]);

    Logger::setConsoleLog (false);
    Logger::setDebugMode (false);

    // makeCommandOptionParse allocates the object with operator new, so
    // auto_ptr is fine in this context.
    // TODO: This should eventually be replaced with std::unique_ptr for C++0x
    std::auto_ptr<ost::CommandOptionParse> args(ost::makeCommandOptionParse (argc, argv, ""));

    printf ("SFLphone Daemon %s, by Savoir-Faire Linux 2004-2011\n", VERSION);
    printf ("http://www.sflphone.org/\n");

    if (help.numSet) {
        std::cerr << args->printUsage();
        return 0;
    }
    else if (args->argsHaveError()) {
        std::cerr << args->printErrors();
        std::cerr << args->printUsage();
        return 1;
    }

    if (console.numSet)
        Logger::setConsoleLog (true);

    if (debug.numSet)
        Logger::setDebugMode (true);

    FILE *fp;
    char homepid[128];
    char sfldir[128];

    unsigned int iPid = getpid();
    char cPid[64], cOldPid[64];
    snprintf (cPid, sizeof (cPid), "%d", iPid);
    std::string xdg_cache, xdg_env, path;

    xdg_cache = std::string (HOMEDIR) + DIR_SEPARATOR_STR + ".cache/";

    if (XDG_CACHE_HOME != NULL) {
        xdg_env = std::string (XDG_CACHE_HOME);
        (xdg_env.length() > 0) ? path = xdg_env
                                        :		path = xdg_cache;
    } else
        path = xdg_cache;

    // Use safe sprintf (Contribution #4952, Brendan Smith)
    snprintf (sfldir, sizeof (sfldir), "%s", path.c_str ());

    path  = path + "sflphone";

    // Use safe sprintf (Contribution #4952, Brendan Smith)
    snprintf (homepid, sizeof (homepid), "%s/%s", path.c_str (), PIDFILE);

    if ( (fp = fopen (homepid,"r")) == NULL) {
        // Check if $XDG_CACHE_HOME directory exists or not.
        DIR *dir;

        if ( (dir = opendir (sfldir)) == NULL) {
            //Create it
            if (mkdir (sfldir, 0755) != 0) {
                fprintf (stderr, "Creating directory %s failed. Exited.", sfldir);
                return 1;
            }
        }

        // Then create the sflphone directory inside the $XDG_CACHE_HOME dir
        snprintf (sfldir, sizeof (sfldir), "%s", path.c_str ());

        if ( (dir = opendir (sfldir)) == NULL) {
            //Create it
            if (mkdir (sfldir, 0755) != 0) {
                fprintf (stderr, "Creating directory %s failed. Exited.", sfldir);
                return 1;
            }
        }

        // PID file doesn't exists, create and write pid in it
        if ( (fp = fopen (homepid,"w")) == NULL) {
            fprintf (stderr, "Creating PID file %s failed. Exited.", homepid);
            return 1;
        } else {
            fputs (cPid , fp);
            fclose (fp);
        }
    } else {
        // PID file exists. Check the former process still alive or not. If alive, give user a hint.
        char *res;
        res = fgets (cOldPid, 64, fp);

        if (res == NULL) perror ("Error getting string from stream");

        else {
            fclose (fp);

            if (kill (atoi (cOldPid), 0) == 0) {
                std::cerr << "There is already a sflphoned daemon running in the system. Starting Failed." << std::endl;
                return 1;
            } else {
                if ( (fp = fopen (homepid,"w")) == NULL) {
                    fprintf (stderr, "Writing to PID file %s failed. Exited.", homepid);
                    return 1;
                } else {
                    fputs (cPid , fp);
                    fclose (fp);
                }
            }
        }
    }

    try {
        // TODO Use $XDG_CONFIG_HOME to save the config file (which default to $HOME/.config)
    	Manager::instance().initConfigFile();
    	Manager::instance().init();
    } catch (std::exception &e) {
        std::cerr << e.what() << std::endl;
        return 1;
    } catch (...) {
        fprintf (stderr, "An exception occured when initializing the system.");
        return 1;
    }

    _debug ("Starting DBus event loop");
    Manager::instance().getDbusManager()->exec();

    return 0;
}

