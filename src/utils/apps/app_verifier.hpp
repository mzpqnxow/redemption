/*
   Product name: redemption, a FLOSS RDP proxy
   Copyright (C) Wallix 2013
   Author(s): Christophe Grosjean, Raphael Zhou

   redrec video verifier program
*/

#include <iostream>

#include <utility>
#include <string>

#include "main/version.hpp"
#include "transport/in_filename_transport.hpp"
#include "transport/in_meta_sequence_transport.hpp"
#include "system/ssl_calls.hpp"
#include "configs/config.hpp"

#include "program_options/program_options.hpp"

#ifndef HASH_LEN
#define HASH_LEN 64
#endif

#define QUICK_CHECK_LENGTH 4096


struct ifile_read_API
{
    // ifile is a thin API layer over system open/read/close
    // it means open/read/close mimicks system open/read/close
    // (except fd is wrapped in an object)

    int fd;
    ifile_read_API() : fd(-1) {}
    // We choose to define an open function to mimick system behavior
    // instead of opening through constructor. This allows to manage
    // explicit error management depending on return code.
    // if open worked open returns 0 and this->fd contains file descriptor
    // negative code are errors, return EINVAL if lib software related
    virtual int open(const char * s) = 0;
    // read can either return the number of bytes asked or less.
    // That the exact number of bytes is returned is never 
    // guaranteed and checking that is at caller's responsibility
    // if some error occurs the return is -1 and the error code
    // is in errno, like for system calls.
    // returning 0 means EOF
    virtual int read(char * buf, size_t len) = 0;
    // close beside calling the system call must also ensure it sets fd to 1
    // this is to avoid performing close twice when called explicitely
    // as it is also performed by destructor (in most cases there will be
    // no reason for calling close explicitly).
    virtual void close()
    {
        ::close(fd);
        this->fd = -1;
    }

    virtual ~ifile_read_API(){
        if (this->fd != -1){
            this->close();
        }
    }
};

struct ifile_read : public ifile_read_API
{
    int open(const char * s)
    {
        this->fd = ::open(s, O_RDONLY);
        return this->fd;
    }
    int read(char * buf, size_t len)
    {
        return ::read(this->fd, buf, len);
    }
    virtual ~ifile_read(){}
};


struct MetaLine2
{
    char    filename[PATH_MAX + 1];
    off_t   size;
    mode_t  mode;
    uid_t   uid;
    gid_t   gid;
    dev_t   dev;
    ino_t   ino;
    time_t  mtime;
    time_t  ctime;
    time_t  start_time;
    time_t  stop_time;
    unsigned char hash1[MD_HASH_LENGTH];
    unsigned char hash2[MD_HASH_LENGTH];
};


struct HashHeader {
    unsigned version;
};

class ifile_read_encrypted : public ifile_read_API
{
public:
    CryptoContext * cctx;
    int fd;
    char clear_data[CRYPTO_BUFFER_SIZE];  // contains either raw data from unencrypted file
                                          // or already decrypted/decompressed data
    uint32_t clear_pos;                   // current position in clear_data buf
    uint32_t raw_size;                    // the unciphered/uncompressed data available in buffer

    EVP_CIPHER_CTX ectx;                  // [en|de]cryption context
    uint32_t state;                       // enum crypto_file_state
    unsigned int   MAX_CIPHERED_SIZE;     // = MAX_COMPRESSED_SIZE + AES_BLOCK_SIZE;
    int encryption;

public:
    explicit ifile_read_encrypted(CryptoContext * cctx, int encryption)
    : cctx(cctx)
    , fd(-1)
    , clear_data{}
    , clear_pos(0)
    , raw_size(0)
    , state(0)
    , MAX_CIPHERED_SIZE(0)
    , encryption(encryption)
    {
    }

    ~ifile_read_encrypted()
    {
        if (-1 != this->fd) {
            ::close(this->fd);
            this->fd = -1;
        }
    }

    int open(const char * filename)
    {
        this->fd = ::open(filename, O_RDONLY);
        if (this->fd < 0) {
            return this->fd;
        }

        if (this->encryption){
            size_t base_len = 0;
            const uint8_t * base = reinterpret_cast<const uint8_t *>(basename_len(filename, base_len));

            ::memset(this->clear_data, 0, sizeof(this->clear_data));
            ::memset(&this->ectx, 0, sizeof(this->ectx));
            this->clear_pos = 0;
            this->raw_size = 0;
            this->state = 0;
            const size_t MAX_COMPRESSED_SIZE = ::snappy_max_compressed_length(CRYPTO_BUFFER_SIZE);
            this->MAX_CIPHERED_SIZE = MAX_COMPRESSED_SIZE + AES_BLOCK_SIZE;

            uint8_t data[40];
            size_t avail = 0;
            while (avail != 40) {
                ssize_t ret = ::read(this->fd, &data[avail], 40-avail);
                if (ret < 0 && errno == EINTR){
                    continue;
                }
                if (ret <= 0){
                    // Either read error or EOF: in both cases we are in trouble
                    if (ret == 0) {
                        // see error management, we would need object internal errors
                        // basically this case means: failed to read compression header
                        // error.
                        errno = EINVAL;
                    }
                    this->close();
                    return - 1;
                }
                avail += ret;
            }

            // Encrypted/Compressed file header (40 bytes)
            // -------------------------------------------
            // MAGIC: 4 bytes
            // 0x57 0x43 0x46 0x4D (WCFM)
            // VERSION: 4 bytes
            // 0x01 0x00 0x00 0x00
            // IV: 32 bytes
            // (random)
            
            
            Parse p(data);
            const uint32_t magic = p.in_uint32_le();
            if (magic != WABCRYPTOFILE_MAGIC) {
                LOG(LOG_ERR, "[CRYPTO_ERROR][%d]: Wrong file type %04x != %04x\n",
                    ::getpid(), magic, WABCRYPTOFILE_MAGIC);
                errno = EINVAL;
                this->close();
                return -1;
            }

            const int version = p.in_uint32_le();
            if (version > WABCRYPTOFILE_VERSION) {
                LOG(LOG_ERR, "[CRYPTO_ERROR][%d]: Unsupported version %04x > %04x\n",
                    ::getpid(), version, WABCRYPTOFILE_VERSION);
                errno = EINVAL;
                this->close();
                return -1;
            }

            // replace p.p with some arrayview of 32 bytes
            const uint8_t * const iv = p.p;
            const EVP_CIPHER * cipher  = ::EVP_aes_256_cbc();
            const uint8_t salt[]  = { 0x39, 0x30, 0x00, 0x00, 0x31, 0xd4, 0x00, 0x00 };
            const int          nrounds = 5;
            unsigned char      key[32];
            unsigned char trace_key[CRYPTO_KEY_LENGTH]; // derived key for cipher
            cctx->get_derived_key(trace_key, base, base_len);
            if (32 != ::EVP_BytesToKey(cipher, ::EVP_sha1(), salt,
                               trace_key, CRYPTO_KEY_LENGTH, nrounds, key, nullptr)){
                // TODO: add error management
                LOG(LOG_ERR, "[CRYPTO_ERROR][%d]: EVP_BytesToKey size is wrong\n", ::getpid());
                errno = EINVAL;
                this->close();
                return -1;
            }

            ::EVP_CIPHER_CTX_init(&this->ectx);
            if(::EVP_DecryptInit_ex(&this->ectx, cipher, nullptr, key, iv) != 1) {
                // TODO: add error management
                errno = EINVAL;
                LOG(LOG_ERR, "[CRYPTO_ERROR][%d]: Could not initialize decrypt context\n", ::getpid());
                this->close();
                return -1;
            }
        }
        return 0;
    }

