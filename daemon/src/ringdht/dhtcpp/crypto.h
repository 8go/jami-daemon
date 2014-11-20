/*
 *  Copyright (C) 2014 Savoir-Faire Linux Inc.
 *  Author : Adrien Béraud <adrien.beraud@savoirfairelinux.com>
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

#pragma once

#include "serialize.h"

extern "C" {
#include <gnutls/gnutls.h>
#include <gnutls/abstract.h>
#include <gnutls/x509.h>
}

#include <vector>
#include <memory>

namespace dht {
namespace crypto {

struct PrivateKey;
struct Certificate;

typedef std::pair<std::shared_ptr<PrivateKey>, std::shared_ptr<Certificate>> Identity;

/**
 * Generate an RSA key pair (2048 bits) and a certificate.
 * If a certificate authority (ca) is provided, it will be used to
 * sign the certificate, otherwise the certificate will be self-signed.
 */
Identity generateIdentity(const std::string& name = "dhtnode", Identity ca = {});

struct PublicKey : public Serializable
{
    PublicKey() {}
    PublicKey(gnutls_pubkey_t k) : pk(k) {}
    PublicKey(const Blob& pk);
    PublicKey(PublicKey&& o) noexcept : pk(o.pk) { o.pk = nullptr; };

    ~PublicKey();
    operator bool() const { return pk; }

    PublicKey& operator=(PublicKey&& o) noexcept;

    InfoHash getId() const;
    bool checkSignature(const Blob& data, const Blob& signature) const;
    Blob encrypt(const Blob&) const;

    void pack(Blob& b) const override;

    void unpack(Blob::const_iterator& begin, Blob::const_iterator& end) override;

    gnutls_pubkey_t pk {};
private:
    PublicKey(const PublicKey&) = delete;
    PublicKey& operator=(const PublicKey&) = delete;
};

struct PrivateKey
{
    PrivateKey() {}
    //PrivateKey(gnutls_privkey_t k) : key(k) {}
    PrivateKey(gnutls_x509_privkey_t k);
    PrivateKey(PrivateKey&& o) noexcept : key(o.key), x509_key(o.x509_key)
        { o.key = nullptr; o.x509_key = nullptr; };
    PrivateKey& operator=(PrivateKey&& o) noexcept;

    PrivateKey(const Blob& import);
    ~PrivateKey();
    operator bool() const { return key; }
    PublicKey getPublicKey() const;
    Blob serialize() const;
    Blob sign(const Blob&) const;
    Blob decrypt(const Blob& cypher) const;

    /**
     * Generate a new RSA key pair
     */
    static PrivateKey generate();

private:
    PrivateKey(const PrivateKey&) = delete;
    PrivateKey& operator=(const PrivateKey&) = delete;
    gnutls_privkey_t key {};
    gnutls_x509_privkey_t x509_key {};

    friend dht::crypto::Identity dht::crypto::generateIdentity(const std::string&, dht::crypto::Identity);
};

struct Certificate : public Serializable {
    Certificate() {}
    Certificate(gnutls_x509_crt_t crt) : cert(crt) {}
    Certificate(const Blob& crt);
    Certificate(Certificate&& o) noexcept : cert(o.cert) { o.cert = nullptr; };
    Certificate& operator=(Certificate&& o) noexcept;

    ~Certificate();
    operator bool() const { return cert; }
    PublicKey getPublicKey() const;
    void pack(Blob& b) const override;
    void unpack(Blob::const_iterator& begin, Blob::const_iterator& end) override;

private:
    Certificate(const Certificate&) = delete;
    Certificate& operator=(const Certificate&) = delete;
    gnutls_x509_crt_t cert {};

    friend dht::crypto::Identity dht::crypto::generateIdentity(const std::string&, dht::crypto::Identity);
};


}
}
