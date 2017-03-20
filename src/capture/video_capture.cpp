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
   Author(s): Christophe Grosjean, Jonatan Poelen
*/

#include "capture/video_capture.hpp"

#include "utils/log.hpp"

#include "utils/difftimeval.hpp"

#include "gdi/capture_api.hpp"

#include "core/RDP/RDPDrawable.hpp"

#include "capture/video_recorder.hpp"
#include "capture/flv_params.hpp"
#include "transport/file_transport.hpp"
#include "transport/transport.hpp"
#include "utils/bitmap_shrink.hpp"

#include <cerrno>
#include <cstring>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>



FullVideoCaptureImpl::TmpFileTransport::TmpFileTransport(
    const char * const prefix,
    const char * const filename,
    const char * const extension,
    const int groupid)
: fd(-1)
, groupid(groupid)
{
    this->final_filename[0] = 0;
    // TODO: check that filename_gen is not too large of throw some exception
    std::snprintf(
        this->final_filename,
        sizeof(this->final_filename),
        "%s%s%s", prefix, filename, extension);
    this->tmp_filename[0] = 0;
}

void FullVideoCaptureImpl::TmpFileTransport::seek(int64_t offset, int whence)
{
    if (static_cast<off64_t>(-1) == lseek64(this->fd, offset, whence)){
        throw Error(ERR_TRANSPORT_SEEK_FAILED, errno);
    }
}

FullVideoCaptureImpl::TmpFileTransport::~TmpFileTransport()
{
    if (this->fd != -1) {
        ::close(this->fd);
        // LOG(LOG_INFO, "\"%s\" -> \"%s\".", this->current_filename, this->rename_to);
        if (::rename(this->tmp_filename, this->final_filename) < 0) {
            LOG( LOG_ERR, "renaming file \"%s\" -> \"%s\" failed erro=%u : %s\n"
               , this->tmp_filename, this->final_filename, errno, strerror(errno));
        }
    }
}

void FullVideoCaptureImpl::TmpFileTransport::do_send(const uint8_t * data, size_t len)
{
    if (this->fd == -1) {
        std::snprintf(
            this->tmp_filename, sizeof(this->tmp_filename),
            "%sred-XXXXXX.tmp", this->final_filename);
        this->fd = ::mkostemps(this->tmp_filename, 4, O_WRONLY | O_CREAT);
        if (this->fd == -1) {
            this->status = false;
            auto eid = (errno == ENOSPC) ? ERR_TRANSPORT_WRITE_NO_ROOM : ERR_TRANSPORT_WRITE_FAILED;
            throw Error(eid, errno);
        }
        if (chmod(this->tmp_filename, this->groupid ?(S_IRUSR|S_IRGRP):S_IRUSR) == -1) {
            LOG( LOG_ERR, "can't set file %s mod to %s : %s [%u]"
                , this->tmp_filename
                , this->groupid ? "u+r, g+r" : "u+r"
                , strerror(errno), errno);
            // TODO: throw error if chmod fails
            // TODO: see if we can provide chmod in mkostemp
        }
    }

    size_t remaining_len = len;
    size_t total_sent = 0;
    while (remaining_len) {
        ssize_t ret = ::write(this->fd, data + total_sent, remaining_len);
        if (ret <= 0) {
            if (errno == EINTR){ continue; }
            this->status = false;
            auto eid = (errno == ENOSPC) ? ERR_TRANSPORT_WRITE_NO_ROOM : ERR_TRANSPORT_WRITE_FAILED;
            throw Error(eid, errno);
        }
        remaining_len -= ret;
        total_sent += ret;
    }
    this->last_quantum_sent += total_sent;
}


struct IOVideoRecorderWithTransport
{
    IOVideoRecorderWithTransport(Transport & trans)
    : trans(trans)
    {}

    video_recorder::write_packet_fn_t write_fn() const { return &write_packet; }
    video_recorder::seek_fn_t seek_fn() const { return &seek; }
    void * params() const { return &this->trans; }

private:
    Transport& trans;

