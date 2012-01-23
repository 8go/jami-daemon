/*
 *  Copyright (C) 2011 Savoir-Faire Linux Inc.
 *  Copyright © 2009 Rémi Denis-Courmont
 *
 *  Author: Rafaël Carré <rafael.carre@savoirfairelinux.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <cstdio>
#include <stdexcept> // for std::runtime_error
#include <sstream>

#include <cc++/thread.h>

#include "logger.h"

#include <libudev.h>
#include <cstring>

extern "C" {
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
}

#include <cerrno>

#include "video_v4l2_list.h"

#include "manager.h"

namespace sfl_video {

static int is_v4l2(struct udev_device *dev)
{
	const char *version = udev_device_get_property_value(dev, "ID_V4L_VERSION");
	/* we do not support video4linux 1 */
	return version and strcmp(version, "1");
}

VideoV4l2ListThread::VideoV4l2ListThread() : devices_(), mutex_(), udev_(0), udev_mon_(0)
{
    udev_list_entry *devlist;
    udev_enumerate *devenum;

    addDevice("SFLTEST");

    udev_ = udev_new();
    if (!udev_)
        goto udev_failed;

    udev_mon_ = udev_monitor_new_from_netlink(udev_, "udev");
    if (!udev_mon_)
        goto udev_failed;
    if (udev_monitor_filter_add_match_subsystem_devtype(udev_mon_, "video4linux", NULL))
        goto udev_failed;

    /* Enumerate existing devices */
    devenum = udev_enumerate_new(udev_);
    if (devenum == NULL)
        goto udev_failed;
    if (udev_enumerate_add_match_subsystem(devenum, "video4linux")) {
        udev_enumerate_unref(devenum);
        goto udev_failed;
    }

    udev_monitor_enable_receiving(udev_mon_);
    /* Note that we enumerate _after_ monitoring is enabled so that we do not
     * loose device events occuring while we are enumerating. We could still
     * loose events if the Netlink socket receive buffer overflows. */
    udev_enumerate_scan_devices(devenum);
    devlist = udev_enumerate_get_list_entry(devenum);
    struct udev_list_entry *deventry;
    udev_list_entry_foreach(deventry, devlist) {
        const char *path = udev_list_entry_get_name(deventry);
        struct udev_device *dev = udev_device_new_from_syspath(udev_, path);

        if (is_v4l2(dev)) {
            const char *devpath = udev_device_get_devnode(dev);
            if (devpath) {
                try {
                    addDevice(devpath);
                } catch (const std::runtime_error &e) {
                    ERROR(e.what());
                }
            }
        }
        udev_device_unref(dev);
    }
    udev_enumerate_unref(devenum);

    return;

udev_failed:

	ERROR("udev enumeration failed");

	if (udev_mon_)
        udev_monitor_unref(udev_mon_);
    if (udev_)
        udev_unref(udev_);
    udev_mon_ = NULL;
    udev_ = NULL;

    /* fallback : go through /dev/video* */
    int idx;
    for(idx = 0;; ++idx) {
        std::stringstream ss;
        ss << "/dev/video" << idx;
        try {
            if (!addDevice(ss.str()))
                return;
        } catch (const std::runtime_error &e) {
            ERROR(e.what());
            return;
        }
    }
}

namespace {

int getNumber(const std::string &name, size_t *sharp)
{
	size_t len = name.length();
    // name is too short to be numbered
	if (len < 3)
		return -1;

	for (size_t c = len; c; --c) {
		if (name[c] == '#') {
			unsigned i;
			if (sscanf(name.substr(c).c_str(), "#%u", &i) != 1)
				return -1;
			*sharp = c;
			return i;
		}
	}

	return -1;
}

void giveUniqueName(VideoV4l2Device &dev, const std::vector<VideoV4l2Device> &devices)
{
    start:
    for (size_t i = 0; i < devices.size(); ++i) {
		if (dev.name == devices[i].name) {
            size_t sharp;
			int num = getNumber(dev.name, &sharp);
			if (num < 0) // not numbered
				dev.name += " #0";
			else {
				std::stringstream ss;
				ss  << num+1;
				dev.name.replace(sharp + 1, ss.str().length(), ss.str());
			}
			goto start; // we changed the name, let's look again if it is unique
		}
	}
}
} // end anonymous namespace

