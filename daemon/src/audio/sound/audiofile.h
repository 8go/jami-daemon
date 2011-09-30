/*
 *  Copyright (C) 2004, 2005, 2006, 2008, 2009, 2010, 2011 Savoir-Faire Linux Inc.
 *  Author: Yan Morin <yan.morin@savoirfairelinux.com>
 *
 *  Inspired by tonegenerator of
 *   Laurielle Lea <laurielle.lea@savoirfairelinux.com> (2004)
 *  Inspired by ringbuffer of Audacity Project
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
#ifndef __AUDIOFILE_H__
#define __AUDIOFILE_H__

#include <stdexcept>
#include <fstream>

#include "audio/audioloop.h"

namespace sfl {
class AudioCodec;
}

class AudioFileException : public std::runtime_error
{
public:
    AudioFileException (const std::string& str="") :
        std::runtime_error("AudioFile: AudioFileException occured: " + str) {}
};

/**
 * @brief Abstract interface for file readers
 */
class AudioFile : public AudioLoop
{
public:
    const std::string &getFilePath(void) const{
    	return filepath;
    }

protected:
    /** The absolute path to the sound file */
    std::string filepath;
};



/**
 * @file audiofile.h
 * @brief A class to manage sound files
 */

class RawFile : public AudioFile
{
    public:
        /**
         * Constructor
         */
        RawFile(const std::string& name, sfl::AudioCodec* codec, unsigned int sampleRate = 8000);

    private:
        // Copy Constructor
        RawFile (const RawFile& rh);

        // Assignment Operator
        RawFile& operator= (const RawFile& rh);

        /** Your preferred codec */
        sfl::AudioCodec* audioCodec;
};

class WaveFile : public AudioFile
{

    public:
		/**
		 * Load a sound file in memory
			 * @param filename  The absolute path to the file
			 * @param sampleRate	The sample rate to read it
			 */
		WaveFile(const std::string&, unsigned int);
};

#endif
