/*
 *  Copyright (C) 2004, 2005, 2006, 2008, 2009, 2010, 2011 Savoir-Faire Linux Inc.
 *  Author : Alexandre Savard <alexandre.savard@savoirfairelinux.com>
 *
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
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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

#include <sstream>

#include "conference.h"
#include "manager.h"
#include "audio/audiolayer.h"
#include "audio/mainbuffer.h"

Conference::Conference() : _id (""), _confState (ACTIVE_ATTACHED), _nbParticipant (0)
{
    _id = Manager::instance().getNewCallID();

    Recordable::initRecFileName (_id);
}


Conference::~Conference()
{



}


int Conference::getState()
{
    return _confState;
}


void Conference::setState (ConferenceState state)
{
    _confState = state;
}


void Conference::add (std::string participant_id)
{

    _debug ("Conference:: add participant %s", participant_id.c_str());

    _participants.insert (participant_id);

    _nbParticipant++;
}


void Conference::remove (std::string participant_id)
{


    _debug ("Conference::remove participant %s", participant_id.c_str());

    _participants.erase (participant_id);

    _nbParticipant--;

}

void Conference::bindParticipant (std::string participant_id)
{

    if (_nbParticipant >= 1) {

    	_debug("************************************************ Conference: bindParticipant");

    	ParticipantSet::iterator iter = _participants.begin();

        while (iter != _participants.end()) {

            if (participant_id != (*iter)) {
                Manager::instance().getMainBuffer()->bindCallID (participant_id, *iter);
            }

            iter++;
        }

    }


    Manager::instance().getMainBuffer()->bindCallID (participant_id);

}


std::string Conference::getStateStr()
{

    std::string state_str;

    switch (_confState) {

        case ACTIVE_ATTACHED:
            state_str = "ACTIVE_ATACHED";
            break;
        case ACTIVE_DETACHED:
            state_str = "ACTIVE_DETACHED";
            break;
        case ACTIVE_ATTACHED_REC:
        	state_str = "ACTIVE_ATTACHED_REC";
        	break;
        case ACTIVE_DETACHED_REC:
        	state_str = "ACTIVE_DETACHED_REC";
        	break;
        case HOLD:
            state_str = "HOLD";
            break;
        case HOLD_REC:
        	state_str = "HOLD_REC";
        	break;
        default:
            break;
    }

    return state_str;
}


ParticipantSet Conference::getParticipantList()
{
    return _participants;
}



bool Conference::setRecording()
{

    bool recordStatus = Recordable::recAudio.isRecording();

    Recordable::recAudio.setRecording();

    // start recording
    if (!recordStatus) {

        MainBuffer *mbuffer = Manager::instance().getMainBuffer();

        ParticipantSet::iterator iter = _participants.begin();

        std::string process_id = Recordable::recorder.getRecorderID();

        while (iter != _participants.end()) {
            mbuffer->bindHalfDuplexOut (process_id, *iter);
            iter++;
        }

        mbuffer->bindHalfDuplexOut (process_id);

        Recordable::recorder.start();

    }
    // stop recording
    else {

        MainBuffer *mbuffer = Manager::instance().getMainBuffer();

        ParticipantSet::iterator iter = _participants.begin();

        std::string process_id = Recordable::recorder.getRecorderID();

        while (iter != _participants.end()) {
            mbuffer->unBindHalfDuplexOut (process_id, *iter);
            iter++;
        }

        mbuffer->unBindHalfDuplexOut (process_id);

        // Recordable::recorder.start();

    }

    return recordStatus;

}