    static int write_packet(void *opaque, uint8_t *buf, int buf_size)
    {
        Transport * trans       = reinterpret_cast<Transport *>(opaque);
        int         return_code = buf_size;
        try {
            trans->send(buf, buf_size);
        }
        catch (Error const & e) {
            if (e.id == ERR_TRANSPORT_WRITE_NO_ROOM) {
                LOG(LOG_ERR, "Video write_packet failure, no space left on device (id=%d)", e.id);
            }
            else {
                LOG(LOG_ERR, "Video write_packet failure (id=%d, errnum=%d)", e.id, e.errnum);
            }
            return_code = -1;
        }
        return return_code;
    }

    static int64_t seek(void *opaque, int64_t offset, int whence)
    {
        // This function is like the fseek() C stdio function.
        // "whence" can be either one of the standard C values
        // (SEEK_SET, SEEK_CUR, SEEK_END) or one more value: AVSEEK_SIZE.

        // When "whence" has this value, your seek function must
        // not seek but return the size of your file handle in bytes.
        // This is also optional and if you don't implement it you must return <0.

        // Otherwise you must return the current position of your stream
        //  in bytes (that is, after the seeking is performed).
        // If the seek has failed you must return <0.
        if (whence == AVSEEK_SIZE) {
            LOG(LOG_ERR, "Video seek failure");
            return -1;
        }
        try {
            Transport *trans = reinterpret_cast<Transport *>(opaque);
            trans->seek(offset, whence);
            return offset;
        }
        catch (Error const & e){
            LOG(LOG_ERR, "Video seek failure (id=%d)", e.id);
            return -1;
        };
    }
};


FullVideoCaptureImpl::FullVideoCaptureImpl(
    const timeval & now, const char * const record_path, const char * const basename,
    const int groupid, bool no_timestamp, RDPDrawable & drawable, FlvParams flv_params)
: trans_tmp_file(record_path, basename, ("." + flv_params.codec).c_str(), groupid)
, drawable(drawable)
, flv_params(std::move(flv_params))
, start_video_capture(now)
, inter_frame_interval(1000000L / this->flv_params.frame_rate)
, no_timestamp(no_timestamp)
{
    if (this->flv_params.verbosity) {
        LOG(LOG_INFO, "Video recording %d x %d, rate: %d, qscale: %d, brate: %d, codec: %s",
            this->flv_params.target_width, this->flv_params.target_height,
            this->flv_params.frame_rate, this->flv_params.qscale, this->flv_params.bitrate,
            this->flv_params.codec.c_str());
    }

    if (this->recorder) {
        this->recorder.reset();
        this->trans_tmp_file.next();
    }

    IOVideoRecorderWithTransport io{this->trans_tmp_file};
    this->recorder.reset(new video_recorder(
        io.write_fn(), io.seek_fn(), io.params(),
        drawable.width(), drawable.height(),
        drawable.pix_len(),
        drawable.data(),
        this->flv_params.bitrate,
        this->flv_params.frame_rate,
        this->flv_params.qscale,
        this->flv_params.codec.c_str(),
        this->flv_params.target_width,
        this->flv_params.target_height,
        this->flv_params.verbosity
    ));

    ::unlink((std::string(record_path) + basename + "." + flv_params.codec).c_str());
}

FullVideoCaptureImpl::~FullVideoCaptureImpl() = default;


void FullVideoCaptureImpl::frame_marker_event(
    const timeval& /*now*/, int /*cursor_x*/, int /*cursor_y*/, bool /*ignore_frame_in_timeval*/)
{
    this->drawable.trace_mouse();
    if (!this->no_timestamp) {
        time_t rawtime = this->start_video_capture.tv_sec;
        tm tm_result;
        localtime_r(&rawtime, &tm_result);
        this->drawable.trace_timestamp(tm_result);
    }
    this->recorder->preparing_video_frame(true);
    if (!this->no_timestamp) { this->drawable.clear_timestamp(); }
    this->drawable.clear_mouse();
}