    int read(char * data, size_t len)
    {
        if (this->encryption){
            if (this->state & CF_EOF) {
                return 0;
            }

            unsigned int requested_size = len;

            while (requested_size > 0) {
                // Check how much we have already decoded
                if (!this->raw_size) {
                    uint8_t hlen[4] = {};
                    {
                        size_t rlen = 4;
                        while (rlen) {
                            ssize_t ret = ::read(this->fd, &hlen[4 - rlen], rlen);
                            if (ret < 0){
                                if (errno == EINTR){
                                    continue;
                                }
                                // Error should still be there next time we try to read: fatal
                                this->close();
                                return - 1;
                            }
                            // Unexpected EOF, we are in trouble for decompression: fatal
                            if (ret == 0){
                                this->close();
                                return -1;
                            }
                            rlen -= ret;
                        }
                    }

                    Parse p(hlen);
                    uint32_t ciphered_buf_size = p.in_uint32_le();
                    if (ciphered_buf_size == WABCRYPTOFILE_EOF_MAGIC) { // end of file
                        this->state = CF_EOF;
                        this->clear_pos = 0;
                        this->raw_size = 0;
                        this->close();
                        break;
                    }

                    if (ciphered_buf_size > this->MAX_CIPHERED_SIZE) {
                        LOG(LOG_ERR, "[CRYPTO_ERROR][%d]: Integrity error, erroneous chunk size!\n", ::getpid());
                        this->close();
                        return -1;
                    }

                    uint32_t compressed_buf_size = ciphered_buf_size + AES_BLOCK_SIZE;

                    //char ciphered_buf[ciphered_buf_size];
                    unsigned char ciphered_buf[65536];
                    //char compressed_buf[compressed_buf_size];
                    unsigned char compressed_buf[65536];

                    {
                        size_t rlen = ciphered_buf_size;
                        while (rlen) {
                            ssize_t ret = ::read(this->fd, &ciphered_buf[ciphered_buf_size - rlen], rlen);
                            if (ret < 0){
                                if (errno == EINTR){
                                    continue;
                                }
                                // Error should still be there next time we try to read
                                // TODO: see if we have already decrypted data
                                // error reported too early
                                this->close();
                                return - 1;
                            }
                            // We must exit loop or we will enter infinite loop
                            if (ret == 0){
                                // TODO: see if we have already decrypted data
                                // error reported too early
                                this->close();
                                return -1;
                            }
                            rlen -= ret;
                        }
                    }

                    int safe_size = compressed_buf_size;
                    int remaining_size = 0;

                    /* allows reusing of ectx for multiple encryption cycles */
                    if (EVP_DecryptInit_ex(&this->ectx, nullptr, nullptr, nullptr, nullptr) != 1){
                        LOG(LOG_ERR, "[CRYPTO_ERROR][%d]: Could not prepare decryption context!\n", getpid());
                        return -1;
                    }
                    if (EVP_DecryptUpdate(&this->ectx, compressed_buf, &safe_size, ciphered_buf, ciphered_buf_size) != 1){
                        LOG(LOG_ERR, "[CRYPTO_ERROR][%d]: Could not decrypt data!\n", getpid());
                        return -1;
                    }
                    if (EVP_DecryptFinal_ex(&this->ectx, compressed_buf + safe_size, &remaining_size) != 1){
                        LOG(LOG_ERR, "[CRYPTO_ERROR][%d]: Could not finish decryption!\n", getpid());
                        return -1;
                    }
                    compressed_buf_size = safe_size + remaining_size;

                    size_t chunk_size = CRYPTO_BUFFER_SIZE;
                    const snappy_status status = snappy_uncompress(
                            reinterpret_cast<char *>(compressed_buf),
                            compressed_buf_size, this->clear_data, &chunk_size);

                    switch (status)
                    {
                        case SNAPPY_OK:
                            break;
                        case SNAPPY_INVALID_INPUT:
                            LOG(LOG_ERR, "[CRYPTO_ERROR][%d]: Snappy decompression failed with status code INVALID_INPUT!\n", getpid());
                            return -1;
                        case SNAPPY_BUFFER_TOO_SMALL:
                            LOG(LOG_ERR, "[CRYPTO_ERROR][%d]: Snappy decompression failed with status code BUFFER_TOO_SMALL!\n", getpid());
                            return -1;
                        default:
                            LOG(LOG_ERR, "[CRYPTO_ERROR][%d]: Snappy decompression failed with unknown status code (%d)!\n", getpid(), status);
                            return -1;
                    }

                    this->clear_pos = 0;
                    // When reading, raw_size represent the current chunk size
                    this->raw_size = chunk_size;

                    // TODO: check that
                    if (!this->raw_size) { // end of file reached
                        break;
                    }
                }
                // remaining_size is the amount of data available in decoded buffer
                unsigned int remaining_size = this->raw_size - this->clear_pos;
                // Check how much we can copy
                unsigned int copiable_size = std::min(remaining_size, requested_size);
                // Copy buffer to caller
                ::memcpy(&data[len - requested_size], this->clear_data + this->clear_pos, copiable_size);
                this->clear_pos      += copiable_size;
                requested_size -= copiable_size;
                // Check if we reach the end
                if (this->raw_size == this->clear_pos) {
                    this->raw_size = 0;
                }
            }
            return len - requested_size;
        }
        else {
            // for non encrypted file, returning partial read is OK
            return::read(this->fd, data, len);
        }
    }
};

