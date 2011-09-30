/*
 *  Copyright (C) 2004, 2005, 2006, 2008, 2009, 2010 Savoir-Faire Linux Inc.
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

#ifndef _RTP_TEST_
#define _RTP_TEST_

// Cppunit import
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/TestCaller.h>
#include <cppunit/TestCase.h>
#include <cppunit/TestSuite.h>

#include <assert.h>

#include <stdio.h>
#include <sstream>
#include <ccrtp/rtp.h>

// pjsip import
#include <pjsip.h>
#include <pjlib.h>
#include <pjsip_ua.h>
#include <pjlib-util.h>
#include <pjnath/stun_config.h>

// Application import
#include "audio/audiortp/AudioRtpFactory.h"
#include "manager.h"
#include "call.h"
#include "sip/sipcall.h"
#include "sip/sipvoiplink.h"

#include "config/config.h"

using namespace sfl;

class AudioSymmetricRtpSession;
//class AudioRtpFactory;
class SIPVoIPLink;

class RtpTest: public CppUnit::TestCase {

	/*
	 * Use cppunit library macros to add unit test the factory
	 */
CPPUNIT_TEST_SUITE( RtpTest );
		CPPUNIT_TEST( testRtpInitClose );
	CPPUNIT_TEST_SUITE_END();

public:

	RtpTest() :
		CppUnit::TestCase("Audio Layer Tests") {
	}

	/*
	 * Code factoring - Common resources can be initialized here.
	 * This method is called by unitcpp before each test
	 */
	void setUp();

	/*
	 * Code factoring - Common resources can be released here.
	 * This method is called by unitcpp after each test
	 */
	void tearDown();

	bool pjsipInit();

	void testRtpInitClose();

private:

	enum CallType {
		Incoming, Outgoing
	};

	ManagerImpl* manager;

	AudioRtpFactory *audiortp;

	SIPCall *sipcall;

	pj_caching_pool _cp;

	pj_pool_t *_pool;
};
/* Register our test module */
CPPUNIT_TEST_SUITE_NAMED_REGISTRATION(RtpTest, "RtpTest");
CPPUNIT_TEST_SUITE_REGISTRATION( RtpTest );

#endif
