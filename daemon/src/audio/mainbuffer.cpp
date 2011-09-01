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

#include "mainbuffer.h"
#include <utility> // for std::pair
#include "manager.h"

MainBuffer::MainBuffer() : _internalSamplingRate (8000)
{
}


MainBuffer::~MainBuffer()
{
    // delete any ring buffers that didn't get removed
    for (RingBufferMap::iterator iter = _ringBufferMap.begin(); iter != _ringBufferMap.end(); ++iter)
        delete iter->second;
}


void MainBuffer::setInternalSamplingRate (int sr)
{
    if (sr > _internalSamplingRate) {
        flushAllBuffers();
        _internalSamplingRate = sr;
    }
}

CallIDSet* MainBuffer::getCallIDSet (const std::string & call_id)
{
    CallIDMap::iterator iter = _callIDMap.find (call_id);
    return (iter != _callIDMap.end()) ? iter->second : NULL;
}

void MainBuffer::createCallIDSet (const std::string & set_id)
{
    _callIDMap.insert (std::pair<std::string, CallIDSet*> (set_id, new CallIDSet));
}

bool MainBuffer::removeCallIDSet (const std::string & set_id)
{
    CallIDSet* callid_set = getCallIDSet (set_id);

    if (!callid_set) {
        _debug ("removeCallIDSet error callid set %s does not exist!", set_id.c_str());
        return false;
    }

    if (_callIDMap.erase (set_id) == 0) {
		_debug ("removeCallIDSet error while removing callid set %s!", set_id.c_str());
		return false;
    }
	delete callid_set;
	callid_set = NULL;
	return true;
}

void MainBuffer::addCallIDtoSet (const std::string & set_id, const std::string & call_id)
{
    CallIDSet* callid_set = getCallIDSet (set_id);
    callid_set->insert (call_id);
}

void MainBuffer::removeCallIDfromSet (const std::string & set_id, const std::string & call_id)
{
    CallIDSet* callid_set = getCallIDSet (set_id);

    if (callid_set == NULL)
        _error("removeCallIDfromSet error callid set %s does not exist!", set_id.c_str());
    else if (callid_set->erase (call_id) == 0)
		_error("removeCallIDfromSet error while removing callid %s from set %s!", call_id.c_str(), set_id.c_str());
}


RingBuffer* MainBuffer::getRingBuffer (const std::string & call_id)
{
    RingBufferMap::iterator iter = _ringBufferMap.find (call_id);

    return (iter != _ringBufferMap.end()) ? iter->second : NULL;
}


RingBuffer* MainBuffer::createRingBuffer (const std::string & call_id)
{
    RingBuffer* newRingBuffer = new RingBuffer (SIZEBUF, call_id);
    _ringBufferMap.insert (std::pair<std::string, RingBuffer*> (call_id, newRingBuffer));
    return newRingBuffer;
}


bool MainBuffer::removeRingBuffer (const std::string & call_id)
{
    RingBuffer* ring_buffer = getRingBuffer (call_id);

    if (ring_buffer != NULL) {
        if (_ringBufferMap.erase (call_id) != 0) {
            delete ring_buffer;
            return true;
        } else {
            _error ("BufferManager: Error: Fail to delete ringbuffer %s!", call_id.c_str());
            return false;
        }
    } else {
        _debug ("BufferManager: Error: Ringbuffer %s does not exist!", call_id.c_str());
        return true;
    }
}


void MainBuffer::bindCallID (const std::string & call_id1, const std::string & call_id2)
{
    ost::MutexLock guard (_mutex);

    RingBuffer* ring_buffer;
    CallIDSet* callid_set;

    if ( (ring_buffer = getRingBuffer (call_id1)) == NULL)
        createRingBuffer (call_id1);

    if ( (callid_set = getCallIDSet (call_id1)) == NULL)
        createCallIDSet (call_id1);

    if ( (ring_buffer = getRingBuffer (call_id2)) == NULL)
        createRingBuffer (call_id2);

    if ( (callid_set = getCallIDSet (call_id2)) == NULL)
        createCallIDSet (call_id2);

    getRingBuffer (call_id1)->createReadPointer (call_id2);

    getRingBuffer (call_id2)->createReadPointer (call_id1);

    addCallIDtoSet (call_id1, call_id2);

    addCallIDtoSet (call_id2, call_id1);

}

void MainBuffer::bindHalfDuplexOut (const std::string & process_id, const std::string & call_id)
{
    ost::MutexLock guard (_mutex);

    // This method is used only for active calls, if this call does not exist, do nothing
    if (!getRingBuffer (call_id))
        return;

    if (!getCallIDSet (process_id))
        createCallIDSet (process_id);

    getRingBuffer (call_id)->createReadPointer (process_id);

    addCallIDtoSet (process_id, call_id);

}