struct MetaHeaderXXX {
    unsigned version;
    //unsigned witdh;
    //unsigned height;
    bool has_checksum;
};


class MwrmReaderXXX
{
    public:
    MetaHeaderXXX header;
    
//    private:
    char buf[1024];
    char * eof;
    char * eol;
    char * cur;
    ifile_read_API & ibuf;


    long long int get_ll(char * & cur, char * eol, char sep, int err)
    {
        char * pos = std::find(cur, eol, sep);
        if (pos == eol || (pos - cur < 1)){
            throw Error(err);
        }
        char * pend = nullptr;
        long long int res = strtoll(cur, &pend, 10);
        if (pend != pos){
            throw Error(err);
        }
        cur = pos + 1;
        return res;
    }

    void in_copy_bytes(uint8_t * hash, int len, char * & cur, char * eol, int err)
    {
        if (eol - cur < len){
            throw Error(err);
        }
        memcpy(hash, cur, len);
        cur += len;
    }

    void in_hex256(uint8_t * hash, int len, char * & cur, char * eol, char sep, int exc)
    {
        int err = 0;
        char * pos = std::find(cur, eol, sep);
        if (pos == eol || (pos - cur != 2*len)){
            throw Error(exc);
        }
        for (int i = 0 ; i < len ; i++){
            hash[i] = (chex_to_int(cur[i*2u], err)*16)
                     + chex_to_int(cur[i*2u+1], err);
        }
        if (err){
            throw Error(err);
        }
        cur = pos + 1;
    }

public:
    MwrmReaderXXX(ifile_read_API & reader_buf) noexcept
    : buf{}
    , eof(buf)
    , eol(buf)
    , cur(buf)
    , ibuf(reader_buf)
    {
        memset(this->buf, 0, sizeof(this->buf));
    }

    int read_meta_file2(MetaLine2 & meta_line) 
    {
        try {
            this->read_meta_file(meta_line);
            return 0;
        }
        catch(...){
            return 1;
        };
    }

    void read_meta_headers()
    {
        this->next_line(); // v2
        this->header.version = (this->cur[0] == 'v')?2:1;
        if (this->header.version == 2) {
            this->next_line(); // 800 600
            this->next_line(); // checksum or nochecksum
        }
        this->header.has_checksum = (header.version > 1) 
                                 && (this->cur[0] == 'c');
        // else v1
        this->next_line(); // blank
        this->next_line(); // blank
    }

    void read_meta_file(MetaLine2 & meta_line) 
    {
        this->next_line();
        size_t len = this->eol - this->cur;

        // Line format "fffff 
        // [st_size st_mode st_uid st_gid st_dev st_ino st_mtime st_ctime]
        // sssss eeeee hhhhh HHHHH"
        //            ^  ^  ^  ^
        //            |  |  |  |
        //            |hash1|  |
        //            |     |  |
        //        space3    |hash2
        //                  |
        //                space4
        // filename(1 or >) + space(1) + [stat_info(ll|ull * 8) + space(1)] 
        // + start_sec(1 or >) + space(1) + stop_sec(1 or >) +
        //     space(1) + hash1(64) + space(1) + hash2(64) >= 135

        typedef std::reverse_iterator<char*> reverse_iterator;
        reverse_iterator first(this->cur);
        reverse_iterator space(this->cur+len);                
        for(int i = 0; i < ((this->header.version==1)?2:10) + 2*this->header.has_checksum; i++){
            space = std::find(space, first, ' ');                
            space++;
        }
        int path_len = first-space;
        this->in_copy_bytes(reinterpret_cast<uint8_t*>(meta_line.filename), 
                            path_len, this->cur, this->eol, ERR_TRANSPORT_READ_FAILED);
        this->cur++;
        meta_line.filename[path_len] = 0;

        if (this->header.version == 1){
            meta_line.size = 0;
            meta_line.mode = 0;
            meta_line.uid = 0;
            meta_line.gid = 0;
            meta_line.dev = 0;
            meta_line.ino = 0;
            meta_line.mtime = 0;
            meta_line.ctime = 0;
        }
        else{ // v2
            meta_line.size = this->get_ll(cur, eol, ' ', ERR_TRANSPORT_READ_FAILED);
            meta_line.mode = this->get_ll(cur, eol, ' ', ERR_TRANSPORT_READ_FAILED);
            meta_line.uid = this->get_ll(cur, eol, ' ', ERR_TRANSPORT_READ_FAILED);
            meta_line.gid = this->get_ll(cur, eol, ' ', ERR_TRANSPORT_READ_FAILED);
            meta_line.dev = this->get_ll(cur, eol, ' ', ERR_TRANSPORT_READ_FAILED);
            meta_line.ino = this->get_ll(cur, eol, ' ', ERR_TRANSPORT_READ_FAILED);
            meta_line.mtime = this->get_ll(cur, eol, ' ', ERR_TRANSPORT_READ_FAILED);
            meta_line.ctime = this->get_ll(cur, eol, ' ', ERR_TRANSPORT_READ_FAILED);
        }

        // st_start_time + space
        meta_line.start_time = this->get_ll(cur, eol, ' ', ERR_TRANSPORT_READ_FAILED);
        // st_stop_time + space
        meta_line.stop_time = this->get_ll(cur, eol, this->header.has_checksum?' ':'\n',
                                           ERR_TRANSPORT_READ_FAILED);
        if (this->header.has_checksum){
            // HASH1 + space
            this->in_hex256(meta_line.hash1, MD_HASH_LENGTH, cur, eol, ' ', ERR_TRANSPORT_READ_FAILED);
            // HASH1 + CR
            this->in_hex256(meta_line.hash2, MD_HASH_LENGTH, cur, eol, '\n', ERR_TRANSPORT_READ_FAILED);
        }
        else {
            memset(meta_line.hash1, 0, sizeof(meta_line.hash1));
            memset(meta_line.hash2, 0, sizeof(meta_line.hash2));
        }

        // TODO: check the whole line has been consumed (or it's an error)
        this->cur = this->eol;
    }