std::chrono::microseconds FullVideoCaptureImpl::do_snapshot(
    const timeval& now, int /*cursor_x*/, int /*cursor_y*/, bool ignore_frame_in_timeval)
{
    uint64_t tick = difftimeval(now, this->start_video_capture);
    uint64_t const inter_frame_interval = this->inter_frame_interval.count();
    if (tick >= inter_frame_interval) {
        auto encoding_video_frame = [this](time_t rawtime){
            this->drawable.trace_mouse();
            if (!this->no_timestamp) {
                tm tm_result;
                localtime_r(&rawtime, &tm_result);
                this->drawable.trace_timestamp(tm_result);
                this->recorder->encoding_video_frame();
                this->drawable.clear_timestamp();
            }
            else {
                this->recorder->encoding_video_frame();
            }
            this->drawable.clear_mouse();
        };

        if (ignore_frame_in_timeval) {
            auto const nframe = tick / inter_frame_interval;
            encoding_video_frame(this->start_video_capture.tv_sec);
            auto const usec = inter_frame_interval * nframe;
            auto sec = usec / 1000000LL;
            this->start_video_capture.tv_usec += usec - sec * inter_frame_interval;
            if (this->start_video_capture.tv_usec >= 1000000LL){
                this->start_video_capture.tv_usec -= 1000000LL;
                ++sec;
            }
            this->start_video_capture.tv_sec += sec;
            tick -= inter_frame_interval * nframe;
        }
        else {
            do {
                encoding_video_frame(this->start_video_capture.tv_sec);
                this->start_video_capture.tv_usec += inter_frame_interval;
                if (this->start_video_capture.tv_usec >= 1000000LL){
                    this->start_video_capture.tv_sec += 1;
                    this->start_video_capture.tv_usec -= 1000000LL;
                }
                tick -= inter_frame_interval;
            } while (tick >= inter_frame_interval);
        }
    }
    return std::chrono::microseconds(inter_frame_interval - tick);
}

std::chrono::microseconds FullVideoCaptureImpl::periodic_snapshot(
    const timeval& now, int cursor_x, int cursor_y, bool ignore_frame_in_timeval)
{
    auto next_duration = this->do_snapshot(now, cursor_x, cursor_y, ignore_frame_in_timeval);
    assert(next_duration.count() >= 0);
    return next_duration;
}

void FullVideoCaptureImpl::encoding_video_frame()
{
    this->recorder->encoding_video_frame();
}


void SequencedVideoCaptureImpl::SequenceTransport
::FileGen::set_final_filename(char * final_filename, size_t final_filename_size)
{
    std::snprintf(
        final_filename, final_filename_size, "%s%s-%06u%s",
        this->path, this->base, this->num, this->ext);
}

SequencedVideoCaptureImpl::SequenceTransport::SequenceTransport(
    const char * const prefix,
    const char * const filename,
    const char * const extension,
    const int groupid,
    auth_api * authentifier)
: fd(-1)
, groupid(groupid)
{
    if (strlen(prefix) > sizeof(this->filegen.path) - 1
     || strlen(filename) > sizeof(this->filegen.base) - 1
     || strlen(extension) > sizeof(this->filegen.ext) - 1) {
        throw Error(ERR_TRANSPORT);
    }

    strcpy(this->filegen.path, prefix);
    strcpy(this->filegen.base, filename);
    strcpy(this->filegen.ext, extension);

    this->final_filename[0] = 0;
    this->tmp_filename[0] = 0;

    if (authentifier) {
        this->set_authentifier(authentifier);
    }
}

void SequencedVideoCaptureImpl::SequenceTransport::seek(int64_t offset, int whence)
{
    if (static_cast<off64_t>(-1) == lseek64(this->fd, offset, whence)){
        throw Error(ERR_TRANSPORT_SEEK_FAILED, errno);
    }
}

bool SequencedVideoCaptureImpl::SequenceTransport::next()
{
    if (this->status == false) {
        throw Error(ERR_TRANSPORT_NO_MORE_DATA);
    }
    ::close(this->fd);
    this->fd = -1;
    // LOG(LOG_INFO, "\"%s\" -> \"%s\".", this->current_filename, this->rename_to);

    this->filegen.set_final_filename(this->final_filename, sizeof(this->final_filename));
    if (::rename(this->tmp_filename, this->final_filename) < 0)
    {
        LOG( LOG_ERR, "renaming file \"%s\" -> \"%s\" failed erro=%u : %s\n"
            , this->tmp_filename, this->final_filename, errno, strerror(errno));
        this->status = false;
        LOG(LOG_ERR, "Write to transport failed (M): code=%d", errno);
        throw Error(ERR_TRANSPORT_WRITE_FAILED, errno);
    }
    this->tmp_filename[0] = 0;

    ++this->filegen.num;
    ++this->seqno;
    return true;
}

