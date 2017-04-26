/*
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

   Product name: redemption, a FLOSS RDP proxy
   Copyright (C) Wallix 2017
   Author(s): Christophe Grosjean, Jonathan Poelen

*/

#include "main/redcryptofile.hpp"
#include "transport/out_crypto_transport.hpp"
#include "transport/in_crypto_transport.hpp"

#include <memory>

#include "../tests/includes/test_only/lcg_random.hpp" // TODO


#define CHECK_HANDLE(handle) if (!handle) return -1
#define CHECK_NOTHROW(exp) do { try { exp; } catch (...) { return -1; } } while (0)

extern "C"
{

struct CryptoContextWrapper
{
    CryptoContext cctx;

    CryptoContextWrapper(get_hmac_key_prototype * hmac_fn, get_trace_key_prototype * trace_fn)
    {
        cctx.set_get_hmac_key_cb(hmac_fn);
        cctx.set_get_trace_key_cb(trace_fn);
    }
};

struct RedCryptoWriterHandle
{
    enum RandomType { LCG, UDEV };

    RedCryptoWriterHandle(
        RandomType random_type,
        bool with_encryption, bool with_checksum,
        get_hmac_key_prototype * hmac_fn, get_trace_key_prototype * trace_fn)
    : cctx_wrapper(hmac_fn, trace_fn)
    , random_wrapper(random_type)
    , out_crypto_transport(with_encryption, with_checksum, cctx_wrapper.cctx, *random_wrapper.rnd)
    {}

private:
    struct RandomWrapper
    {
        Random * rnd;

        RandomWrapper(RandomType rnd_type)
        {
            switch (rnd_type) {
                case LCG:
                    new (&u.lcg) LCGRandom(0);
                    rnd = &u.lcg;
                    break;
                case UDEV:
                    new (&u.udev) UdevRandom();
                    rnd = &u.udev;
                    break;
            }
        }

        ~RandomWrapper()
        {
            rnd->~Random();
        }

    private:
        union U {
            LCGRandom lcg; /* for reproductible tests */
            UdevRandom udev;
            char dummy;
            U() : dummy() {}
            ~U() {}
        } u;
    };

    CryptoContextWrapper cctx_wrapper;
    RandomWrapper random_wrapper;

public:
    HashHexArray qhashhex;
    HashHexArray fhashhex;

    OutCryptoTransport out_crypto_transport;
};


struct RedCryptoReaderHandle
{
    RedCryptoReaderHandle(InCryptoTransport::EncryptionMode encryption
                        , get_hmac_key_prototype * hmac_fn
                        , get_trace_key_prototype * trace_fn)
    : cctxw(hmac_fn, trace_fn)
    , in_crypto_transport(cctxw.cctx, encryption)
    {}

private:
    CryptoContextWrapper cctxw;

public:
    InCryptoTransport in_crypto_transport;
};



using HashArray = uint8_t[MD_HASH::DIGEST_LENGTH];

inline void hash_to_hashhex(HashArray const & hash, HashHexArray hashhex) noexcept {
    char const * t = "0123456789ABCDEF";
    static_assert(sizeof(hash) * 2 + 1 == sizeof(HashHexArray), "");
    auto phex = hashhex;
    for (uint8_t c : hash) {
        *phex++ = t[c >> 4];
        *phex++ = t[c & 0xf];
    }
    *phex = '\0';
}


const char * redcryptofile_writer_qhashhex(RedCryptoWriterHandle * handle) {
    return handle->qhashhex;
}

const char * redcryptofile_writer_fhashhex(RedCryptoWriterHandle * handle) {
    return handle->fhashhex;
}


RedCryptoWriterHandle * redcryptofile_writer_new(int with_encryption
                                               , int with_checksum
                                               , get_hmac_key_prototype * hmac_fn
                                               , get_trace_key_prototype * trace_fn) {
    LOG(LOG_INFO, "redcryptofile_writer_new()");
    try {
        auto handle = new (std::nothrow) RedCryptoWriterHandle(
            RedCryptoWriterHandle::LCG /* TODO UDEV */, with_encryption, with_checksum, hmac_fn, trace_fn
        );
        LOG(LOG_INFO, "redcryptofile_writer_new -> exit");
        return handle;
    }
    catch (...) {
        LOG(LOG_INFO, "redcryptofile_writer_new() -> exit exception");
        return nullptr;
    }
}

int redcryptofile_writer_open(RedCryptoWriterHandle * handle, const char * path) {
    LOG(LOG_INFO, "redcryptofile_writer_open()");
    CHECK_HANDLE(handle);
    CHECK_NOTHROW(handle->out_crypto_transport.open(path, 0 /* TODO groupid */));
    return 0;
}


int redcryptofile_writer_write(RedCryptoWriterHandle * handle, uint8_t const * buffer, unsigned long len) {
    LOG(LOG_INFO, "redcryptofile_write()");
    CHECK_HANDLE(handle);
    try {
        handle->out_crypto_transport.send(buffer, len);
    }
    catch (...)
    {
        return -1;
    }
    LOG(LOG_INFO, "redcryptofile_write() done");
    return len;
}


int redcryptofile_writer_close(RedCryptoWriterHandle * handle) {
    LOG(LOG_INFO, "redcryptofile_writer_close()");
    CHECK_HANDLE(handle);
    HashArray qhash;
    HashArray fhash;
    CHECK_NOTHROW(handle->out_crypto_transport.close(qhash, fhash));
    if (handle) {
        hash_to_hashhex(qhash, handle->qhashhex);
    }
    if (handle) {
        hash_to_hashhex(fhash, handle->fhashhex);
    }

    hexdump(qhash, sizeof(HashArray));
    

    LOG(LOG_INFO, "redcryptofile_writer_close() done");
    return 0;
}


void redcryptofile_writer_delete(RedCryptoWriterHandle * handle) {
    LOG(LOG_INFO, "redcryptofile_writer_delete()");
    delete handle;
    LOG(LOG_INFO, "redcryptofile_writer_delete() done");
}


RedCryptoReaderHandle * redcryptofile_reader_new(get_hmac_key_prototype* hmac_fn
                                               , get_trace_key_prototype* trace_fn) {
    LOG(LOG_INFO, "redcryptofile_reader_new()");
    try {
        auto handle = new (std::nothrow) RedCryptoReaderHandle(
            InCryptoTransport::EncryptionMode::Auto, hmac_fn, trace_fn
        );
        LOG(LOG_INFO, "redcryptofile_reader_new() -> exit ok");
        return handle;
    }
    catch (...) {
        LOG(LOG_INFO, "redcryptofile_reader_new() -> exit exception");
        return nullptr;
    }
}

int redcryptofile_reader_open(RedCryptoReaderHandle * handle, char const * path) {
    LOG(LOG_INFO, "redcryptofile_reader_open()");
    CHECK_HANDLE(handle);
    CHECK_NOTHROW(handle->in_crypto_transport.open(path));
    return 0;
}


// 0: if end of file, len: if data was read, negative number on error
int redcryptofile_reader_read(RedCryptoReaderHandle * handle, uint8_t * buffer, unsigned long len) {
    LOG(LOG_INFO, "redcryptofile_reader_read()");
    CHECK_HANDLE(handle);
    try {
        LOG(LOG_INFO, "redcryptofile_reader_read() done");
        return handle->in_crypto_transport.partial_read(buffer, len);
    }
    catch (Error e) {
        LOG(LOG_INFO, "redcryptofile_reader_read() error");
        return -e.id;
    }
    catch (...) {
        LOG(LOG_INFO, "redcryptofile_reader_read() error");
        return -1;
    }
}

int redcryptofile_reader_close(RedCryptoReaderHandle * handle) {
    LOG(LOG_INFO, "redcryptofile_reader_close()");
    CHECK_HANDLE(handle);
    std::unique_ptr<RedCryptoReaderHandle> u(handle);
    CHECK_NOTHROW(handle->in_crypto_transport.close());
    LOG(LOG_INFO, "redcryptofile_reader_close() done");
    return 0;
}

void redcryptofile_reader_delete(RedCryptoReaderHandle * handle) {
    LOG(LOG_INFO, "redcryptofile_reader_delete()");
    delete handle;
    LOG(LOG_INFO, "redcryptofile_reader_delete() done");
}

}