    void next_line()
    {
        this->cur = this->eol;
        while (this->cur == this->eof) // empty buffer
        {
            ssize_t ret = this->ibuf.read(this->buf, sizeof(this->buf)-1);
            if (ret < 0) {
                throw Error(ERR_TRANSPORT_READ_FAILED, errno);
            }
            if (ret == 0) {
                break;
            }
            this->cur = this->buf;
            this->eof = this->buf + ret;
            this->eof[0] = 0;
        }
        char * pos = std::find(this->cur, this->eof, '\n');
        while (pos == this->eof){ // read and append to buffer
            size_t len = -(this->eof-this->cur);
            if (len >= sizeof(buf)-1){
                // if the buffer can't hold at least one line, 
                // there is some problem behind
                // if a line were available we should have found \n
                throw Error(ERR_TRANSPORT_READ_FAILED, errno);
            }
            ssize_t ret = this->ibuf.read(this->eof, sizeof(this->buf)-1-len);
            if (ret < 0) {
                throw Error(ERR_TRANSPORT_READ_FAILED, errno);
            }
            if (ret == 0) {
                break;
            }
            this->eof += ret;
            this->eof[0] = 0;
            pos = std::find(this->cur, this->eof, '\n');
        }
        this->eol = (pos == this->eof)?this->eof:pos+1; // set eol after \n (start of next line)
    }
};

ssize_t file_size(const char * filename)
{
    // we just check the size match
    // used when checksum is active
    struct stat64 sb;
    memset(&sb, 0, sizeof(sb));
    if (lstat64(filename, &sb) < 0){
        return -1;
    }
    return sb.st_size;
}

// Compute HmacSha256 
// up to check_size orf end of file whicherver happens first
// if check_size == 0, checks to eof
// return 0 on success and puts signature in provided buffer
// return -1 if some system error occurs, errno contains actual error
int file_start_hmac_sha256(const char * filename, 
                     uint8_t const * crypto_key,
                     size_t          key_len,
                     size_t          check_size,
                     uint8_t (& hash)[SHA256_DIGEST_LENGTH])
{
    struct fdwrap
    {
        int fd;
        fdwrap(int fd) : fd(fd) {}
        ~fdwrap(){ if (fd >=0) {::close(fd);} }
    } file(::open(filename, O_RDONLY));
    if (file.fd < 0) { return file.fd; }

    SslHMAC_Sha256 hmac(crypto_key, key_len);

    uint8_t buf[4096] = {};
    ssize_t ret = ::read(file.fd, buf, sizeof(buf));
    for (size_t  number_of_bytes_read = 0 ; ret ; number_of_bytes_read += ret){
        // number_of_bytes_read < check_size
        if (ret < 0){
            // interruption signal, not really an error
            if (errno == EINTR){
                continue;
            }
            return -1;
        }
        if (check_size && number_of_bytes_read + ret > check_size){
            hmac.update(buf, check_size - number_of_bytes_read);
            break;
        }
        if (ret == 0){ break; }
        hmac.update(buf, ret);
        ret = ::read(file.fd, buf, sizeof(buf));
    }
    hmac.final(&hash[0], SHA256_DIGEST_LENGTH);
    return 0;
}

class MwrmReader : public MwrmReaderXXX
{
    void in_copy_bytes(uint8_t * hash, int len, char * & cur, char * eof, int err)
    {
        if (eof - cur < len){
            throw Error(err);
        }
        memcpy(hash, cur, len);
        cur += len;
    }

public:
    MwrmReader(ifile_read_API & reader_buf) noexcept
    : MwrmReaderXXX(reader_buf)
    {
        memset(this->buf, 0, sizeof(this->buf));
    }

    int read_meta_file2(MetaHeaderXXX const & meta_header, MetaLine2 & meta_line) {
        printf("read_meta_file2\n");
        try {
            if (meta_header.version == 1) {
                this->read_meta_file_v1(meta_line);
                return 0;
            }
            else {
                this->read_meta_file_v2(meta_header, meta_line);
                return 0;
            }
        }
        catch(...){
            return 1;
        };
    }

    MetaHeaderXXX read_meta_headers()
    {
        printf("read_meta_headers\n");
        MetaHeaderXXX header{1, false};

        this->next_line();

        // v2
        if (this->cur[0] == 'v') {
            this->next_line();
            this->next_line();
            header.version = 2;
            header.has_checksum = (this->cur[0] == 'c');
        }
        // else v1
        this->next_line();
        this->next_line();
        return header;
    }

    void read_meta_file_v1(MetaLine2 & meta_line)
    {
        printf("read_meta_file_v1\n");
        this->next_line();
        this->eof[0] = 0;
        size_t len = this->eof - this->cur;

        // Line format "fffff sssss eeeee hhhhh HHHHH"
        //                               ^  ^  ^  ^
        //                               |  |  |  |
        //                               |hash1|  |
        //                               |     |  |
        //                           space3    |hash2
        //                                     |
        //                                   space4
        //
        // filename(1 or >) + space(1) + start_sec(1 or >) + space(1) + stop_sec(1 or >) +
        //     space(1) + hash1(64) + space(1) + hash2(64) >= 135
        typedef std::reverse_iterator<char*> reverse_iterator;
        reverse_iterator first(cur);
        reverse_iterator last(cur + len);
        reverse_iterator space4 = std::find(last, first, ' ');
        space4++;
        reverse_iterator space3 = std::find(space4, first, ' ');
        space3++;
        reverse_iterator space2 = std::find(space3, first, ' ');
        space2++;
        reverse_iterator space1 = std::find(space2, first, ' ');
        space1++;
        int path_len = first-space1;
        this->in_copy_bytes(reinterpret_cast<uint8_t*>(meta_line.filename), path_len, cur, eof, ERR_TRANSPORT_READ_FAILED);
        this->cur += path_len + 1;

        // start_time + ' '
        meta_line.start_time = this->get_ll(cur, eof, ' ', ERR_TRANSPORT_READ_FAILED);
        // stop_time + ' '
        meta_line.stop_time = this->get_ll(cur, eof, ' ', ERR_TRANSPORT_READ_FAILED);
        // HASH1 + ' '
        this->in_hex256(meta_line.hash1, MD_HASH_LENGTH, cur, eof, ' ', ERR_TRANSPORT_READ_FAILED);
        // HASH1 + CR
        this->in_hex256(meta_line.hash2, MD_HASH_LENGTH, cur, eof, '\n', ERR_TRANSPORT_READ_FAILED);
    }