VideoV4l2ListThread::~VideoV4l2ListThread()
{
	terminate();
    if (udev_mon_)
        udev_monitor_unref(udev_mon_);
    if (udev_)
        udev_unref(udev_);
}

void VideoV4l2ListThread::run()
{
	if (!udev_mon_)
		return;

	int fd = udev_monitor_get_fd(udev_mon_);
    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd, &set);
	while (not testCancel()) {
		struct udev_device *dev;
		const char *node, *action;
		int ret = select(fd + 1, &set, NULL, NULL, NULL);
		switch(ret) {
            case 1:
                dev = udev_monitor_receive_device(udev_mon_);
                if (!is_v4l2(dev)) {
                    udev_device_unref(dev);
                    continue;
                }

                node = udev_device_get_devnode(dev);
                action = udev_device_get_action(dev);
                if (!strcmp(action, "add")) {
                    DEBUG("udev: adding %s", node);
                    try {
                        addDevice(node);
                        Manager::instance().notifyVideoDeviceEvent();
                    } catch (const std::runtime_error &e) {
                        ERROR(e.what());
                    }
                } else if (!strcmp(action, "remove")) {
                    DEBUG("udev: removing %s", node);
                    delDevice(std::string(node));
                }
                udev_device_unref(dev);
                continue;

            default:
                ERROR("select() returned %d (%m)", ret);
                return;

            case -1:
                if (errno == EAGAIN)
                    continue;
                ERROR("udev monitoring thread: select failed (%m)");
                return;
        }
    }
}

void VideoV4l2ListThread::delDevice(const std::string &node)
{
	ost::MutexLock lock(mutex_);
    std::vector<std::string> v;

    size_t n = devices_.size();
    for (size_t i = 0 ; i < n ; i++) {
        if (devices_[i].device == node) {
        	devices_.erase(devices_.begin() + i);
			Manager::instance().notifyVideoDeviceEvent();
        	return;
        }
    }
}

bool VideoV4l2ListThread::addDevice(const std::string &dev)
{
	ost::MutexLock lock(mutex_);

    if (dev == "SFLTEST") {
        devices_.push_back(VideoV4l2Device(-1, dev));
        return true;
    }

    int fd = open(dev.c_str(), O_RDWR);
    if (fd == -1)
        return false;

    std::string s(dev);
    VideoV4l2Device v(fd, s);
    giveUniqueName(v, devices_);
    devices_.push_back(v);

    ::close(fd);
    return true;
}

std::vector<std::string> VideoV4l2ListThread::getChannelList(const std::string &dev)
{
	ost::MutexLock lock(mutex_);
	return getDevice(dev).getChannelList();
}

std::vector<std::string> VideoV4l2ListThread::getSizeList(const std::string &dev, const std::string &channel)
{
	ost::MutexLock lock(mutex_);
	return getDevice(dev).getChannel(channel).getSizeList();
}

std::vector<std::string> VideoV4l2ListThread::getRateList(const std::string &dev, const std::string &channel, const std::string &size)
{
	ost::MutexLock lock(mutex_);
	return getDevice(dev).getChannel(channel).getSize(size).getRateList();
}

std::vector<std::string> VideoV4l2ListThread::getDeviceList()
{
	ost::MutexLock lock(mutex_);
    std::vector<std::string> v;

    size_t n = devices_.size();
    for (size_t i = 0 ; i < n ; i++) {
        std::stringstream ss;
        VideoV4l2Device &dev = devices_[i];
        std::string &name = dev.name;
        if (name.length())
            ss << name;
        else
            ss << dev.device;

        v.push_back(ss.str());
    }

    return v;
}

VideoV4l2Device &VideoV4l2ListThread::getDevice(const std::string &name)
{
	ost::MutexLock lock(mutex_);
	for (size_t i = 0; i < devices_.size(); i++)
		if (devices_[i].name == name)
			return devices_[i];

	return devices_.back();
}

unsigned VideoV4l2ListThread::getChannelNum(const std::string &dev, const std::string &name)
{
	ost::MutexLock lock(mutex_);
	return getDevice(dev).getChannel(name).idx;
}

const std::string &VideoV4l2ListThread::getDeviceNode(const std::string &name)
{
	ost::MutexLock lock(mutex_);
	return getDevice(name).device;
}
} // namespace sfl_video
