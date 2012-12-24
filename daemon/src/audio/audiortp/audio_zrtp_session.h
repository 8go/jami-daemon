/*
 *  Copyright (C) 2004-2012 Savoir-Faire Linux Inc.
 *  Author: Alexandre Savard <alexandre.savard@savoirfairelinux.com>
 *  Author: Pierre-Luc Bacon <pierre-luc.bacon@savoirfairelinux.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
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
#ifndef __AUDIO_ZRTP_SESSION_H__
#define __AUDIO_ZRTP_SESSION_H__

#pragma GCC diagnostic ignored "-Weffc++"
#include <cstddef>
#include <stdexcept>

using std::ptrdiff_t;
#include <ccrtp/rtp.h>
#include <libzrtpcpp/zrtpccrtp.h>
#include <libzrtpcpp/ZrtpQueue.h>
#include <libzrtpcpp/ZrtpUserCallback.h>

#include "global.h"
#include "audio_rtp_session.h"

class SIPCall;
class AudioCodec;

namespace sfl {

class ZrtpZidException : public std::runtime_error {
    public:
        ZrtpZidException(const char *str):
            std::runtime_error(str) {}
};

class AudioZrtpSession :
    public ost::SymmetricZRTPSession,
    public AudioRtpSession {
    public:
        AudioZrtpSession(SIPCall &call, const std::string& zidFilename);

        virtual bool onRTPPacketRecv(ost::IncomingRTPPkt &pkt) {
            return AudioRtpSession::onRTPPacketRecv(pkt);
        }

        virtual std::vector<uint8> getLocalMasterKey() const { std::vector<uint8> vec; return vec; }

        virtual std::vector<uint8> getLocalMasterSalt() const { std::vector<uint8> vec; return vec; }

        virtual void setLocalMasterKey(const std::vector<unsigned char>& vec UNUSED) const {}

        virtual void setLocalMasterSalt(const std::vector<unsigned char>& vec UNUSED) const {}

    private:
        NON_COPYABLE(AudioZrtpSession);

        void sendMicData();
        void initializeZid();
        std::string zidFilename_;
        void startReceiveThread();
        virtual int getIncrementForDTMF() const;
};

}

#endif // __AUDIO_ZRTP_SESSION_H__