    int read_meta_file_v2(MetaHeaderXXX const & meta_header, MetaLine2 & meta_line)
    {
        printf("read_meta_file_v2\n");
        this->next_line();
        this->eof[0] = 0;
        size_t len = this->eof - this->cur;
        this->eof[len] = 0;

        printf("cur=%s\n", cur);

        // Line format "fffff
        // st_size st_mode st_uid st_gid st_dev st_ino st_mtime st_ctime
        // sssss eeeee hhhhh HHHHH"
        //            ^  ^  ^  ^
        //            |  |  |  |
        //            |hash1|  |
        //            |     |  |
        //        space11   |hash2
        //                  |
        //                space12
        //
        // filename(1 or >) + space(1) + stat_info(ll|ull * 8) +
        //     space(1) + start_sec(1 or >) + space(1) + stop_sec(1 or >) +
        //     space(1) + hash1(64) + space(1) + hash2(64) >= 135

        typedef std::reverse_iterator<char*> reverse_iterator;
        reverse_iterator first(cur);
        reverse_iterator last(cur + len);
        
        reverse_iterator space = last;
        reverse_iterator space12 = std::find(space, first, ' ');
        space12++;
        reverse_iterator space11 = std::find(space12, first, ' ');
        space11++;
        reverse_iterator space10 = std::find(space11, first, ' ');
        space10++;
        reverse_iterator space9 = std::find(space10, first, ' ');
        space9++;
        reverse_iterator space8 = std::find(space9, first, ' ');
        space8++;
        reverse_iterator space7 = std::find(space8, first, ' ');
        space7++;
        reverse_iterator space6 = std::find(space7, first, ' ');
        space6++;
        reverse_iterator space5 = std::find(space6, first, ' ');
        space5++;
        reverse_iterator space4 = std::find(space5, first, ' ');
        space4++;
        reverse_iterator space3 = std::find(space4, first, ' ');
        space3++;
        reverse_iterator space2 = std::find(space3, first, ' ');
        space2++;
        reverse_iterator space1 = std::find(space2, first, ' ');
        space1++;
        int path_len = first-space1;
        this->in_copy_bytes(reinterpret_cast<uint8_t*>(meta_line.filename), path_len, cur, eof, ERR_TRANSPORT_READ_FAILED);
        this->cur += path_len + 1;

        printf("cur=%s filename=%s\n", cur, meta_line.filename);

        // st_size + space
        meta_line.size = this->get_ll(cur, eof, ' ', ERR_TRANSPORT_READ_FAILED);
        // st_mode + space
        meta_line.mode = this->get_ll(cur, eof, ' ', ERR_TRANSPORT_READ_FAILED);
        // st_uid + space
        meta_line.uid = this->get_ll(cur, eof, ' ', ERR_TRANSPORT_READ_FAILED);
        // st_gid + space
        meta_line.gid = this->get_ll(cur, eof, ' ', ERR_TRANSPORT_READ_FAILED);
        // st_dev + space
        meta_line.dev = this->get_ll(cur, eof, ' ', ERR_TRANSPORT_READ_FAILED);
        // st_ino + space
        meta_line.ino = this->get_ll(cur, eof, ' ', ERR_TRANSPORT_READ_FAILED);
        // st_mtime + space
        meta_line.mtime = this->get_ll(cur, eof, ' ', ERR_TRANSPORT_READ_FAILED);
        // st_ctime + space
        meta_line.ctime = this->get_ll(cur, eof, ' ', ERR_TRANSPORT_READ_FAILED);
        // st_start_time + space
        meta_line.start_time = this->get_ll(cur, eof, ' ', ERR_TRANSPORT_READ_FAILED);
        // st_stop_time + space
        meta_line.stop_time = this->get_ll(cur, eof, ' ', ERR_TRANSPORT_READ_FAILED);

        // HASH1 + space
        this->in_hex256(meta_line.hash1, MD_HASH_LENGTH, cur, eof, ' ', ERR_TRANSPORT_READ_FAILED);
        // HASH1 + CR
        this->in_hex256(meta_line.hash2, MD_HASH_LENGTH, cur, eof, '\n', ERR_TRANSPORT_READ_FAILED);

        return 0;
    }
};


struct HashLoad
{
    MetaLine2 & hash_line;

    long long int get_ll(char * & cur, char * eof, char sep, int err)
    {
        char * pos = std::find(cur, eof, sep);
        if (pos == eof || (pos - cur < 2)){
            throw Error(err);
        }
        char * pend = nullptr;
        long long int res = strtoll(cur, &pend, 10);
        if (pend != pos){
            throw Error(err);
        }
        cur = pos + 1;
        return res;
    }

    void in_copy_bytes(uint8_t * hash, int len, char * & cur, char * eof, int err)
    {
        if (eof - cur < len){
            throw Error(err);
        }
        memcpy(hash, cur, len);
        cur += len;
    }

    void in_hex256(uint8_t * hash, int len, char * & cur, char * eof, char sep, int exc)
    {
        int err = 0;
        char * pos = std::find(cur, eof, sep);
        if (pos == eof || (pos - cur != 2*len)){
            throw Error(exc);
        }
        for (int i = 0 ; i < len ; i++){
            hash[i] = (chex_to_int(cur[i*2u], err)*16)
                     + chex_to_int(cur[i*2u+1], err);
        }
        if (err){
            throw Error(err);
        }
        cur = pos + 1;
    }


