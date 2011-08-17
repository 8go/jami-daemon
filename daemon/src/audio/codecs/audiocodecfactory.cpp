/*
 *  Copyright (C) 2004, 2005, 2006, 2008, 2009, 2010, 2011 Savoir-Faire Linux Inc.
 *  Author: Yan Morin <yan.morin@savoirfairelinux.com>
 *  Author: Laurielle Lea <laurielle.lea@savoirfairelinux.com>
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

#include <iostream>
#include <cstdlib>

#include "audiocodecfactory.h"
#include "fileutils.h"


AudioCodecFactory::AudioCodecFactory() : _CodecsMap(), _defaultCodecOrder(), _Cache(), _nbCodecs(), _CodecInMemory()
{
}

AudioCodecFactory::~AudioCodecFactory()
{

}

//std::vector<std::string> CodecDescriptor::getAllMimeSubtypes()
//{
//    std::vector<std::string> output;
//    IdentifierToCodecInstanceMapIterator it;
//
//    for (it = _codecsMap.begin(); it != _codecsMap.end(); it++) {
//        output.push_back ( ( (*it).second)->getMimeSubtype());
//    }
//
//    return output;
//}


void
AudioCodecFactory::init()
{
    std::vector<sfl::Codec*> CodecDynamicList = scanCodecDirectory();
    _nbCodecs = CodecDynamicList.size();

    if (_nbCodecs <= 0)
        _error ("CodecDescriptor: Error - No codecs available");

    for (int i = 0 ; i < _nbCodecs ; i++) {
        _CodecsMap[ (AudioCodecType) CodecDynamicList[i]->getPayloadType() ] = CodecDynamicList[i];
        _debug ("CodecDescriptor: %s" , CodecDynamicList[i]->getMimeSubtype().c_str());
    }
}

void AudioCodecFactory::setDefaultOrder()
{

    _defaultCodecOrder.clear();
    CodecsMap::iterator iter = _CodecsMap.begin();

    while (iter != _CodecsMap.end()) {
        _defaultCodecOrder.push_back (iter->first);
        iter ++ ;
    }
}

std::string
AudioCodecFactory::getCodecName (AudioCodecType payload)
{
    std::string resNull = "";
    CodecsMap::iterator iter = _CodecsMap.find (payload);

    if (iter!=_CodecsMap.end()) {
        return (iter->second->getMimeSubtype());
    }

    return resNull;
}

sfl::Codec*
AudioCodecFactory::getCodec (AudioCodecType payload)
{
    CodecsMap::iterator iter = _CodecsMap.find (payload);

    if (iter != _CodecsMap.end())
        return iter->second;

    _error ("CodecDescriptor: Error cannont found codec %i in _CodecsMap from codec descriptor", payload);

    return NULL;
}

double AudioCodecFactory::getBitRate (AudioCodecType payload)
{

    CodecsMap::iterator iter = _CodecsMap.find (payload);

    if (iter!=_CodecsMap.end())
        return (iter->second->getBitRate());
    else
        return 0.0;
}


int AudioCodecFactory::getSampleRate (AudioCodecType payload)
{

    CodecsMap::iterator iter = _CodecsMap.find (payload);

    if (iter!=_CodecsMap.end())
        return (iter->second->getClockRate());
    else
        return 0;
}

void AudioCodecFactory::saveActiveCodecs (const std::vector<std::string>& list)
{

    _defaultCodecOrder.clear();
    // list contains the ordered payload of active codecs picked by the user
    // we used the CodecOrder vector to save the order.
    int i=0;
    int payload;
    size_t size = list.size();

    while ( (unsigned int) i < size) {
        payload = std::atoi (list[i].data());

        if (isCodecLoaded (payload)) {
            _defaultCodecOrder.push_back ( (AudioCodecType) payload);
        }

        i++;
    }
}

void
AudioCodecFactory::deleteHandlePointer (void)
{
    _debug ("CodecDesccriptor: Delete codec handle pointers");

    for (int i = 0 ; (unsigned int) i < _CodecInMemory.size() ; i++) {
        unloadCodec (_CodecInMemory[i]);
    }

    _CodecInMemory.clear();
}

std::vector<sfl::Codec*> AudioCodecFactory::scanCodecDirectory (void)
{
    std::vector<sfl::Codec*> codecs;

    std::string libDir = std::string (CODECS_DIR).append ("/");
    std::string homeDir = std::string (HOMEDIR)  + DIR_SEPARATOR_STR + "." + PROGDIR + "/";
    const char *progDir = get_program_dir();
    // look for a CODECS_PATH environment variable...used in tests
    const char *envDir = getenv("CODECS_PATH");
    std::vector<std::string> dirToScan;
    dirToScan.push_back (homeDir);
    dirToScan.push_back (libDir);
    if (envDir)
        dirToScan.push_back(std::string(envDir) + DIR_SEPARATOR_STR);
    if (progDir)
        dirToScan.push_back(std::string(progDir) + DIR_SEPARATOR_STR + "audio/codecs/");

    for (int i = 0 ; (unsigned int) i < dirToScan.size() ; i++) {
        std::string dirStr = dirToScan[i];
        _debug ("CodecDescriptor: Scanning %s to find audio codecs....",  dirStr.c_str());
        DIR *dir = opendir (dirStr.c_str());
        sfl::Codec* audioCodec;

        if (!dir)
            continue;

        dirent *dirStruct;
        while ( (dirStruct = readdir (dir))) {
            std::string file = dirStruct -> d_name ;
            if (file == CURRENT_DIR or file == PARENT_DIR)
                continue;

            if (seemsValid (file) && !alreadyInCache (file)) {
                audioCodec = loadCodec (dirStr+file);
                if (audioCodec) {
                    codecs.push_back (audioCodec);
                    _Cache.push_back (file);
                }
            }
        }

        closedir (dir);
    }

    return codecs;
}

sfl::Codec* AudioCodecFactory::loadCodec (std::string path)
{

    CodecHandlePointer p;
    void * codecHandle = dlopen (path.c_str() , RTLD_LAZY);

    if (!codecHandle) {
    	_error("%s\n", dlerror());
    	return NULL;
    }

    dlerror();

    create_t* createCodec = (create_t*) dlsym (codecHandle , "create");
    char *error = dlerror();
    if (error)
    	_error("%s\n", error);

    sfl::Codec* a = createCodec();

    p = CodecHandlePointer (a, codecHandle);

    _CodecInMemory.push_back (p);

    return a;
}


void AudioCodecFactory::unloadCodec (CodecHandlePointer p)
{

    destroy_t* destroyCodec = (destroy_t*) dlsym (p.second , "destroy");

    char *error = dlerror();
    if (error)
    	_error("%s\n", error);

    destroyCodec (p.first);

    dlclose (p.second);
}

sfl::Codec* AudioCodecFactory::instantiateCodec (AudioCodecType payload)
{
    std::vector< CodecHandlePointer >::iterator iter;

    for (iter = _CodecInMemory.begin(); iter != _CodecInMemory.end(); ++iter) {
        if (iter->first->getPayloadType() == payload) {
            create_t* createCodec = (create_t*) dlsym (iter->second , "create");

            char *error = dlerror();
            if (error)
            	_error("%s\n", error);
            else
				return createCodec();
        }
    }

    return NULL;
}



sfl::Codec* AudioCodecFactory::getFirstCodecAvailable (void)
{

    CodecsMap::iterator iter = _CodecsMap.begin();

    if (iter != _CodecsMap.end())
        return iter->second;
    else
        return NULL;
}

bool AudioCodecFactory::seemsValid (std::string lib)
{

    // The name of the shared library seems valid  <==> it looks like libcodec_xxx.so
    // We check this
    std::string begin = SFL_CODEC_VALID_PREFIX;
    std::string end = SFL_CODEC_VALID_EXTEN;

    // First : check the length of the file name.
    // If it is shorter than begin.length() + end.length() , not a SFL shared library

    if (lib.length() <= begin.length() + end.length())
        return false;

    // Second: check the extension of the file name.
    // If it is different than SFL_CODEC_VALID_EXTEN , not a SFL shared library
    if (lib.substr (lib.length() - end.length() , lib.length()) != end)
        return false;


#ifdef HAVE_SPEEX_CODEC
    // Nothing special
#else

    if (lib.substr (begin.length() , lib.length() - begin.length() - end.length()) == SPEEX_STRING_DESCRIPTION)
        return false;

#endif

#ifdef HAVE_GSM_CODEC
    // Nothing special
#else

    if (lib.substr (begin.length() , lib.length() - begin.length() - end.length()) == GSM_STRING_DESCRIPTION)
        return false;

#endif

#ifdef BUILD_ILBC
    // Nothing special
#else

    if (lib.substr (begin.length() , lib.length() - begin.length() - end.length()) == ILBC_STRING_DESCRIPTION)
        return false;

#endif

    if (lib.substr (0, begin.length()) == begin)
        if (lib.substr (lib.length() - end.length() , end.length()) == end)
            return true;
        else
            return false;
    else
        return false;
}

bool
AudioCodecFactory::alreadyInCache (std::string lib)
{
    int i;

    for (i = 0 ; (unsigned int) i < _Cache.size() ; i++) {
        if (_Cache[i] == lib) {
            return true;
        }
    }

    return false;
}

bool AudioCodecFactory::isCodecLoaded (int payload)
{

    CodecsMap::iterator iter = _CodecsMap.begin();

    while (iter != _CodecsMap.end()) {
        if (iter -> first == payload)
            return true;

        iter++;
    }

    return false;
}

std::vector <std::string> AudioCodecFactory::getCodecSpecifications (const int32_t& payload)
{
    _debug ("CodecDescriptor: Gathering codec specifications for payload %i", payload);

    std::vector<std::string> v;
    std::stringstream ss;

    // Add the name of the codec
    v.push_back (getCodecName ( (AudioCodecType) payload));

    // Add the sample rate
    ss << getSampleRate ( (AudioCodecType) payload);
    v.push_back ( (ss.str()).data());
    ss.str ("");

    // Add the bit rate
    ss << getBitRate ( (AudioCodecType) payload);
    v.push_back ( (ss.str()).data());
    ss.str ("");

    return v;
}