void MainBuffer::unBindCallID (const std::string & call_id1, const std::string & call_id2)
{
    ost::MutexLock guard (_mutex);

    removeCallIDfromSet (call_id1, call_id2);
    removeCallIDfromSet (call_id2, call_id1);

    RingBuffer* ringbuffer = getRingBuffer (call_id2);

    if (ringbuffer) {

        ringbuffer->removeReadPointer (call_id1);

        if (ringbuffer->getNbReadPointer() == 0) {
            removeCallIDSet (call_id2);
            removeRingBuffer (call_id2);
        }

    }

    ringbuffer = getRingBuffer (call_id1);

    if (ringbuffer) {

        ringbuffer->removeReadPointer (call_id2);

        if (ringbuffer->getNbReadPointer() == 0) {
            removeCallIDSet (call_id1);
            removeRingBuffer (call_id1);
        }
    }
}

void MainBuffer::unBindHalfDuplexOut (const std::string & process_id, const std::string & call_id)
{
    ost::MutexLock guard (_mutex);

    removeCallIDfromSet (process_id, call_id);

    RingBuffer* ringbuffer = getRingBuffer (call_id);

    if (ringbuffer) {
        ringbuffer->removeReadPointer (process_id);

        if (ringbuffer->getNbReadPointer() == 0) {
            removeCallIDSet (call_id);
            removeRingBuffer (call_id);
        }
    } else {
        _debug ("Error: did not found ringbuffer %s", process_id.c_str());
        removeCallIDSet (process_id);
    }


    CallIDSet* callid_set = getCallIDSet (process_id);

    if (callid_set) {
        if (callid_set->empty())
            removeCallIDSet (process_id);
    }

}


void MainBuffer::unBindAll (const std::string & call_id)
{
    CallIDSet* callid_set = getCallIDSet (call_id);

    if (callid_set == NULL)
        return;

    if (callid_set->empty())
        return;

    CallIDSet temp_set = *callid_set;

    CallIDSet::iterator iter_set = temp_set.begin();

    while (iter_set != temp_set.end()) {
        std::string call_id_in_set = *iter_set;
        unBindCallID (call_id, call_id_in_set);

        iter_set++;
    }

}


void MainBuffer::putData (void *buffer, int toCopy, const std::string & call_id)
{
    ost::MutexLock guard (_mutex);

    RingBuffer* ring_buffer = getRingBuffer (call_id);
    if (ring_buffer)
    	ring_buffer->Put (buffer, toCopy);
}

int MainBuffer::getData (void *buffer, int toCopy, const std::string & call_id)
{
    ost::MutexLock guard (_mutex);

    CallIDSet* callid_set = getCallIDSet (call_id);

    if (!callid_set || callid_set->empty()) {
        return 0;
    }

    if (callid_set->size() == 1) {

        CallIDSet::iterator iter_id = callid_set->begin();

        if (iter_id != callid_set->end()) {
            return getDataByID (buffer, toCopy, *iter_id, call_id);
        } else
            return 0;
    } else {
        memset (buffer, 0, toCopy);

        int size = 0;

        CallIDSet::iterator iter_id = callid_set->begin();

        while (iter_id != callid_set->end()) {
            int nbSmplToCopy = toCopy / sizeof (SFLDataFormat);
            SFLDataFormat mixBuffer[nbSmplToCopy];
            memset (mixBuffer, 0, toCopy);
            size = getDataByID (mixBuffer, toCopy, *iter_id, call_id);

            if (size > 0) {
                for (int k = 0; k < nbSmplToCopy; k++) {
                    ( (SFLDataFormat*) (buffer)) [k] += mixBuffer[k];
                }
            }

            iter_id++;
        }

        return size;
    }
}


int MainBuffer::getDataByID (void *buffer, int toCopy, const std::string & call_id, const std::string & reader_id)
{
    RingBuffer* ring_buffer = getRingBuffer (call_id);

    return ring_buffer ? ring_buffer->Get(buffer, toCopy, reader_id) : 0;
}


int MainBuffer::availForGet (const std::string & call_id)
{
    ost::MutexLock guard (_mutex);

    CallIDSet* callid_set = getCallIDSet (call_id);

    if (callid_set == NULL)
        return 0;

    if (callid_set->empty()) {
        return 0;
    }

    if (callid_set->size() == 1) {
        CallIDSet::iterator iter_id = callid_set->begin();

        if ( (call_id != Call::DEFAULT_ID) && (*iter_id == call_id)) {
            _debug ("This problem should not occur since we have %i element", (int) callid_set->size());
        }

        return availForGetByID (*iter_id, call_id);

    } else {

        int avail_bytes = 99999;
        int nb_bytes;
        CallIDSet::iterator iter_id = callid_set->begin();

        syncBuffers (call_id);

        for (iter_id = callid_set->begin(); iter_id != callid_set->end(); iter_id++) {
            nb_bytes = availForGetByID (*iter_id, call_id);

            if ( (nb_bytes != 0) && (nb_bytes < avail_bytes))
                avail_bytes = nb_bytes;
        }

        return avail_bytes != 99999 ? avail_bytes : 0;
    }

}