SequencedVideoCaptureImpl::SequenceTransport::~SequenceTransport()
{
    if (this->fd != -1) {
        ::close(this->fd);
        // LOG(LOG_INFO, "\"%s\" -> \"%s\".", this->current_filename, this->rename_to);

        this->filegen.set_final_filename(this->final_filename, sizeof(this->final_filename));
        const int res = ::rename(this->tmp_filename, this->final_filename);
        if (res < 0) {
            LOG( LOG_ERR, "renaming file \"%s\" -> \"%s\" failed erro=%u : %s\n"
                , this->tmp_filename, this->final_filename, errno, strerror(errno));
        }
    }
}

void SequencedVideoCaptureImpl::SequenceTransport::do_send(const uint8_t * data, size_t len)
{
    if (this->fd == -1) {

        this->filegen.set_final_filename(this->final_filename, sizeof(this->final_filename));
        std::snprintf(
            this->tmp_filename, sizeof(this->tmp_filename),
            "%sred-XXXXXX.tmp", this->final_filename);
        this->fd = ::mkostemps(this->tmp_filename, 4, O_WRONLY | O_CREAT);
        if (this->fd == -1) {
            this->status = false;
            auto id = ERR_TRANSPORT_WRITE_FAILED;
            if (errno == ENOSPC) {
                char message[1024];
                std::snprintf(message, sizeof(message), "100|unknown");
                this->authentifier->report("FILESYSTEM_FULL", message);
                id = ERR_TRANSPORT_WRITE_NO_ROOM;
                errno = ENOSPC;
            }
            throw Error(id, errno);
        }
        if (chmod(this->tmp_filename, this->groupid ?
            (S_IRUSR | S_IRGRP) : S_IRUSR) == -1) {
            LOG( LOG_ERR, "can't set file %s mod to %s : %s [%u]"
                , this->tmp_filename
                , this->groupid ? "u+r, g+r" : "u+r"
                , strerror(errno), errno);
        }
    }

    size_t remaining_len = len;
    size_t total_sent = 0;
    while (remaining_len) {
        ssize_t ret = ::write(this->fd, data + total_sent, remaining_len);
        if (ret <= 0){
            if (errno == EINTR){
                continue;
            }
            this->status = false;
            auto id = ERR_TRANSPORT_WRITE_FAILED;
            if (errno == ENOSPC) {
                char message[1024];
                std::snprintf(message, sizeof(message), "100|unknown");
                this->authentifier->report("FILESYSTEM_FULL", message);
                id = ERR_TRANSPORT_WRITE_NO_ROOM;
                errno = ENOSPC;
            }
            throw Error(id, errno);
        }
        remaining_len -= ret;
        total_sent += ret;
    }
    this->last_quantum_sent += total_sent;
}


std::chrono::microseconds  SequencedVideoCaptureImpl::do_snapshot(
    timeval const & now, int cursor_x, int cursor_y, bool ignore_frame_in_timeval)
{
    this->vc.do_snapshot(now, cursor_x, cursor_y, ignore_frame_in_timeval);
    if (!this->ic_has_first_img) {
        return this->first_image.do_snapshot(now, cursor_x, cursor_y, ignore_frame_in_timeval);
    }
    return this->video_sequencer.do_snapshot(now, cursor_x, cursor_y, ignore_frame_in_timeval);
}

void  SequencedVideoCaptureImpl::frame_marker_event(
    timeval const & now, int cursor_x, int cursor_y, bool ignore_frame_in_timeval)
{
    this->vc.frame_marker_event(now, cursor_x, cursor_y, ignore_frame_in_timeval);
    if (!this->ic_has_first_img) {
        this->first_image.frame_marker_event(now, cursor_x, cursor_y, ignore_frame_in_timeval);
    }
    else {
        this->video_sequencer.frame_marker_event(now, cursor_x, cursor_y, ignore_frame_in_timeval);
    }
}

