/*
 *  Copyright (C) 2007-2012 Savoir-Faire Linux inc.
 *  Author: Yan Morin <yan.morin@savoirfairelinux.com>
 *  Author: Laurielle Lea <laurielle.lea@savoirfairelinux.com>
 *  Author: Alexandre Savard <alexandre.savard@savoirfairelinux.com>
 *  Author: Adrien Beraud <adrien.beraud@gmail.com>
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
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA.
 */

#ifndef __RING_BUFFER__
#define __RING_BUFFER__

#include <fstream>
#include <vector>
#include <map>

#include "noncopyable.h"
#include "audiobuffer.h"

typedef std::map<std::string, size_t> ReadPointer;

/**
 * A ring buffer for mutichannel audio samples
 */
class RingBuffer {
    public:
        /**
         * Constructor
         * @param size  Size of the buffer to create
         */
        RingBuffer(size_t size, const std::string &call_id);

        std::string getBufferId() const {
            return buffer_id_;
        }

        /**
         * Reset the counters to 0 for this read pointer
         */
        void flush(const std::string &call_id);

        void flushAll();

        /**
         * Get read pointer coresponding to this call
         */
        size_t getReadPointer(const std::string &call_id) const;

        /**
         * Get the whole readpointer list for this ringbuffer
         */
        ReadPointer* getReadPointerList() {
            return &readpointers_;
        }

        /**
         * Return the smalest readpointer. Usefull to evaluate if ringbuffer is full
         */
        size_t getSmallestReadPointer() const;

        /**
         * Move readpointer forward by pointer_value
         */
        void storeReadPointer(size_t pointer_value, const std::string &call_id);

        /**
         * Add a new readpointer for this ringbuffer
         */
        void createReadPointer(const std::string &call_id);

        /**
         * Remove a readpointer for this ringbuffer
         */
        void removeReadPointer(const std::string &call_id);

        /**
         * Test if readpointer coresponding to this call is still active
         */
        bool hasThisReadPointer(const std::string &call_id) const;

        bool hasNoReadPointers() const;

        /**
         * Write data in the ring buffer
         * @param buffer Data to copied
         * @param toCopy Number of bytes to copy
         */
         void put(AudioBuffer& buf);

        /**
         * To get how much samples are available in the buffer to read in
         * @return int The available (multichannel) samples number
         */
        size_t availableForGet(const std::string &call_id) const;

        /**
         * Get data in the ring buffer
         * @param buffer Data to copied
         * @param toCopy Number of bytes to copy
         * @return size_t Number of bytes copied
         */
         size_t get(AudioBuffer& buf, const std::string &call_id);

        /**
         * Discard data from the buffer
         * @param toDiscard Number of samples to discard
         * @return size_t Number of samples discarded
         */
        size_t discard(size_t toDiscard, const std::string &call_id);

        /**
         * Total length of the ring buffer which is available for "putting"
         * @return int
         */
        size_t putLength() const;

        size_t getLength(const std::string &call_id) const;

        /**
         * Debug function print mEnd, mStart, mBufferSize
         */
        void debug();

    private:
        /* POULET! */
        NON_COPYABLE(RingBuffer);

        /** Pointer on the last data */
        size_t endPos_;
        /** Data */
        AudioBuffer buffer_;

        ReadPointer readpointers_;
        std::string buffer_id_;

        friend class MainBufferTest;
};


#endif /*  __RING_BUFFER__ */