int MainBuffer::availForGetByID (const std::string & call_id, const std::string & reader_id)
{
    if ( (call_id != Call::DEFAULT_ID) && (reader_id == call_id)) {
        _error ("MainBuffer: Error: RingBuffer has a readpointer on tiself");
    }

    RingBuffer* ringbuffer = getRingBuffer (call_id);

    if (ringbuffer == NULL) {
        _error ("MainBuffer: Error: RingBuffer does not exist");
        return 0;
    } else
        return ringbuffer->AvailForGet (reader_id);

}


int MainBuffer::discard (int toDiscard, const std::string & call_id)
{
    ost::MutexLock guard (_mutex);

    CallIDSet* callid_set = getCallIDSet (call_id);

    if (!callid_set ||  callid_set->empty())
        return 0;

	for (CallIDSet::iterator iter = callid_set->begin(); iter != callid_set->end(); iter++)
		discardByID (toDiscard, *iter, call_id);

	return toDiscard;
}


void MainBuffer::discardByID (int toDiscard, const std::string & call_id, const std::string & reader_id)
{
    RingBuffer* ringbuffer = getRingBuffer (call_id);
    if (ringbuffer)
    	ringbuffer->Discard (toDiscard, reader_id);
}



void MainBuffer::flush (const std::string & call_id)
{
    ost::MutexLock guard (_mutex);

    CallIDSet* callid_set = getCallIDSet (call_id);
    if (callid_set == NULL)
        return;

	for (CallIDSet::iterator iter = callid_set->begin(); iter != callid_set->end(); iter++)
		flushByID (*iter, call_id);

}

void MainBuffer::flushByID (const std::string & call_id, const std::string & reader_id)
{
    RingBuffer* ringbuffer = getRingBuffer (call_id);

    if (ringbuffer != NULL)
        ringbuffer->flush (reader_id);
}


void MainBuffer::flushAllBuffers()
{
    for (RingBufferMap::iterator iter = _ringBufferMap.begin(); iter != _ringBufferMap.end(); ++iter)
        iter->second->flushAll();
}

void MainBuffer::syncBuffers (const std::string & call_id)
{
    CallIDSet* callid_set = getCallIDSet (call_id);

    if (callid_set || callid_set->empty())
        return;

    if (callid_set->size() == 1) // no need to resync, only one session
        return;

    float mean_nbBytes = 0.0;

    CallIDSet::iterator iter;
    // compute mean nb byte in buffers
    for (iter = callid_set->begin(); iter != callid_set->end(); iter++)
        mean_nbBytes += availForGetByID (*iter, call_id);

    mean_nbBytes /= (float) callid_set->size();

    // resync buffers in this conference according to the computed mean
    for (iter = callid_set->begin(); iter != callid_set->end(); iter++)
        if (availForGetByID (*iter, call_id) > (mean_nbBytes + 640))
            discardByID (640, *iter, call_id);
}


void MainBuffer::stateInfo()
{
    // print each call and bound call ids
    for (CallIDMap::iterator iter_call = _callIDMap.begin(); iter_call != _callIDMap.end(); ++iter_call) {

        std::string dbg_str ("    Call: \t");
        dbg_str.append (iter_call->first);
        dbg_str.append ("   is bound to: \t");

        CallIDSet *call_id_set = iter_call->second;
        for (CallIDSet::iterator iter = call_id_set->begin(); iter != call_id_set->end(); ++iter) {
            dbg_str.append (*iter);
            dbg_str.append (", ");
        }
        _debug("%s", dbg_str.c_str());
    }

    // Print ringbuffers ids and readpointers
    for (RingBufferMap::iterator iter_buffer = _ringBufferMap.begin(); iter_buffer != _ringBufferMap.end(); ++iter_buffer) {
        RingBuffer* rbuffer = iter_buffer->second;
        ReadPointer* rpointer = NULL;

        std::string dbg_str ("    Buffer: \t");

        dbg_str.append (iter_buffer->first);
        dbg_str.append ("   as read pointer: \t");

        if (rbuffer)
            rpointer = rbuffer->getReadPointerList();

        if (rpointer) {
            for (ReadPointer::iterator iter = rpointer->begin(); iter != rpointer->end(); ++iter) {
                dbg_str.append (iter->first);
                dbg_str.append (", ");
            }
        }
        _debug("%s", dbg_str.c_str());
    }

}