std::chrono::microseconds SequencedVideoCaptureImpl::periodic_snapshot(
    timeval const & now, int cursor_x, int cursor_y, bool ignore_frame_in_timeval)
{
    this->vc.periodic_snapshot(now, cursor_x, cursor_y, ignore_frame_in_timeval);
    if (!this->ic_has_first_img) {
        return this->first_image.periodic_snapshot(now, cursor_x, cursor_y, ignore_frame_in_timeval);
    }
    return this->video_sequencer.periodic_snapshot(now, cursor_x, cursor_y, ignore_frame_in_timeval);
}

std::chrono::microseconds SequencedVideoCaptureImpl::FirstImage::periodic_snapshot(
    timeval const & now, int cursor_x, int cursor_y, bool ignore_frame_in_timeval)
{
    // assert(now >= previous);
    auto next_duration = this->do_snapshot(now, cursor_x, cursor_y, ignore_frame_in_timeval);
    assert(next_duration.count() >= 0);
    return next_duration;
}

void SequencedVideoCaptureImpl::FirstImage::frame_marker_event(
    timeval const & now, int cursor_x, int cursor_y, bool ignore_frame_in_timeval)
{
    this->periodic_snapshot(now, cursor_x, cursor_y, ignore_frame_in_timeval);
}

std::chrono::microseconds SequencedVideoCaptureImpl::FirstImage::do_snapshot(
    const timeval& now, int x, int y, bool ignore_frame_in_timeval)
{
    std::chrono::microseconds ret;

    auto const duration = std::chrono::microseconds(difftimeval(now, this->first_image_start_capture));
    auto const interval = std::chrono::microseconds(std::chrono::seconds(3))/2;
    if (duration >= interval) {
        auto video_interval = first_image_impl.video_sequencer.get_interval();
        if (this->first_image_impl.ic_drawable.logical_frame_ended() || duration > std::chrono::seconds(2) || duration >= video_interval) {
            tm ptm;
            localtime_r(&now.tv_sec, &ptm);
            this->first_image_impl.ic_drawable.trace_timestamp(ptm);
            this->first_image_impl.ic_flush();
            this->first_image_impl.ic_drawable.clear_timestamp();
            this->first_image_impl.ic_has_first_img = true;
            this->first_image_impl.ic_trans.next();
            ret = video_interval;
        }
        else {
            ret = interval / 3;
        }
    }
    else {
        ret = interval - duration;
    }

    return std::min(ret, this->first_image_impl.video_sequencer.periodic_snapshot(now, x, y, ignore_frame_in_timeval));
}


SequencedVideoCaptureImpl::VideoCapture::VideoCapture(
    const timeval & now,
    Transport & trans,
    RDPDrawable & drawable,
    bool no_timestamp,
    FlvParams flv_params)
: trans(trans)
, flv_params(std::move(flv_params))
, drawable(drawable)
, start_video_capture(now)
, inter_frame_interval(1000000L / this->flv_params.frame_rate)
, no_timestamp(no_timestamp)
{
    if (this->flv_params.verbosity) {
        LOG(LOG_INFO, "Video recording %d x %d, rate: %d, qscale: %d, brate: %d, codec: %s",
            this->flv_params.target_width, this->flv_params.target_height,
            this->flv_params.frame_rate, this->flv_params.qscale, this->flv_params.bitrate,
            this->flv_params.codec.c_str());
    }

    this->next_video();
}

SequencedVideoCaptureImpl::VideoCapture::~VideoCapture() = default;

void SequencedVideoCaptureImpl::VideoCapture::next_video()
{
    if (this->recorder) {
        this->recorder.reset();
        this->trans.next();
    }

    IOVideoRecorderWithTransport io{this->trans};
    this->recorder.reset(new video_recorder(
        io.write_fn(), io.seek_fn(), io.params(),
        drawable.width(), drawable.height(),
        drawable.pix_len(),
        drawable.data(),
        this->flv_params.bitrate,
        this->flv_params.frame_rate,
        this->flv_params.qscale,
        this->flv_params.codec.c_str(),
        this->flv_params.target_width,
        this->flv_params.target_height,
        this->flv_params.verbosity
    ));
}

void SequencedVideoCaptureImpl::VideoCapture::encoding_video_frame()
{
    this->recorder->encoding_video_frame();
}

