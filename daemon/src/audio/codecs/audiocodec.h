/*
 *  Copyright (C) 2004-2012 Savoir-Faire Linux Inc.
 *  Author:  Alexandre Savard <alexandre.savard@savoirfairelinux.com>
 *
 *  Mostly borrowed from asterisk's sources (Steve Underwood <steveu@coppice.org>)
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
#ifndef __AUDIO_CODEC_H__
#define __AUDIO_CODEC_H__

#include <string>
#include <vector>

#include "cc_config.h"
#include "sfl_types.h"
#include <ccrtp/formats.h> // for ost::DynamicPayloadFormat

#define XSTR(s) STR(s)
#define STR(s) #s

/* bump when codec binary interface changes */
#define AUDIO_CODEC_ENTRY create_1_2_3
#define AUDIO_CODEC_ENTRY_SYMBOL XSTR(AUDIO_CODEC_ENTRY)

// We assume all decoders will be fed 20ms of audio or less
// And we'll resample them to 44.1kHz or less
// Also assume mono
#define DEC_BUFFER_SIZE ((44100 * 20) / 1000)

namespace sfl {

class AudioCodec {
    public:
        AudioCodec(uint8 payload, const std::string &codecName, int clockRate, int frameSize, unsigned channels);

        /**
         * Copy constructor.
         */
        AudioCodec(const AudioCodec& codec);

        virtual ~AudioCodec() {};

        std::string getMimeSubtype() const;

        /**
         * Decode an input buffer and fill the output buffer with the decoded data
         * @param buffer_size : the size of the input buffer
         * @return the number of samples decoded
         */
        virtual int decode(SFLAudioSample *dst, unsigned char *buf, size_t buffer_size) = 0;

        /**
         * Encode an input buffer and fill the output buffer with the encoded data
         * @param buffer_size : the maximum size of encoded data buffer (dst)
         * @return the number of bytes encoded
         */
        virtual int encode(unsigned char *dst, SFLAudioSample *src, size_t buffer_size) = 0;

        /**
         * Multichannel version of decode().
         * Default implementation decode(short *, unsigned char *, size_t) to the first channel (assume 1 channel).
         */
        virtual int decode(std::vector<std::vector<short> > *dst, unsigned char *buf, size_t buffer_size, size_t dst_offset=0);

        /**
         * Multichannel version of encode().
         * Default implementation calls encode() on the first channel (assume 1 channel).
         */
        virtual int encode(unsigned char *dst, std::vector<std::vector<short> > *src, size_t buffer_size);

        uint8 getPayloadType() const;

        void setPayloadType(uint8 pt) {
            payload_ = pt;
        }

        /**
         * @return true if this payload is a dynamic one.
         */
        bool hasDynamicPayload() const;

        uint32 getClockRate() const;

        double getBitRate() const;

        unsigned getChannels() const;

        /**
         * @return the framing size for this codec.
         */
        unsigned int getFrameSize() const;

    protected:
        /** Holds SDP-compliant codec name */
        std::string codecName_; // what we put inside sdp

        /** Clock rate or sample rate of the codec, in Hz */
        uint32 clockRate_;

        /** Number of channel 1 = mono, 2 = stereo */
        uint8 channel_;

        /** codec frame size in samples*/
        unsigned frameSize_;

        /** Bitrate */
        double bitrate_;

    private:
        AudioCodec& operator=(const AudioCodec&);
        uint8 payload_;

        ost::DynamicPayloadFormat payloadFormat_;

protected:
        bool hasDynamicPayload_;
};
} // end namespace sfl


typedef sfl::AudioCodec* create_t();
typedef void destroy_t(sfl::AudioCodec* codec);

#endif
