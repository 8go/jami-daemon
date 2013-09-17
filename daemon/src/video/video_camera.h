/*
 *  Copyright (C) 2011-2013 Savoir-Faire Linux Inc.
 *
 *  Author: Tristan Matthews <tristan.matthews@savoirfairelinux.com>
 *  Author: Guillaume Roguez <Guillaume.Roguez@savoirfairelinux.com>
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

#ifndef __VIDEO_CAMERA_H__
#define __VIDEO_CAMERA_H__

#include "noncopyable.h"
#include "shm_sink.h"
#include "video_decoder.h"
#include "sflthread.h"

#include <string>
#include <map>


namespace sfl_video {
using std::string;

class VideoCamera :
    public VideoGenerator,
    public SFLThread
{
public:
    VideoCamera(const std::map<string, string> &args);
    ~VideoCamera();

    // as VideoGenerator
    int getWidth() const;
    int getHeight() const;
    int getPixelFormat() const;

private:
    NON_COPYABLE(VideoCamera);

    std::string id_;
    std::map<string, string> args_;
    VideoDecoder *decoder_;
    SHMSink sink_;
    int sinkWidth_;
    int sinkHeight_;

    // as SFLThread
    bool setup();
    void process();
    void cleanup();

    static int interruptCb(void *ctx);
    bool captureFrame();
};

}

#endif // __VIDEO_CAMERA_H__
