/*
 *  Copyright (C) 2004, 2005, 2006, 2009, 2008, 2009, 2010, 2011 Savoir-Faire Linux Inc.
 *  Author: Tristan Matthews <tristan.matthews@savoirfairelinux.com>
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

#ifndef __VIDEO_RTP_SESSION_H__
#define __VIDEO_RTP_SESSION_H__

#include <string>
#include <map>
#include <tr1/memory>

class Sdp;

namespace sfl_video {

class VideoSendThread;
class VideoReceiveThread;

class VideoRtpSession {
    public:
        VideoRtpSession();
        VideoRtpSession(const std::map<std::string, std::string> &txArgs,
                        const std::map<std::string, std::string> &rxArgs);

        void start();
        void stop();
        void test();
        void test_loopback();
        void updateDestination(const std::string &destination,
                               unsigned int port);
        void updateSDP(const Sdp &sdp);

    private:
        std::tr1::shared_ptr<VideoSendThread> sendThread_;
        std::tr1::shared_ptr<VideoReceiveThread> receiveThread_;
        std::map<std::string, std::string> txArgs_;
        std::map<std::string, std::string> rxArgs_;
        bool sending_;
        bool receiving_;
};
}

#endif // __VIDEO_RTP_SESSION_H__