    HashLoad(const std::string & full_hash_path, const std::string & input_filename,
             unsigned int infile_version, bool infile_is_checksumed,
             MetaLine2 & hash_line,
            CryptoContext * cctx, bool infile_is_encrypted, int verbose)
        : hash_line(hash_line)
    {
        ifile_read_encrypted in_hash_fb(cctx, infile_is_encrypted);
        in_hash_fb.open(full_hash_path.c_str());

        char buffer[8192];
        memset(buffer, 0, sizeof(buffer));

        ssize_t len = in_hash_fb.read(buffer, sizeof(buffer));

        char * eof =  &buffer[len];
        char * cur = &buffer[0];

        if (infile_version == 1) {
            if (verbose) {
                LOG(LOG_INFO, "Hash data v1");
            }
            // Filename HASH_64_BYTES
            //         ^
            //         |
            //     separator

            int len = input_filename.length()+1;
            if (eof-cur < len){
                throw Error(ERR_TRANSPORT_READ_FAILED);
            }

            if (0 != memcmp(cur, input_filename.c_str(), input_filename.length()))
            {
                throw Error(ERR_TRANSPORT_READ_FAILED);
            }
            cur += input_filename.length();
            if (cur[0] != ' '){
                throw Error(ERR_TRANSPORT_READ_FAILED);
            }
            cur++;
            this->in_copy_bytes(this->hash_line.hash1, MD_HASH_LENGTH, cur, eof,
                                ERR_TRANSPORT_READ_FAILED);
            this->in_copy_bytes(this->hash_line.hash2, MD_HASH_LENGTH, cur, eof,
                                ERR_TRANSPORT_READ_FAILED);

        }
        else {
            if (verbose) {
                LOG(LOG_INFO, "Hash data v2 or higher");
            }

            // v2
            if (cur == eof || cur[0] != 'v'){
                Error(ERR_TRANSPORT_READ_FAILED, errno);
            }

            // skip 3 lines
            for (auto i = 0 ; i < 3 ; i++)
            {
                char * pos = std::find(cur, eof, '\n');
                if (pos == eof) {
                    throw Error(ERR_TRANSPORT_READ_FAILED);
                }
                cur = pos + 1;
            }

            // Line format "fffff
            // st_size st_mode st_uid st_gid st_dev st_ino st_mtime
            // st_ctime hhhhh HHHHH"
            //         ^  ^  ^  ^
            //         |  |  |  |
            //         |hash1|  |
            //         |     |  |
            //       space   |hash2
            //                  |
            //                space
            //
            // filename(1 or >) + space(1)
            // + stat_info(ll|ull * 8) + space(1)
            // + hash1(64) + space(1) + hash2(64) >= 135

            // filename(1 or >) followed by space
            {
                char * pos = std::find(cur, eof, ' ');
                if (pos == eof){
                    throw Error(ERR_TRANSPORT_READ_FAILED, errno);
                }
                if (size_t(pos-cur) != input_filename.length()
                || (0 != strncmp(cur, input_filename.c_str(), pos-cur)))
                {
                    std::cerr << "File name mismatch: \""
                              << input_filename
                              << "\"" << std::endl
                              << std::endl;
                    throw Error(ERR_TRANSPORT_READ_FAILED, errno);
                }
                memcpy(hash_line.filename, cur, pos - cur);
                hash_line.filename[pos-cur]=0;
                cur = pos + 1;
            }
            // st_size + space
            hash_line.size = this->get_ll(cur, eof, ' ', ERR_TRANSPORT_READ_FAILED);
            // st_mode + space
            hash_line.mode = this->get_ll(cur, eof, ' ', ERR_TRANSPORT_READ_FAILED);
            // st_uid + space
            hash_line.uid = this->get_ll(cur, eof, ' ', ERR_TRANSPORT_READ_FAILED);
            // st_gid + space
            hash_line.gid = this->get_ll(cur, eof, ' ', ERR_TRANSPORT_READ_FAILED);
            // st_dev + space
            hash_line.dev = this->get_ll(cur, eof, ' ', ERR_TRANSPORT_READ_FAILED);
            // st_ino + space
            hash_line.ino = this->get_ll(cur, eof, ' ', ERR_TRANSPORT_READ_FAILED);
            // st_mtime + space
            hash_line.mtime = this->get_ll(cur, eof, ' ', ERR_TRANSPORT_READ_FAILED);
            // st_ctime + space
            hash_line.ctime = this->get_ll(cur, eof, ' ', ERR_TRANSPORT_READ_FAILED);

            if (infile_is_checksumed){
                // HASH1 + space
                this->in_hex256(hash_line.hash1, MD_HASH_LENGTH, cur, eof, ' ', ERR_TRANSPORT_READ_FAILED);
                // HASH1 + CR
                this->in_hex256(hash_line.hash2, MD_HASH_LENGTH, cur, eof, '\n', ERR_TRANSPORT_READ_FAILED);
            }
        }
    }
};


static inline void load_ssl_digests(bool encryption)
{
    if (encryption){
        OpenSSL_add_all_digests();
    }
}

