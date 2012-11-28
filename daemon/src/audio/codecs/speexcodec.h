/*
 *  Copyright (C) 2004-2012 Savoir-Faire Linux Inc.
 *  Author: Alexandre Savard <alexandre.savard@savoirfairelinux.com>
 *  Author: Emmanuel Milou <emmanuel.milou@savoirfairelinux.com>
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

#include "global.h"
#include "audiocodec.h"
#include "noncopyable.h"
#include "array_size.h"
#include <speex/speex.h>
#include <cassert>

class Speex : public sfl::AudioCodec {
    public:
        Speex(int payload, unsigned clockRate, unsigned frameSize,
              unsigned bitRate, bool dynamicPayload, const SpeexMode *mode) :
            sfl::AudioCodec(payload, "speex", clockRate, frameSize, 1),
            speex_dec_bits_(),
            speex_enc_bits_(),
            speex_dec_state_(0),
            speex_enc_state_(0),
            speex_frame_size_(0) {
                assert(payload >= 110 && payload <= 112);
                assert(110 == PAYLOAD_CODEC_SPEEX_8000 &&
                       111 == PAYLOAD_CODEC_SPEEX_16000 &&
                       112 == PAYLOAD_CODEC_SPEEX_32000);

                bitrate_ = bitRate;
                hasDynamicPayload_ = dynamicPayload;

                // Init the decoder struct
                speex_bits_init(&speex_dec_bits_);
                speex_dec_state_ = speex_decoder_init(mode);

                // Init the encoder struct
                speex_bits_init(&speex_enc_bits_);
                speex_enc_state_ = speex_encoder_init(mode);

                speex_encoder_ctl(speex_enc_state_, SPEEX_SET_SAMPLING_RATE, &clockRate_);
                speex_decoder_ctl(speex_dec_state_, SPEEX_GET_FRAME_SIZE, &speex_frame_size_);
        }

        NON_COPYABLE(Speex);

        ~Speex() {
            // Destroy the decoder struct
            speex_bits_destroy(&speex_dec_bits_);
            speex_decoder_destroy(speex_dec_state_);
            speex_dec_state_ = 0;

            // Destroy the encoder struct
            speex_bits_destroy(&speex_enc_bits_);
            speex_encoder_destroy(speex_enc_state_);
            speex_enc_state_ = 0;
        }

        virtual int decode(short *dst, unsigned char *src, size_t buf_size) {
            speex_bits_read_from(&speex_dec_bits_, (char*) src, buf_size);
            speex_decode_int(speex_dec_state_, &speex_dec_bits_, dst);
            return frameSize_;
        }

        virtual int encode(unsigned char *dst, short *src, size_t buf_size) {
            speex_bits_reset(&speex_enc_bits_);
            speex_encode_int(speex_enc_state_, src, &speex_enc_bits_);
            return speex_bits_write(&speex_enc_bits_, (char*) dst, buf_size);
        }

    private:
        SpeexBits speex_dec_bits_;
        SpeexBits speex_enc_bits_;
        void *speex_dec_state_;
        void *speex_enc_state_;
        int speex_frame_size_;
};
