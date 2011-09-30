/*
 *  Copyright (C) 2004, 2005, 2006, 2008, 2009, 2010, 2011 Savoir-Faire Linux Inc.
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
#include "AudioSrtpSession.h"

#include "sip/sipcall.h"

#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/rand.h>

#include <cstdio>
#include <cstring>
#include <cerrno>

namespace sfl
{

AudioSrtpSession::AudioSrtpSession (SIPCall * sipcall) :
    AudioSymmetricRtpSession (sipcall),
    _remoteCryptoCtx (NULL),
    _localCryptoCtx (NULL),
    _localCryptoSuite (0),
    _remoteCryptoSuite (0),
    _localMasterKeyLength (0),
    _localMasterSaltLength (0),
    _remoteMasterKeyLength (0),
    _remoteMasterSaltLength (0),
    _remoteOfferIsSet (false)
{
	this->_type = Sdes;
}

AudioSrtpSession::~AudioSrtpSession()
{
	_debug("AudioSrtp: Destroy audio srtp session");
}

void AudioSrtpSession::initLocalCryptoInfo()
{
    _debug ("AudioSrtp: Set cryptographic info for this rtp session");

    // Initialize local Crypto context
    initializeLocalMasterKey();
    initializeLocalMasterSalt();
    initializeLocalCryptoContext();

    // Set local crypto context in ccrtp
    _localCryptoCtx->deriveSrtpKeys (0);

    setOutQueueCryptoContext (_localCryptoCtx);

}

std::vector<std::string> AudioSrtpSession::getLocalCryptoInfo()
{

    _debug ("AudioSrtp: Get Cryptographic info from this rtp session");

    std::vector<std::string> crypto_vector;

    // @TODO we should return a vector containing supported
    // cryptographic context tagged 1, 2, 3...
    std::string tag = "1";

    std::string crypto_suite = sfl::CryptoSuites[_localCryptoSuite].name;

    // srtp keys formated as the following  as the following
    // inline:keyParameters|keylifetime|MasterKeyIdentifier
    // inline:NzB4d1BINUAvLEw6UzF3WSJ+PSdFcGdUJShpX1Zj|2^20|1:32
    std::string srtp_keys = "inline:";
    srtp_keys += getBase64ConcatenatedKeys();
    // srtp_keys.append("|2^20|1:32");

    // generate crypto attribute
    std::string crypto_attr = tag.append (" ");
    crypto_attr += crypto_suite.append (" ");
    crypto_attr += srtp_keys;

    _debug ("%s", crypto_attr.c_str());

    crypto_vector.push_back (crypto_attr);

    return crypto_vector;
}


void AudioSrtpSession::setRemoteCryptoInfo (sfl::SdesNegotiator& nego)
{
    if (!_remoteOfferIsSet) {

        _debug ("%s", nego.getKeyInfo().c_str());

        // Use second crypto suite if key length is 32 bit, default is 80;

        if (nego.getAuthTagLength() == "32") {
            _localCryptoSuite = 1;
            _remoteCryptoSuite = 1;
        }

        // decode keys
        unBase64ConcatenatedKeys (nego.getKeyInfo());

        // init crypto content in Srtp session
        initializeRemoteCryptoContext();
        setInQueueCryptoContext (_remoteCryptoCtx);

        // initLocalCryptoInfo();
        _remoteOfferIsSet = true;
    }

}


void AudioSrtpSession::initializeLocalMasterKey (void)
{
    _debug ("AudioSrtp: Init local master key");

    // @TODO key may have different length depending on cipher suite
    _localMasterKeyLength = sfl::CryptoSuites[_localCryptoSuite].masterKeyLength / 8;

    _debug ("AudioSrtp: Local master key length %d", _localMasterKeyLength);

    // Allocate memory for key
    unsigned char *random_key = new unsigned char[_localMasterKeyLength];

    // Generate ryptographically strong pseudo-random bytes
    int err;

    if ( (err = RAND_bytes (random_key, _localMasterKeyLength)) != 1)
        _debug ("Error occured while generating cryptographically strong pseudo-random key");

    memcpy (_localMasterKey, random_key, _localMasterKeyLength);

}


void AudioSrtpSession::initializeLocalMasterSalt (void)
{

    // @TODO key may have different length depending on cipher suite
    _localMasterSaltLength = sfl::CryptoSuites[_localCryptoSuite].masterSaltLength / 8;

    // Allocate memory for key
    unsigned char *random_key = new unsigned char[_localMasterSaltLength];

    _debug ("AudioSrtp: Local master salt length %d", _localMasterSaltLength);

    // Generate ryptographically strong pseudo-random bytes
    int err;

    if ( (err = RAND_bytes (random_key, _localMasterSaltLength)) != 1)
        _debug ("Error occured while generating cryptographically strong pseudo-random key");

    memcpy (_localMasterSalt, random_key, _localMasterSaltLength);


}


std::string AudioSrtpSession::getBase64ConcatenatedKeys()
{
    _debug ("AudioSrtp: Get base64 concatenated keys");

    // compute concatenated master and salt length
    int concatLength = _localMasterKeyLength + _localMasterSaltLength;

    uint8 concatKeys[concatLength];

    _debug ("AudioSrtp: Concatenated length %d", concatLength);

    // concatenate keys
    memcpy ( (void*) concatKeys, (void*) _localMasterKey, _localMasterKeyLength);
    memcpy ( (void*) (concatKeys + _localMasterKeyLength), (void*) _localMasterSalt, _localMasterSaltLength);

    // encode concatenated keys in base64
    return encodeBase64 ( (unsigned char*) concatKeys, concatLength);
}


void AudioSrtpSession::unBase64ConcatenatedKeys (std::string base64keys)
{
    _remoteMasterKeyLength = sfl::CryptoSuites[_remoteCryptoSuite].masterKeyLength / 8;
    _remoteMasterSaltLength = sfl::CryptoSuites[_remoteCryptoSuite].masterSaltLength / 8;

    // pointer to binary data
    char *dataptr = (char*) base64keys.data();

    // decode concatenated binary keys
    char *output = decodeBase64 ( (unsigned char*) dataptr, strlen (dataptr));

    // copy master and slt respectively
    memcpy ( (void*) _remoteMasterKey, (void*) output, _remoteMasterKeyLength);
    memcpy ( (void*) _remoteMasterSalt, (void*) (output + _remoteMasterKeyLength), _remoteMasterSaltLength);

    delete[] output;
}


void AudioSrtpSession::initializeRemoteCryptoContext (void)
{
    _debug ("AudioSrtp: Initialize remote crypto context");

    CryptoSuiteDefinition crypto = sfl::CryptoSuites[_remoteCryptoSuite];

    if (_remoteCryptoCtx) {
        delete _remoteCryptoCtx;
        _remoteCryptoCtx = NULL;
    }

    _remoteCryptoCtx = new ost::CryptoContext (0x0,
            0,                               // roc,
            0L,                              // keydr,
            SrtpEncryptionAESCM,             // encryption algo
            SrtpAuthenticationSha1Hmac,      // authtication algo
            _remoteMasterKey,
            _remoteMasterKeyLength,
            _remoteMasterSalt,
            _remoteMasterSaltLength,
            crypto.encryptionKeyLength / 8,
            crypto.srtpAuthKeyLength / 8,
            crypto.masterSaltLength / 8,                         // session salt len
            crypto.srtpAuthTagLength / 8);

}

void AudioSrtpSession::initializeLocalCryptoContext (void)
{
    _debug ("AudioSrtp: Initialize local crypto context");

    CryptoSuiteDefinition crypto = sfl::CryptoSuites[_localCryptoSuite];

    if (_localCryptoCtx) {
        delete _localCryptoCtx;
        _localCryptoCtx = NULL;
    }

    _localCryptoCtx = new ost::CryptoContext (OutgoingDataQueue::getLocalSSRC(),
            0,                               // roc,
            0L,                              // keydr,
            SrtpEncryptionAESCM,             // encryption algo
            SrtpAuthenticationSha1Hmac,      // authtication algo
            _localMasterKey,
            _localMasterKeyLength,
            _localMasterSalt,
            _localMasterSaltLength,
            crypto.encryptionKeyLength / 8,
            crypto.srtpAuthKeyLength / 8,
            crypto.masterSaltLength / 8,                         // session salt len
            crypto.srtpAuthTagLength / 8);

}

void AudioSrtpSession::restoreCryptoContext(ost::CryptoContext *localContext, ost::CryptoContext *remoteContext) {
	setInQueueCryptoContext (remoteContext);
	setOutQueueCryptoContext (localContext);
}

std::string AudioSrtpSession::encodeBase64 (unsigned char *input, int length)
{
    BIO *b64, *bmem;
    BUF_MEM *bptr ;

    // init decoder
    b64 = BIO_new (BIO_f_base64());
    BIO_set_flags (b64, BIO_FLAGS_BASE64_NO_NL);

    // init internal buffer
    bmem = BIO_new (BIO_s_mem());

    // create decoder chain
    b64 = BIO_push (b64, bmem);

    BIO_write (b64, input, length);
    // BIO_flush (b64);

    // get pointer to data
    BIO_get_mem_ptr (b64, &bptr);

    std::string output(bptr->data, bptr->length);

    BIO_free_all (bmem);

    return output;
}

char* AudioSrtpSession::decodeBase64 (unsigned char *input, int length)
{
    BIO *b64, *bmem;

    // init decoder and read-only BIO buffer
    b64 = BIO_new (BIO_f_base64());
    BIO_set_flags (b64, BIO_FLAGS_BASE64_NO_NL);

    // init internal buffer
    bmem = BIO_new_mem_buf (input, length);

    // create encoder chain
    bmem = BIO_push (b64, bmem);

    char *buffer = new char[length];
    memset (buffer, 0, length);

    BIO_read (bmem, buffer, length);

    BIO_free_all (bmem);

    return buffer;
}

}