void SequencedVideoCaptureImpl::VideoCapture::frame_marker_event(
    const timeval& /*now*/, int /*cursor_x*/, int /*cursor_y*/, bool /*ignore_frame_in_timeval*/)
{
    this->drawable.trace_mouse();
    if (!this->no_timestamp) {
        time_t rawtime = this->start_video_capture.tv_sec;
        tm tm_result;
        localtime_r(&rawtime, &tm_result);
        this->drawable.trace_timestamp(tm_result);
    }
    this->recorder->preparing_video_frame(true);
    if (!this->no_timestamp) { this->drawable.clear_timestamp(); }
    this->drawable.clear_mouse();
}

std::chrono::microseconds SequencedVideoCaptureImpl::VideoCapture::do_snapshot(
    const timeval& now, int /*cursor_x*/, int /*cursor_y*/, bool ignore_frame_in_timeval)
{
    uint64_t tick = difftimeval(now, this->start_video_capture);
    uint64_t const inter_frame_interval = this->inter_frame_interval.count();
    if (tick >= inter_frame_interval) {
        auto encoding_video_frame = [this](time_t rawtime){
            this->drawable.trace_mouse();
            if (!this->no_timestamp) {
                tm tm_result;
                localtime_r(&rawtime, &tm_result);
                this->drawable.trace_timestamp(tm_result);
                this->recorder->encoding_video_frame();
                this->drawable.clear_timestamp();
            }
            else {
                this->recorder->encoding_video_frame();
            }
            this->drawable.clear_mouse();
        };

        if (ignore_frame_in_timeval) {
            auto const nframe = tick / inter_frame_interval;
            encoding_video_frame(this->start_video_capture.tv_sec);
            auto const usec = inter_frame_interval * nframe;
            auto sec = usec / 1000000LL;
            this->start_video_capture.tv_usec += usec - sec * inter_frame_interval;
            if (this->start_video_capture.tv_usec >= 1000000LL){
                this->start_video_capture.tv_usec -= 1000000LL;
                ++sec;
            }
            this->start_video_capture.tv_sec += sec;
            tick -= inter_frame_interval * nframe;
        }
        else {
            do {
                encoding_video_frame(this->start_video_capture.tv_sec);
                this->start_video_capture.tv_usec += inter_frame_interval;
                if (this->start_video_capture.tv_usec >= 1000000LL){
                    this->start_video_capture.tv_sec += 1;
                    this->start_video_capture.tv_usec -= 1000000LL;
                }
                tick -= inter_frame_interval;
            } while (tick >= inter_frame_interval);
        }
    }

    return std::chrono::microseconds(inter_frame_interval - tick);
}

std::chrono::microseconds SequencedVideoCaptureImpl::VideoCapture::periodic_snapshot(
    timeval const & now, int cursor_x, int cursor_y, bool ignore_frame_in_timeval)
{
    // assert(now >= previous);
    auto next_duration = this->do_snapshot(now, cursor_x, cursor_y, ignore_frame_in_timeval);
    assert(next_duration.count() >= 0);
    return next_duration;
}

void SequencedVideoCaptureImpl::zoom(unsigned percent)
{
    percent = std::min(percent, 100u);
    const unsigned zoom_width = (this->ic_drawable.width() * percent) / 100;
    const unsigned zoom_height = (this->ic_drawable.height() * percent) / 100;
    this->ic_zoom_factor = percent;
    this->ic_scaled_width = (zoom_width + 3) & 0xFFC;
    this->ic_scaled_height = zoom_height;
    if (this->ic_zoom_factor != 100) {
        this->ic_scaled_buffer.reset(new uint8_t[this->ic_scaled_width * this->ic_scaled_height * 3]);
    }
}

void SequencedVideoCaptureImpl::ic_flush()
{
    if (this->ic_zoom_factor == 100) {
        this->dump24();
    }
    else {
        this->scale_dump24();
    }
}

void SequencedVideoCaptureImpl::dump24()
{
    ::transport_dump_png24(
        this->ic_trans, this->ic_drawable.data(),
        this->ic_drawable.width(), this->ic_drawable.height(),
        this->ic_drawable.rowsize(), true);
}

void SequencedVideoCaptureImpl::scale_dump24()
{
    scale_data(
        this->ic_scaled_buffer.get(), this->ic_drawable.data(),
        this->ic_scaled_width, this->ic_drawable.width(),
        this->ic_scaled_height, this->ic_drawable.height(),
        this->ic_drawable.rowsize());
    ::transport_dump_png24(
        this->ic_trans, this->ic_scaled_buffer.get(),
        this->ic_scaled_width, this->ic_scaled_height,
        this->ic_scaled_width * 3, false);
}