static inline int check_encrypted_or_checksumed(
                                       std::string const & input_filename,
                                       std::string const & mwrm_path,
                                       std::string const & hash_path,
                                       bool quick_check,
                                       uint32_t verbose,
                                       CryptoContext * cctx) {

    std::string const full_mwrm_filename = mwrm_path + input_filename;

    bool infile_is_encrypted = false;

    uint8_t tmp_buf[4] ={};
    int fd = open(full_mwrm_filename.c_str(), O_RDONLY);
    if (fd == -1){
        std::cerr << "Input file missing.\n";
        return 1;
    }
    struct fdbuf
    {
        int fd;
        explicit fdbuf(int fd) noexcept : fd(fd){}
        ~fdbuf() {::close(this->fd);}
    } file(fd);

    const size_t len = sizeof(tmp_buf);
    size_t remaining_len = len;
    while (remaining_len) {
        ssize_t ret = ::read(fd, &tmp_buf[len - remaining_len], remaining_len);
        if (ret < 0){
            if (errno == EINTR){
                continue;
            }
            // Error should still be there next time we try to read
            std::cerr << "Input file error\n";
            return 1;
        }
        // We must exit loop or we will enter infinite loop
        remaining_len -= ret;
    }

    const uint32_t magic = tmp_buf[0] + (tmp_buf[1] << 8) + (tmp_buf[2] << 16) + (tmp_buf[3] << 24);
    if (magic == WABCRYPTOFILE_MAGIC) {
        std::cout << "Input file is encrypted.\n";
        infile_is_encrypted = true;
    }

    unsigned infile_version = 1;
    bool infile_is_checksumed = false;

    load_ssl_digests(infile_is_encrypted);

    ifile_read_encrypted ibuf(cctx, infile_is_encrypted);
    if (ibuf.open(full_mwrm_filename.c_str()) < 0){
        throw Error(ERR_TRANSPORT_READ_FAILED, errno);
    }

    MwrmReaderXXX reader(ibuf);
    reader.read_meta_headers();

    infile_version       = reader.header.version;
    infile_is_checksumed = reader.header.has_checksum;

    // TODO: check compatibility of version and encryption
    if (infile_version < 2) {
        std::cout << "Input file is unencrypted.\n";
        return 0;
    }

    if (infile_is_checksumed == 0) {
        std::cout << "Input file don't include checksum\n";
        return 0;
    }

    /*****************
    * Load file hash *
    *****************/
    MetaLine2 hash_line = {{}, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, {}, {}};

    {
        std::string const full_hash_path = hash_path + input_filename;

        // if reading hash fails
        try {
            HashLoad meta(full_hash_path, input_filename, infile_version, infile_is_checksumed, hash_line, cctx, infile_is_encrypted, verbose);
        }
        catch (Error const & e) {
            std::cerr << "Exception code (hash): " << e.id << std::endl << std::endl;
            if (infile_is_checksumed){
                return 1;
            }
        }
        catch (...) {
            std::cerr << "Cannot read hash file: \"" << full_hash_path << "\"" << std::endl << std::endl;
            if (infile_is_checksumed){
                return 1;
            }
        }
    }

    printf("Check MWRM File!\n");

    /******************
    * Check mwrm file *
    ******************/
    bool result = false;

    if (file_size(full_mwrm_filename.c_str()) != hash_line.size)
    {
        std::cerr << "File \"" 
                  << full_mwrm_filename 
                  << "\" is invalid! (size mismatch)" 
                  << std::endl << std::endl;
        return 1;
    }

    if (infile_is_checksumed){
        if (quick_check){
            uint8_t hash[SHA256_DIGEST_LENGTH]={};
            if (file_start_hmac_sha256(full_mwrm_filename.c_str(), 
                                 cctx->get_hmac_key(), sizeof(cctx->get_hmac_key()),
                                 QUICK_CHECK_LENGTH, hash) < 0){
                std::cerr << "Error reading file \"" 
                          << full_mwrm_filename 
                          << "\"" 
                          << std::endl << std::endl;
                return 1;
            }
            if (0 != memcmp(hash, hash_line.hash1, SHA256_DIGEST_LENGTH)){
                std::cerr << "Error checking file \"" 
                          << full_mwrm_filename 
                          << "\" (invalid checksum)" 
                          << std::endl << std::endl;
                return 1;
            }

            ifile_read_encrypted ifile(cctx, infile_is_encrypted);
            if (ifile.open(full_mwrm_filename.c_str()) < 0) {
                LOG(LOG_ERR, "failed opening=%s", full_mwrm_filename.c_str());
                std::cerr << "Failed opening file " << full_mwrm_filename << std::endl;
                std::cerr << "File \"" << full_mwrm_filename << "\" is invalid!" << std::endl << std::endl;
                return 1;;
            }

            MwrmReader reader(ifile);

            auto meta_header = reader.read_meta_headers();

            MetaLine2 meta_line_wrm;

            result = true;
            while (reader.read_meta_file2(meta_header, meta_line_wrm) == 0) {

                size_t tmp_wrm_filename_len = 0;
                
                const char * tmp_wrm_filename = basename_len(meta_line_wrm.filename, tmp_wrm_filename_len);
                std::string const meta_line_wrm_filename = std::string(tmp_wrm_filename, tmp_wrm_filename_len);
                std::string const full_part_filename = mwrm_path + meta_line_wrm_filename;

                if (file_size(full_part_filename.c_str()) != meta_line_wrm.size)
                {
                    std::cerr << "File \"" 
                              << full_mwrm_filename 
                              << "\" is invalid! (part size mismatch for:" 
                              << full_part_filename
                              << ")" 
                              << std::endl << std::endl;
                    return 1;
                }

                uint8_t hash[SHA256_DIGEST_LENGTH]={};
                if (file_start_hmac_sha256(full_part_filename.c_str(), 
                                     cctx->get_hmac_key(), sizeof(cctx->get_hmac_key()),
                                     QUICK_CHECK_LENGTH, hash) < 0){
                    std::cerr << "Error reading part file \"" 
                              << full_part_filename 
                              << "\"" 
                              << std::endl << std::endl;
                    return 1;
                }
                if (0 != memcmp(hash, meta_line_wrm.hash1, SHA256_DIGEST_LENGTH)){
                    std::cerr << "Error checking part file \"" 
                              << full_part_filename 
                              << "\" (invalid checksum)" 
                              << std::endl << std::endl;
                    return 1;
                }
            }
        }
        else {
            uint8_t hash[SHA256_DIGEST_LENGTH]={};
            if (file_start_hmac_sha256(full_mwrm_filename.c_str(), 
                                 cctx->get_hmac_key(), sizeof(cctx->get_hmac_key()),
                                 0, hash) < 0){
                std::cerr << "Error reading file \"" 
                          << full_mwrm_filename 
                          << "\"" 
                          << std::endl << std::endl;
                return 1;
            }
            if (0 != memcmp(hash, hash_line.hash2, SHA256_DIGEST_LENGTH)){
                std::cerr << "Error checking file \"" 
                          << full_mwrm_filename 
                          << "\" (invalid checksum)" 
                          << std::endl << std::endl;
                return 1;
            }

            ifile_read_encrypted ifile(cctx, infile_is_encrypted);
            if (ifile.open(full_mwrm_filename.c_str()) < 0) {
                LOG(LOG_ERR, "failed opening=%s", full_mwrm_filename.c_str());
                std::cerr << "Failed opening file " << full_mwrm_filename << std::endl;
                std::cerr << "File \"" << full_mwrm_filename << "\" is invalid!" << std::endl << std::endl;
                return 1;;
            }

            MwrmReader reader(ifile);

            auto meta_header = reader.read_meta_headers();

            MetaLine2 meta_line_wrm;

            result = true;
            while (reader.read_meta_file2(meta_header, meta_line_wrm) == 0) {

                size_t tmp_wrm_filename_len = 0;
                const char * tmp_wrm_filename = basename_len(meta_line_wrm.filename, tmp_wrm_filename_len);
                std::string const meta_line_wrm_filename = std::string(tmp_wrm_filename, tmp_wrm_filename_len);
                std::string const full_part_filename = mwrm_path + meta_line_wrm_filename;

                if (file_size(full_part_filename.c_str()) !=  meta_line_wrm.size)
                {
                    std::cerr << "File \"" 
                              << full_mwrm_filename 
                              << "\" is invalid! (part size mismatch for:" 
                              << full_part_filename
                              << ")" 
                              << std::endl << std::endl;
                    return 1;
                }

                uint8_t hash[SHA256_DIGEST_LENGTH]={};
                if (file_start_hmac_sha256(full_part_filename.c_str(), 
                                     cctx->get_hmac_key(), sizeof(cctx->get_hmac_key()),
                                     0, hash) < 0){
                    std::cerr << "Error reading part file \"" 
                              << full_part_filename 
                              << "\"" 
                              << std::endl << std::endl;
                    return 1;
                }
                if (0 != memcmp(hash, meta_line_wrm.hash2, SHA256_DIGEST_LENGTH)){
                    std::cerr << "Error checking part file \"" 
                              << full_part_filename 
                              << "\" (invalid checksum)" 
                              << std::endl << std::endl;
                    return 1;
                }
            }
        }
    }
    else {
        struct stat64 sb;
        memset(&sb, 0, sizeof(sb));
        lstat64(full_mwrm_filename.c_str(), &sb);
        if ((hash_line.dev   != sb.st_dev  )
        ||  (hash_line.ino   != sb.st_ino  )
        ||  (hash_line.mode  != sb.st_mode )
        ||  (hash_line.uid   != sb.st_uid  )
        ||  (hash_line.gid   != sb.st_gid  )
        ||  (hash_line.size  != sb.st_size )
        ||  (hash_line.mtime != sb.st_mtime)
        ||  (hash_line.ctime != sb.st_ctime))
        {
            std::cerr << "File \"" 
                      << full_mwrm_filename 
                      << "\" is invalid! (metafile changed)" 
                      << std::endl << std::endl;
            return 1;
        }
        
        ifile_read_encrypted ifile(cctx, infile_is_encrypted);
        if (ifile.open(full_mwrm_filename.c_str()) < 0) {
            LOG(LOG_ERR, "failed opening=%s", full_mwrm_filename.c_str());
            std::cerr << "Failed opening file " << full_mwrm_filename << std::endl;
            std::cerr << "File \"" << full_mwrm_filename << "\" is invalid!" << std::endl << std::endl;
            return 1;;
        }

        MwrmReader reader(ifile);

        auto meta_header = reader.read_meta_headers();

        MetaLine2 meta_line_wrm;

        result = true;
        while (reader.read_meta_file2(meta_header, meta_line_wrm) == 0) {

            size_t tmp_wrm_filename_len = 0;
            
            const char * tmp_wrm_filename = basename_len(meta_line_wrm.filename, tmp_wrm_filename_len);
            std::string const meta_line_wrm_filename = std::string(tmp_wrm_filename, tmp_wrm_filename_len);
            std::string const full_part_filename = mwrm_path + meta_line_wrm_filename;

            struct stat64 sb;
            memset(&sb, 0, sizeof(sb));
            lstat64(full_part_filename.c_str(), &sb);
            if ((meta_line_wrm.dev   != sb.st_dev  )
            ||  (meta_line_wrm.ino   != sb.st_ino  )
            ||  (meta_line_wrm.mode  != sb.st_mode )
            ||  (meta_line_wrm.uid   != sb.st_uid  )
            ||  (meta_line_wrm.gid   != sb.st_gid  )
            ||  (meta_line_wrm.size  != sb.st_size )
            ||  (meta_line_wrm.mtime != sb.st_mtime)
            ||  (meta_line_wrm.ctime != sb.st_ctime))
            {
                std::cerr << "Error checking part file \"" 
                          << full_part_filename 
                          << "\" (metadata changed)" 
                          << std::endl << std::endl;
                return 1;
            }
        }
    }

    if (!result){
        std::cerr << "File \"" << full_mwrm_filename << "\" is invalid!" << std::endl << std::endl;
        return 1;
    }

    std::cout << "No error detected during the data verification." << std::endl << std::endl;
    return 0;
}

