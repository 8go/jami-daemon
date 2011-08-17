/*
 *  Copyright (C) 2004, 2005, 2006, 2008, 2009, 2010, 2011 Savoir-Faire Linux Inc.
 *  Author: Emmanuel Milou <emmanuel.milou@savoirfairelinux.com>
 *  Author: Alexandre Savard <alexandre.savard@savoirfairelinux.com>
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

#ifndef _SDP_H
#define _SDP_H

#include <pjmedia/sdp.h>
#include <pjmedia/sdp_neg.h>
#include <pjsip/sip_transport.h>
#include <pjlib.h>
#include <pjsip_ua.h>
#include <pjmedia/errno.h>
#include <pj/pool.h>
#include <pj/assert.h>
#include <vector>
#include <string>
#include <stdexcept>

#include "sdpmedia.h"
#include "global.h" // FIXME: CodecOrder shouldn't be in global.h

namespace sfl {
    class AudioCodec;
}

class SdpException : public std::runtime_error
{
    public:
        SdpException (const std::string& str="") :
            std::runtime_error("SDP: SdpException occured: " + str) {}
};

typedef std::vector<std::string> CryptoOffer;

class Sdp
{

    public:

        /*
         * Class Constructor.
         *
         * @param ip_addr
         */
        Sdp (pj_pool_t *pool);

        ~Sdp();

        /**
         * Accessor for the internal memory pool
         */
        pj_pool_t *getMemoryPool (void) const {
            return memPool_;
        }

        /**
         *  Read accessor. Get the local passive sdp session information before negotiation
         *
         *  @return The structure that describes a SDP session
         */
        pjmedia_sdp_session *getLocalSdpSession (void) {
            return localSession_;
        }

        /**
         * Read accessor. Get the remote passive sdp session information before negotiation
         *
         * @return The structure that describe the SDP session
         */
        pjmedia_sdp_session *getRemoteSdpSession(void) {
        	return remoteSession_;
        }

        /**
         * Set the negotiated sdp offer from the sip payload.
         *
         * @param sdp   the negotiated offer
         */
        void setActiveLocalSdpSession (const pjmedia_sdp_session *sdp);

        /**
         * Retrieve the negotiated sdp offer from the sip payload.
         *
         * @param sdp   the negotiated offer
         */
        void setActiveRemoteSdpSession (const pjmedia_sdp_session *sdp);

        /**
         * Returns a string version of the negotiated SDP fields which pertain
         * to video.
         * Second member of the vector is the video codec rtp name
         */
        std::vector<std::string> getActiveVideoDescription() const;

        /**
         * Return whether or not the media have been determined for this sdp session
         */
        bool hasSessionMedia(void) const;

        /**
         * Return the codec of the first media after negotiation
         * @throw SdpException
         */
        sfl::AudioCodec* getSessionAudioCodec (void);
        const std::string &getSessionVideoCodec (void);

        /*
         * On building an invite outside a dialog, build the local offer and create the
         * SDP negotiator instance with it.
         */
        int createOffer (CodecOrder selectedCodecs, const std::vector<std::string> &videoCodecs);

        /*
        * On receiving an invite outside a dialog, build the local offer and create the
        * SDP negotiator instance with the remote offer.
        *
        * @param remote    The remote offer
        */
        int receiveOffer (const pjmedia_sdp_session* remote, CodecOrder selectedCodecs, const std::vector<std::string> &videoCodecs);

        /*
         * On receiving a message, check if it contains SDP and negotiate. Should be used for
         * SDP answer and offer but currently is only used for answer.
         * SDP negotiator instance with the remote offer.
         *
         * @param inv       The  the invitation
         * @param rdata     The remote data
         */
        int receivingAnswerAfterInitialOffer(const pjmedia_sdp_session* remote);

        /**
         * Generate answer after receiving Initial Offer
         */
        int generateAnswerAfterInitialOffer(void);

        /**
         * Start the sdp negotiation.
         *
         * @return pj_status_t  0 on success
         *                      1 otherwise
         */
        pj_status_t startNegotiation (void);

        /**
         * Update internal state after negotiation
         */
        void updateInternalState(void);

        /**
         * Remove all media in the session media vector.
         */
        void cleanSessionMedia (void);

        /**
         * Remove all media in local media capability vector
         */
        void cleanLocalMediaCapabilities (void);

        /*
         * Write accessor. Set the local IP address that will be used in the sdp session
         */
        void setLocalIP (const std::string &ip_addr) {
            localIpAddr_ = ip_addr;
        }

        /*
         * Read accessor. Get the local IP address
         */
        std::string getLocalIP (void) const {
            return localIpAddr_;
        }

        /**
         * @param Set the published audio port
         */
        void  setLocalPublishedAudioPort (int port) {
            localAudioPort_ = port;
            if (localAudioMediaCap_)
            	localAudioMediaCap_->port = port;
        }

        /**
         * @param Set the published video port
         */
        void  setLocalPublishedVideoPort (int port) {
            localVideoPort_ = port;
            if (localVideoMediaCap_)
            	localVideoMediaCap_->port = port;
        }

        /**
         * @return The published audio port
         */
        int  getLocalPublishedAudioPort (void) const {
            return localAudioPort_;
        }

        /**
         * @return The published video port
         */
        int  getLocalPublishedVideoPort (void) const {
            return localVideoPort_;
        }

        /**
         * Return IP of destination
         * @return const std:string	The remote IP address
         */
        const std::string& getRemoteIP() {
            return remoteIpAddr_;
        }

        /**
         * Return audio port at destination
         * @return unsigned int The remote audio port
         */
        unsigned int getRemoteAudioPort() const {
            return remoteAudioPort_;
        }


        /**
         * Return video port at destination
         * @return unsigned int The remote video port
         */
        unsigned int getRemoteVideoPort() const {
            return remoteVideoPort_;
        }

        /**
         *
         */
        void addAttributeToLocalAudioMedia(const std::string &attr);

        /**
         *
         */
        void removeAttributeFromLocalAudioMedia(const std::string &attr);

        void addAttributeToLocalVideoMedia(const std::string &attr);

        /**
         *
         */
        void removeAttributeFromLocalVideoMedia(const std::string &attr);

        /**
         * Get SRTP master key
         * @param remote sdp session
         * @param crypto offer
         */
        void getRemoteSdpCryptoFromOffer (const pjmedia_sdp_session* remote_sdp, CryptoOffer& crypto_offer);

        /**
         * Set the SRTP master_key
         * @param mk The Master Key of a srtp session.
         */
        void setLocalSdpCrypto (const std::vector<std::string> lc) {
            srtpCrypto_ = lc;
        }

        /**
         * Set the zrtp hash that was previously calculated from the hello message in the zrtp layer.
         * This hash value is unique at the media level. Therefore, if video support is added, one would
         * have to set the correct zrtp-hash value in the corresponding media section.
         * @param hash The hello hash of a rtp session. (Only audio at the moment)
         */
        void setZrtpHash (const std::string& hash) {
            zrtpHelloHash_ = hash;
        }

        unsigned int getTelephoneEventType() const {
        	return telephoneEventPayload_;
        }

    private:
        /**
         * The pool to allocate memory, ownership to SipCall
         * SDP should not release the pool itself
         */
        pj_pool_t *memPool_;

        /** negotiator */
        pjmedia_sdp_neg *negotiator_;

        /**
         * Local SDP
         */
        pjmedia_sdp_session *localSession_;

        /**
         * Remote SDP
         */
        pjmedia_sdp_session *remoteSession_;

        /**
         * The negotiated SDP remote session
         * Explanation: each endpoint's offer is negotiated, and a new sdp offer results from this
         * negotiation, with the compatible media from each part
         */
        pjmedia_sdp_session *activeLocalSession_;

        /**
         * The negotiated SDP remote session
         * Explanation: each endpoint's offer is negotiated, and a new sdp offer results from this
         * negotiation, with the compatible media from each part
         */
        pjmedia_sdp_session *activeRemoteSession_;

        /**
         * Codec Map used for offer
         */
        sdpMedia *localAudioMediaCap_;
        sdpMedia *localVideoMediaCap_;

        /**
         * The codecs that will be used by the session (after the SDP negotiation)
         */
        sfl::AudioCodec *sessionAudioCodec_;
        std::string sessionVideoCodec_;

        /**
         * IP address
         */
        std::string localIpAddr_;

        /**
         * Remote's IP address
         */
        std::string  remoteIpAddr_;

        /**
         * Local audio port
         */
        int localAudioPort_;

        /**
         * Local video port
         */
        int localVideoPort_;

        /**
         * Remote audio port
         */
        unsigned int remoteAudioPort_;

        /**
         * Remote video port
         */
        unsigned int remoteVideoPort_;

        /**
         * Zrtp hello hash
         */
        std::string zrtpHelloHash_;

        /**
         * "a=crypto" sdes local attributes obtained from AudioSrtpSession
         */
        std::vector<std::string> srtpCrypto_;

        /**
         * Payload type for dtmf telephone event
         */
        unsigned int telephoneEventPayload_;

        Sdp (const Sdp&); //No Copy Constructor

        Sdp& operator= (const Sdp&); //No Assignment Operator

        std::string getLineFromLocalSDP(const std::string &keyword) const;

        /*
         * Build the sdp media section
         * Add rtpmap field if necessary
         *
         * @param media The media to add to SDP
         */
        void setMediaDescriptorLine (sdpMedia* media);

        void setTelephoneEventRtpmap(pjmedia_sdp_media *med);

        /**
         * Build the local media capabilities for this session
         * @param List of codec in preference order
         */
        void setLocalMediaCapabilities (CodecOrder selectedCodecs);
        void setLocalMediaVideoCapabilities (const std::vector<std::string> &videoCodecs);

        /*
         * Build the local SDP offer
         */
        int createLocalSession (CodecOrder selectedCodecs, const std::vector<std::string> &videoCodecs);

        /*
         *  Mandatory field: Protocol version ("v=")
         *  Add the protocol version in the SDP session description
         */
        void addProtocol (void);

        /*
         *  Mandatory field: Origin ("o=")
         *  Gives the originator of the session.
         *  Serves as a globally unique identifier for this version of this session description.
         */
        void addOrigin (void);

        /*
         *  Mandatory field: Session name ("s=")
         *  Add a textual session name.
         */
        void addSessionName (void);

        /*
         *  Optional field: Connection data ("c=")
         *  Contains connection data.
         */
        void addConnectionInfo (void);

        /*
         *  Mandatory field: Timing ("t=")
         *  Specify the start and the stop time for a session.
         */
        void addTiming (void);

        /*
         * Adds a sdes attribute to the given media section.
         *
         * @param media The media to add the srtp attribute to
         * @throw SdpException
         */
        void addSdesAttribute (const std::vector<std::string>& crypto);

        /*
         * Adds a zrtp-hash  attribute to
         * the given media section. The hello hash is
         * available only after is has been computed
         * in the AudioZrtpSession constructor.
         *
         * @param media The media to add the zrtp-hash attribute to
         * @param hash  The hash to which the attribute should be set to
         * @throw SdpException
         */
        void addZrtpAttribute (pjmedia_sdp_media* media, std::string hash);

        void setRemoteIpFromSdp (const pjmedia_sdp_session *r_sdp);

        void setRemoteAudioPortFromSdp (pjmedia_sdp_media *r_media);
        
        void updateMediaTransportInfoFromRemoteSdp ();

        void getRemoteSdpTelephoneEventFromOffer(const pjmedia_sdp_session *remote_sdp);
};

#endif