void SequencedVideoCaptureImpl::VideoSequencer::reset_now(const timeval& now)
{
    this->start_break = now;
}

std::chrono::microseconds SequencedVideoCaptureImpl::VideoSequencer::do_snapshot(
    const timeval& now, int /*cursor_x*/, int /*cursor_y*/, bool /*ignore_frame_in_timeval*/)
{
    assert(this->break_interval.count());
    auto const interval = difftimeval(now, this->start_break);
    if (interval >= uint64_t(this->break_interval.count())) {
        this->impl.next_video_impl(now, NotifyNextVideo::reason::sequenced);
        this->start_break = now;
    }
    return this->break_interval;
}

std::chrono::microseconds SequencedVideoCaptureImpl::VideoSequencer::periodic_snapshot(
    timeval const & now, int cursor_x, int cursor_y, bool ignore_frame_in_timeval)
{
    // assert(now >= previous);
    auto next_duration = this->do_snapshot(now, cursor_x, cursor_y, ignore_frame_in_timeval);
    assert(next_duration.count() >= 0);
    return next_duration;
}

void SequencedVideoCaptureImpl::VideoSequencer::frame_marker_event(
    timeval const & now, int cursor_x, int cursor_y, bool ignore_frame_in_timeval)
{
    this->periodic_snapshot(now, cursor_x, cursor_y, ignore_frame_in_timeval);
}


SequencedVideoCaptureImpl::SequencedVideoCaptureImpl(
    const timeval & now,
    const char * const record_path,
    const char * const basename,
    const int groupid,
    bool no_timestamp,
    unsigned image_zoom,
    /* const */RDPDrawable & drawable,
    FlvParams flv_params,
    std::chrono::microseconds video_interval,
    NotifyNextVideo & next_video_notifier)
: first_image(now, *this)
, vc_trans(record_path, basename, ("." + flv_params.codec).c_str(), groupid, nullptr)
, vc(now, this->vc_trans, drawable, no_timestamp, std::move(flv_params))
, ic_trans(record_path, basename, ".png", groupid, nullptr)
, ic_zoom_factor(std::min(image_zoom, 100u))
, ic_scaled_width(drawable.width())
, ic_scaled_height(drawable.height())
, ic_drawable(drawable)
, video_sequencer(
    now,
    (video_interval > std::chrono::microseconds(0)) ? video_interval : std::chrono::microseconds::max(),
    *this)
, next_video_notifier(next_video_notifier)
{
    const unsigned zoom_width = (this->ic_drawable.width() * this->ic_zoom_factor) / 100;
    const unsigned zoom_height = (this->ic_drawable.height() * this->ic_zoom_factor) / 100;
    this->ic_scaled_width = (zoom_width + 3) & 0xFFC;
    this->ic_scaled_height = zoom_height;
    if (this->ic_zoom_factor != 100) {
        this->ic_scaled_buffer.reset(new uint8_t[this->ic_scaled_width * this->ic_scaled_height * 3]);
    }
}

void SequencedVideoCaptureImpl::next_video_impl(const timeval& now, NotifyNextVideo::reason reason) {
    this->video_sequencer.reset_now(now);
    if (!this->ic_has_first_img) {
        tm ptm;
        localtime_r(&now.tv_sec, &ptm);
        this->ic_drawable.trace_timestamp(ptm);
        this->ic_flush();
        this->ic_drawable.clear_timestamp();
        this->ic_has_first_img = true;
        this->ic_trans.next();
    }
    this->vc.next_video();
    tm ptm;
    localtime_r(&now.tv_sec, &ptm);
    this->ic_drawable.trace_timestamp(ptm);
    this->ic_flush();
    this->ic_drawable.clear_timestamp();
    this->ic_trans.next();
    this->next_video_notifier.notify_next_video(now, reason);
}

void SequencedVideoCaptureImpl::next_video(const timeval& now)
{
    this->next_video_impl(now, NotifyNextVideo::reason::external);
}

void SequencedVideoCaptureImpl::encoding_video_frame()
{
    this->vc.encoding_video_frame();
}