static inline int app_verifier(Inifile & ini, int argc, char const * const * argv, const char * copyright_notice, CryptoContext & cctx)
{
    openlog("verifier", LOG_CONS | LOG_PERROR, LOG_USER);

    std::string hash_path      = ini.get<cfg::video::hash_path>().c_str()  ;
    std::string mwrm_path      = ini.get<cfg::video::record_path>().c_str();
    std::string input_filename                                ;
    bool        quick_check    = false                        ;
    uint32_t    verbose        = 0                            ;

    program_options::options_description desc({
        {'h', "help",    "produce help message"},
        {'v', "version", "show software version"},
        {'q', "quick",   "quick check only"},
        {'s', "hash-path",  &hash_path,         "hash file path"       },
        {'m', "mwrm-path",  &mwrm_path,         "mwrm file path"       },
        {'i', "input-file", &input_filename,    "input mwrm file name" },
        {"verbose",         &verbose,           "more logs"            },
    })
    ;

    auto options = program_options::parse_command_line(argc, argv, desc);

    if (options.count("help") > 0) {
        std::cout << copyright_notice;
        std::cout << "Usage: redver [options]\n\n";
        std::cout << desc << std::endl;
        return 0;
    }

    if (options.count("version") > 0) {
        std::cout << copyright_notice;
        return 0;
    }

    if (options.count("quick") > 0) {
        quick_check = true;
    }

    if (hash_path.c_str()[0] == 0) {
        std::cerr << "Missing hash-path : use -h path\n\n";
        return 1;
    }

    if (mwrm_path.c_str()[0] == 0) {
        std::cerr << "Missing mwrm-path : use -m path\n\n";
        return 1;
    }

    if (input_filename.c_str()[0] == 0) {
        std::cerr << "Missing input mwrm file name : use -i filename\n\n";
        return 1;
    }

    {
        char temp_path[1024]     = {};
        char temp_basename[1024] = {};
        char temp_extension[256] = {};

        canonical_path(input_filename.c_str(), temp_path, sizeof(temp_path), temp_basename, sizeof(temp_basename), temp_extension, sizeof(temp_extension), verbose);

        if (strlen(temp_path) > 0) {
            mwrm_path       = temp_path;
            input_filename  = temp_basename;
            input_filename += temp_extension;
        }
        if (mwrm_path.back() != '/'){
            mwrm_path.push_back('/');
        }
        if (hash_path.back() != '/'){
            hash_path.push_back('/');
        }

    }
    std::cout << "Input file is \"" << mwrm_path << input_filename << "\".\n";

    if (verbose) {
        LOG(LOG_INFO, "hash_path=\"%s\"", hash_path.c_str());
        LOG(LOG_INFO, "mwrm_path=\"%s\"", mwrm_path.c_str());
        LOG(LOG_INFO, "file_name=\"%s\"", input_filename.c_str());
    }

    return check_encrypted_or_checksumed(
                input_filename, mwrm_path, hash_path,
                quick_check, verbose, &cctx);
}
