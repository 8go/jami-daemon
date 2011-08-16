/*
 *  Copyright (C) 2004, 2005, 2006, 2008, 2009, 2010, 2011 Savoir-Faire Linux Inc.
 *  Author: Julien Bonjean <julien.bonjean@savoirfairelinux.com>
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

#include "logger.h"
#include <cstdarg>
#include <string>
#include <cstdio>

namespace Logger
{

bool consoleLog = false;
bool debugMode = false;

void log (const int level, const char* format, ...)
{
    if (!debugMode && level == LOG_DEBUG)
        return;

    va_list ap;
    const char *prefix = "<> ";
    const char *color_prefix = "";

    switch (level) {
        case LOG_ERR: {
            prefix = "<error> ";
            color_prefix = RED;
            break;
        }
        case LOG_WARNING: {
            prefix = "<warning> ";
            color_prefix = LIGHT_RED;
            break;
        }
        case LOG_INFO: {
            prefix = "<info> ";
            color_prefix = "";
            break;
        }
        case LOG_DEBUG: {
            prefix = "<debug> ";
            color_prefix = "";
            break;
        }
    }

    char buffer[8192];
    va_start (ap, format);
    vsnprintf (buffer, sizeof buffer, format, ap);
    va_end (ap);

    if (consoleLog)
        fprintf(stderr, "%s%s"END_COLOR"\n", color_prefix, buffer);

    syslog (level, "%s%s", prefix, buffer);
}

void setConsoleLog (bool c)
{
    Logger::consoleLog = c;
}

void setDebugMode (bool d)
{
    Logger::debugMode = d;
}

}

