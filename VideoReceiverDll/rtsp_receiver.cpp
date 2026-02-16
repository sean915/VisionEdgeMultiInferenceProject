#include "pch.h"
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <deque>
#include <condition_variable>
#include <cstring>
#include <limits>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libavutil/hwcontext.h>
#include <rtsp_receiver.h>
}

static void log_status(RtspStatusCallback cb, void* user, int ch, RtspLogLevel lvl, int code, const std::string& msg) {
    if (cb) cb(user, ch, lvl, code, msg.c_str());
}

void RTSP_CALL rtsp_default_options(RtspOptions* opt) {
    if (!opt) return;
    opt->tcp_only = 1;
    opt->stimeout_us = 5'000'000;
    opt->reconnect = 1;
    opt->reconnect_wait_ms = 1000;
    opt->target_fps = 0;
}

// ============================
// MP4 remux helper (global)
// ============================
static bool write_mp4_impl(
    const char* path,
    std::vector<AVPacket*>& pkts,
    AVCodecParameters* in_par,
    AVRational in_tb)
{
    if (!path || !*path || pkts.empty() || !in_par) return false;

    AVFormatContext* oc = nullptr;
    int ret = avformat_alloc_output_context2(&oc, nullptr, nullptr, path);
    if (ret < 0 || !oc) return false;

    AVStream* st = avformat_new_stream(oc, nullptr);
    if (!st) { avformat_free_context(oc); return false; }

    avcodec_parameters_copy(st->codecpar, in_par);
    st->codecpar->codec_tag = 0;
    st->time_base = in_tb;

    if (!(oc->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&oc->pb, path, AVIO_FLAG_WRITE);
        if (ret < 0) { avformat_free_context(oc); return false; }
    }

    ret = avformat_write_header(oc, nullptr);
    if (ret < 0) {
        if (oc->pb) avio_closep(&oc->pb);
        avformat_free_context(oc);
        return false;
    }

    // 0부터 시작하도록 base shift
    int64_t base_dts = AV_NOPTS_VALUE;
    int64_t base_pts = AV_NOPTS_VALUE;

    for (auto* p : pkts) { if (p && p->dts != AV_NOPTS_VALUE) { base_dts = p->dts; break; } }
    for (auto* p : pkts) { if (p && p->pts != AV_NOPTS_VALUE) { base_pts = p->pts; break; } }

    if (base_dts == AV_NOPTS_VALUE) base_dts = 0;
    if (base_pts == AV_NOPTS_VALUE) base_pts = base_dts;

    for (auto* p : pkts) {
        if (!p) continue;
        p->stream_index = st->index;

        if (p->dts != AV_NOPTS_VALUE) p->dts -= base_dts;
        if (p->pts != AV_NOPTS_VALUE) p->pts -= base_pts;

        if (p->pts == AV_NOPTS_VALUE && p->dts != AV_NOPTS_VALUE) p->pts = p->dts;
        if (p->pts < 0 && p->dts >= 0) p->pts = p->dts;

        av_packet_rescale_ts(p, in_tb, st->time_base);

        ret = av_interleaved_write_frame(oc, p);
        if (ret < 0) {
            // mp4에서 실패하면 (코덱/비트스트림 형태) mkv로 플랜B 권장
            break;
        }
    }

    av_write_trailer(oc);
    if (oc->pb) avio_closep(&oc->pb);
    avformat_free_context(oc);
    return true;
}

// ============================
// Global shared MP4 writer
// ============================
struct Mp4WriteJob {
    std::string path;
    std::vector<AVPacket*> pkts;      // ownership: writer가 free
    AVCodecParameters* par = nullptr; // ownership: writer가 free
    AVRational tb{ 0,1 };
};

class GlobalMp4Writer {
public:
    static GlobalMp4Writer& instance() {
        // ✅ 10채널 기준: writer thread 2개 정도 (환경 따라 1~2로 튜닝)
        static GlobalMp4Writer inst(/*threads=*/2, /*maxQueue=*/32);
        return inst;
    }

    void enqueue(Mp4WriteJob&& job) {
        if (job.path.empty() || job.pkts.empty() || !job.par) {
            free_job(job);
            return;
        }

        std::unique_lock<std::mutex> lk(mtx_);

        // ✅ 큐 폭주 방지: 오래된 job 드랍
        while (q_.size() >= max_q_) {
            Mp4WriteJob drop = std::move(q_.front());
            q_.pop_front();
            lk.unlock();
            free_job(drop);
            lk.lock();
        }

        q_.push_back(std::move(job));
        lk.unlock();
        cv_.notify_one();
    }

    ~GlobalMp4Writer() {
        stop();
    }

private:
    std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<Mp4WriteJob> q_;
    std::vector<std::thread> threads_;
    std::atomic<bool> stop_{ false };
    size_t max_q_ = 32;

    GlobalMp4Writer(int threads, size_t maxQueue)
        : max_q_(maxQueue)
    {
        stop_ = false;
        threads_.reserve((size_t)threads);
        for (int i = 0; i < threads; ++i) {
            threads_.emplace_back([this]() { this->worker_loop(); });
        }
    }

    void stop() {
        stop_ = true;
        cv_.notify_all();
        for (auto& t : threads_) {
            if (t.joinable()) t.join();
        }
        threads_.clear();

        // 남은 큐 정리(드랍)
        while (!q_.empty()) {
            Mp4WriteJob j = std::move(q_.front());
            q_.pop_front();
            free_job(j);
        }
    }

    static void free_job(Mp4WriteJob& job) {
        for (auto* p : job.pkts) {
            if (p) av_packet_free(&p);
        }
        job.pkts.clear();
        if (job.par) {
            avcodec_parameters_free(&job.par);
            job.par = nullptr;
        }
        job.path.clear();
    }

    void worker_loop() {
        for (;;) {
            Mp4WriteJob job;

            {
                std::unique_lock<std::mutex> lk(mtx_);
                cv_.wait(lk, [&]() { return stop_ || !q_.empty(); });

                if (q_.empty()) {
                    if (stop_) break;
                    continue;
                }

                job = std::move(q_.front());
                q_.pop_front();
            }

            write_mp4_impl(job.path.c_str(), job.pkts, job.par, job.tb);
            free_job(job);
        }
    }
};

// ============================
// RtspSession
// ============================
struct RtspSession {
    int channelId = 0;
    std::string url;
    RtspOptions opt{};
    RtspOptions2 opt2{};
    RtspFrameCallback onFrame = nullptr;
    RtspFrameCallbackEx onFrameEx = nullptr;
    RtspStatusCallback onStatus = nullptr;
    void* user = nullptr;

    std::atomic<bool> stopReq{ false };
    std::thread worker;

    AVFormatContext* fmt = nullptr;
    AVCodecContext* dec = nullptr;
    SwsContext* sws = nullptr;
    int videoStreamIndex = -1;

    AVPacket* pkt = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* frameOut = nullptr;
    AVFrame* sw_frame = nullptr;            // HW→SW transfer용
    AVBufferRef* hw_device_ctx = nullptr;   // CUDA device context
    AVPixelFormat hw_pix_fmt = AV_PIX_FMT_NONE; // HW 디코더 출력 포맷
    bool using_hw_decode = false;

    std::vector<uint8_t> outBuf;
    RtspPixelFormat outPixfmt = RTSP_PIXFMT_BGRA32;

    int64_t lastEmitMs = 0;

    AVPixelFormat cachedSrcFmt = AV_PIX_FMT_NONE;
    AVPixelFormat cachedDstFmt = AV_PIX_FMT_NONE;
    int cachedW = 0;
    int cachedH = 0;

    // ===== Packet Ring Buffer =====
    struct RingPkt {
        AVPacket* pkt = nullptr;
        int64_t   ms = 0;
        bool      key = false;
    };

    std::mutex ring_mtx_;
    std::vector<RingPkt> ring_;
    size_t ring_head_ = 0;   // next write
    size_t ring_size_ = 0;
    size_t ring_cap_ = 0;

    // ✅ 10채널 운영용: 시간/바이트 상한 (튜닝 가능)
    int64_t ring_keep_ms_ = 20'000;                 // 최근 20초 유지
    size_t  ring_max_bytes_ = 32ull * 1024 * 1024;  // 채널당 32MB 상한
    size_t  ring_bytes_ = 0;

    // ✅ PTS 역행(재연결/카메라 리셋) 감지
    int64_t last_ring_ms_ = (std::numeric_limits<int64_t>::min)();

    // ✅ 스트림 시그너처(재연결 시 ring 유지 가능 여부 판단)
    AVCodecParameters* last_codecpar_ = nullptr;
    AVRational last_tb_{ 0,1 };
    bool have_last_sig_ = false;

    // ===== Clip Capture State =====
    std::atomic<bool> cap_active_{ false };
    int64_t cap_center_ms_ = 0;
    int cap_pre_ms_ = 0;
    int cap_post_ms_ = 0;
    int64_t cap_end_ms_ = 0;

    std::mutex cap_mtx_;
    std::vector<AVPacket*> cap_pkts_;
    std::string cap_path_;
    AVCodecParameters* cap_codecpar_ = nullptr;
    AVRational cap_time_base_{ 0,1 };

    RtspSession(int ch, const char* u, const RtspOptions& o, RtspFrameCallback fcb, RtspStatusCallback scb, void* usr)
        : channelId(ch), url(u ? u : ""), opt(o), onFrame(fcb), onStatus(scb), user(usr)
    {
        ring_cap_ = (size_t)(30 * 20 * 2); // 1200 (보험용)
        ring_.resize(ring_cap_);
    }

    RtspSession(int ch, const char* u,
        const RtspOptions2& o2,
        RtspFrameCallbackEx fcbEx,
        RtspStatusCallback scb,
        void* usr)
        : channelId(ch), url(u ? u : ""),
        opt2(o2),
        onFrameEx(fcbEx),
        onStatus(scb),
        user(usr)
    {
        opt.tcp_only = opt2.tcp_only;
        opt.stimeout_us = opt2.stimeout_us;
        opt.reconnect = opt2.reconnect;
        opt.reconnect_wait_ms = opt2.reconnect_wait_ms;
        opt.target_fps = opt2.target_fps;
        outPixfmt = opt2.out_pixfmt;

        ring_cap_ = (size_t)(30 * 20 * 2);
        ring_.resize(ring_cap_);
    }

    ~RtspSession() {
        cleanup_all();
    }

    static bool codecpar_compatible(const AVCodecParameters* a, const AVCodecParameters* b) {
        if (!a || !b) return false;
        if (a->codec_id != b->codec_id) return false;
        if (a->width != b->width || a->height != b->height) return false;

        // extradata(SPS/PPS 등) 비교: 달라지면 mp4 remux 실패 가능성 큼
        if (a->extradata_size != b->extradata_size) return false;
        if (a->extradata_size > 0) {
            if (!a->extradata || !b->extradata) return false;
            if (std::memcmp(a->extradata, b->extradata, (size_t)a->extradata_size) != 0) return false;
        }
        return true;
    }

    void free_cap_packets_locked() {
        for (auto* p : cap_pkts_) { if (p) av_packet_free(&p); }
        cap_pkts_.clear();
        if (cap_codecpar_) { avcodec_parameters_free(&cap_codecpar_); cap_codecpar_ = nullptr; }
    }

    void abort_capture_locked_(const char* reason) {
        // cap_mtx_ 락 잡힌 상태에서 호출
        if (!cap_active_) return;

        cap_active_ = false;
        free_cap_packets_locked();
        cap_path_.clear();

        cap_center_ms_ = 0;
        cap_pre_ms_ = 0;
        cap_post_ms_ = 0;
        cap_end_ms_ = 0;

        if (reason) log_status(onStatus, user, channelId, RTSP_LOG_WARN, 0, reason);
    }

    void cleanup_ring_locked() {
        for (auto& s : ring_) {
            if (s.pkt) av_packet_free(&s.pkt);
            s.ms = 0; s.key = false;
        }
        ring_head_ = 0;
        ring_size_ = 0;
        ring_bytes_ = 0;
        last_ring_ms_ = (std::numeric_limits<int64_t>::min)();
    }

    void cleanup_decoder_only() {
        if (pkt) { av_packet_free(&pkt); pkt = nullptr; }
        if (frame) { av_frame_free(&frame); frame = nullptr; }
        if (frameOut) { av_frame_free(&frameOut); frameOut = nullptr; }
        if (sw_frame) { av_frame_free(&sw_frame); sw_frame = nullptr; }

        if (sws) { sws_freeContext(sws); sws = nullptr; }
        if (dec) { avcodec_free_context(&dec); dec = nullptr; }
        if (fmt) { avformat_close_input(&fmt); fmt = nullptr; }
        if (hw_device_ctx) { av_buffer_unref(&hw_device_ctx); hw_device_ctx = nullptr; }

        videoStreamIndex = -1;
        outBuf.clear();
        hw_pix_fmt = AV_PIX_FMT_NONE;
        using_hw_decode = false;

        cachedSrcFmt = AV_PIX_FMT_NONE;
        cachedDstFmt = AV_PIX_FMT_NONE;
        cachedW = 0;
        cachedH = 0;

        lastEmitMs = 0;
    }

    void cleanup_all() {
        cleanup_decoder_only();

        {
            std::lock_guard<std::mutex> lk(cap_mtx_);
            abort_capture_locked_("Capture aborted: session cleanup");
        }

        {
            std::lock_guard<std::mutex> lk(ring_mtx_);
            cleanup_ring_locked();
        }

        if (last_codecpar_) {
            avcodec_parameters_free(&last_codecpar_);
            last_codecpar_ = nullptr;
        }
        have_last_sig_ = false;
        last_tb_ = AVRational{ 0,1 };
    }

    // HW pixel format negotiation callback (static, thread-local approach)
    static thread_local AVPixelFormat s_hw_pix_fmt_tl;

    static AVPixelFormat hw_get_format_cb(AVCodecContext* ctx, const AVPixelFormat* pix_fmts) {
        for (const AVPixelFormat* p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
            if (*p == s_hw_pix_fmt_tl)
                return *p;
        }
        // HW 포맷 불가 → SW fallback
        return pix_fmts[0];
    }

    bool try_open_hw_decoder(AVStream* vs) {
        // H.264 → h264_cuvid, HEVC → hevc_cuvid
        const char* hw_name = nullptr;
        if (vs->codecpar->codec_id == AV_CODEC_ID_H264)
            hw_name = "h264_cuvid";
        else if (vs->codecpar->codec_id == AV_CODEC_ID_HEVC)
            hw_name = "hevc_cuvid";
        else
            return false;

        const AVCodec* hw_codec = avcodec_find_decoder_by_name(hw_name);
        if (!hw_codec) return false;

        // HW device context 생성
        int ret = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_CUDA, nullptr, nullptr, 0);
        if (ret < 0) {
            log_status(onStatus, user, channelId, RTSP_LOG_WARN, ret,
                std::string("CUDA device init failed, SW fallback. (") + hw_name + ")");
            return false;
        }

        dec = avcodec_alloc_context3(hw_codec);
        if (!dec) {
            av_buffer_unref(&hw_device_ctx); hw_device_ctx = nullptr;
            return false;
        }

        ret = avcodec_parameters_to_context(dec, vs->codecpar);
        if (ret < 0) {
            avcodec_free_context(&dec); dec = nullptr;
            av_buffer_unref(&hw_device_ctx); hw_device_ctx = nullptr;
            return false;
        }

        dec->hw_device_ctx = av_buffer_ref(hw_device_ctx);

        // cuvid 디코더의 HW 출력 포맷 탐색
        hw_pix_fmt = AV_PIX_FMT_NONE;
        for (int i = 0;; i++) {
            const AVCodecHWConfig* config = avcodec_get_hw_config(hw_codec, i);
            if (!config) break;
            if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                config->device_type == AV_HWDEVICE_TYPE_CUDA) {
                hw_pix_fmt = config->pix_fmt;
                break;
            }
        }

        if (hw_pix_fmt != AV_PIX_FMT_NONE) {
            s_hw_pix_fmt_tl = hw_pix_fmt;
            dec->get_format = hw_get_format_cb;
        }

        ret = avcodec_open2(dec, hw_codec, nullptr);
        if (ret < 0) {
            log_status(onStatus, user, channelId, RTSP_LOG_WARN, ret,
                std::string("HW decoder open failed, SW fallback. (") + hw_name + ")");
            avcodec_free_context(&dec); dec = nullptr;
            av_buffer_unref(&hw_device_ctx); hw_device_ctx = nullptr;
            hw_pix_fmt = AV_PIX_FMT_NONE;
            return false;
        }

        using_hw_decode = true;
        log_status(onStatus, user, channelId, RTSP_LOG_INFO, 0,
            std::string("GPU decode enabled: ") + hw_name);
        return true;
    }

    bool open_stream() {
        // ✅ 재연결 시 ring 유지: decoder/format만 정리
        cleanup_decoder_only();

        // ✅ 재연결 시 캡처는 안전하게 중단(PTS/코덱 변경 가능)
        {
            std::lock_guard<std::mutex> lk(cap_mtx_);
            abort_capture_locked_("Capture aborted: reconnect");
        }

        AVDictionary* dict = nullptr;
        if (opt.tcp_only) av_dict_set(&dict, "rtsp_transport", "tcp", 0);
        if (opt.stimeout_us > 0) av_dict_set_int(&dict, "stimeout", (int64_t)opt.stimeout_us, 0);

        int ret = avformat_open_input(&fmt, url.c_str(), nullptr, &dict);
        av_dict_free(&dict);

        if (ret < 0) {
            log_status(onStatus, user, channelId, RTSP_LOG_ERROR, ret, "avformat_open_input failed");
            return false;
        }

        ret = avformat_find_stream_info(fmt, nullptr);
        if (ret < 0) {
            log_status(onStatus, user, channelId, RTSP_LOG_ERROR, ret, "avformat_find_stream_info failed");
            return false;
        }

        ret = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (ret < 0) {
            log_status(onStatus, user, channelId, RTSP_LOG_ERROR, ret, "No video stream found");
            return false;
        }
        videoStreamIndex = ret;

        AVStream* vs = fmt->streams[videoStreamIndex];

        // ✅ 스트림 파라미터가 바뀌면 ring 섞지 않도록 ring clear
        {
            std::lock_guard<std::mutex> lk(ring_mtx_);
            if (have_last_sig_) {
                if (!codecpar_compatible(last_codecpar_, vs->codecpar)) {
                    log_status(onStatus, user, channelId, RTSP_LOG_WARN, 0, "Stream params changed. Ring cleared.");
                    cleanup_ring_locked();
                    have_last_sig_ = false;
                    if (last_codecpar_) { avcodec_parameters_free(&last_codecpar_); last_codecpar_ = nullptr; }
                }
            }

            if (!have_last_sig_) {
                last_codecpar_ = avcodec_parameters_alloc();
                if (last_codecpar_) {
                    avcodec_parameters_copy(last_codecpar_, vs->codecpar);
                    have_last_sig_ = true;
                }
            }
            last_tb_ = vs->time_base;
        }

        // ✅ GPU 디코더 우선 시도 → 실패 시 SW fallback
        if (!try_open_hw_decoder(vs)) {
            const AVCodec* codec = avcodec_find_decoder(vs->codecpar->codec_id);
            if (!codec) {
                log_status(onStatus, user, channelId, RTSP_LOG_ERROR, -2, "Decoder not found");
                return false;
            }

            dec = avcodec_alloc_context3(codec);
            if (!dec) {
                log_status(onStatus, user, channelId, RTSP_LOG_ERROR, -3, "avcodec_alloc_context3 failed");
                return false;
            }

            ret = avcodec_parameters_to_context(dec, vs->codecpar);
            if (ret < 0) {
                log_status(onStatus, user, channelId, RTSP_LOG_ERROR, ret, "avcodec_parameters_to_context failed");
                return false;
            }

            ret = avcodec_open2(dec, codec, nullptr);
            if (ret < 0) {
                log_status(onStatus, user, channelId, RTSP_LOG_ERROR, ret, "avcodec_open2 failed");
                return false;
            }
        }

        pkt = av_packet_alloc();
        frame = av_frame_alloc();
        frameOut = av_frame_alloc();
        sw_frame = av_frame_alloc();
        if (!pkt || !frame || !frameOut || !sw_frame) {
            log_status(onStatus, user, channelId, RTSP_LOG_ERROR, -4, "packet/frame alloc failed");
            return false;
        }

        log_status(onStatus, user, channelId, RTSP_LOG_INFO, 0,
            using_hw_decode ? "RTSP opened (GPU decode)" : "RTSP opened (SW decode)");
        return true;
    }

    static AVPixelFormat to_av_dst_fmt(RtspPixelFormat pf) {
        return (pf == RTSP_PIXFMT_BGR24) ? AV_PIX_FMT_BGR24 : AV_PIX_FMT_BGRA;
    }

    void ensure_out(int w, int h, AVPixelFormat srcFmt, RtspPixelFormat emitFmt) {
        AVPixelFormat dstFmt = to_av_dst_fmt(emitFmt);

        if (sws && cachedW == w && cachedH == h && cachedSrcFmt == srcFmt && cachedDstFmt == dstFmt) return;

        sws = sws_getCachedContext(
            sws,
            w, h, srcFmt,
            w, h, dstFmt,
            SWS_FAST_BILINEAR,
            nullptr, nullptr, nullptr
        );
        if (!sws) {
            log_status(onStatus, user, channelId, RTSP_LOG_ERROR, -10, "sws_getCachedContext failed");
            return;
        }

        cachedW = w; cachedH = h;
        cachedSrcFmt = srcFmt;
        cachedDstFmt = dstFmt;

        frameOut->format = dstFmt;
        frameOut->width = w;
        frameOut->height = h;

        int bufSize = av_image_get_buffer_size(dstFmt, w, h, 1);
        if (bufSize <= 0) {
            log_status(onStatus, user, channelId, RTSP_LOG_ERROR, -11, "av_image_get_buffer_size failed");
            return;
        }

        outBuf.resize((size_t)bufSize);
        av_image_fill_arrays(frameOut->data, frameOut->linesize, outBuf.data(), dstFmt, w, h, 1);
    }

    bool should_emit(int64_t nowMs) {
        if (opt.target_fps <= 0) return true;
        int64_t interval = 1000 / opt.target_fps;
        if (nowMs - lastEmitMs >= interval) {
            lastEmitMs = nowMs;
            return true;
        }
        return false;
    }

    int64_t pts_to_ms(const AVFrame* f) const {
        if (!f || !fmt || videoStreamIndex < 0) return 0;
        AVStream* vs = fmt->streams[videoStreamIndex];
        if (f->best_effort_timestamp == AV_NOPTS_VALUE) return 0;
        double sec = f->best_effort_timestamp * av_q2d(vs->time_base);
        return (int64_t)(sec * 1000.0);
    }

    int64_t pkt_to_ms(const AVPacket* p) const {
        if (!p || !fmt || videoStreamIndex < 0) return 0;
        AVStream* vs = fmt->streams[videoStreamIndex];

        int64_t ts = (p->dts != AV_NOPTS_VALUE) ? p->dts : p->pts;
        if (ts == AV_NOPTS_VALUE) {
            // monotonic fallback
            return (int64_t)av_gettime_relative() / 1000;
        }
        return av_rescale_q(ts, vs->time_base, AVRational{ 1,1000 });
    }

    void ring_prune_locked(int64_t newest_ms) {
        while (ring_size_ > 0) {
            size_t oldest = (ring_head_ + ring_cap_ - ring_size_) % ring_cap_;
            RingPkt& s = ring_[oldest];

            bool too_old = false;
            if (newest_ms > 0 && s.ms > 0) {
                too_old = (newest_ms - s.ms) > ring_keep_ms_;
            }
            bool too_big = ring_bytes_ > ring_max_bytes_;

            if (!too_old && !too_big) break;

            if (s.pkt) {
                size_t sz = (s.pkt->size > 0) ? (size_t)s.pkt->size : 0;
                if (ring_bytes_ >= sz) ring_bytes_ -= sz;
                av_packet_free(&s.pkt);
            }
            s.ms = 0; s.key = false;
            ring_size_--;
        }
    }

    void ring_push(const AVPacket* src, int64_t ms, bool key) {
        if (!src || ring_cap_ == 0) return;

        std::lock_guard<std::mutex> lk(ring_mtx_);

        // ✅ PTS(ms) 역행 감지(재연결/카메라 타임스탬프 리셋) → ring clear
        // ring에 데이터가 있을 때만 역행 판단 (최초/재연결 직후는 skip)
        if (ring_size_ > 0 && last_ring_ms_ != (std::numeric_limits<int64_t>::min)())
        {
            if (ms >= 0 && last_ring_ms_ >= 0 && ms + 2000 < last_ring_ms_) {
                log_status(onStatus, user, channelId, RTSP_LOG_WARN, 0, "Packet timestamp went backwards. Ring cleared.");
                cleanup_ring_locked();
            }
        }
        last_ring_ms_ = ms;

        RingPkt& slot = ring_[ring_head_];

        if (slot.pkt) {
            size_t sz = (slot.pkt->size > 0) ? (size_t)slot.pkt->size : 0;
            if (ring_bytes_ >= sz) ring_bytes_ -= sz;
            av_packet_free(&slot.pkt);
        }

        slot.pkt = av_packet_alloc();
        if (!slot.pkt) return;

        av_packet_ref(slot.pkt, src);
        slot.ms = ms;
        slot.key = key;

        ring_bytes_ += (slot.pkt->size > 0) ? (size_t)slot.pkt->size : 0;

        ring_head_ = (ring_head_ + 1) % ring_cap_;
        if (ring_size_ < ring_cap_) ring_size_++;

        // ✅ 시간/바이트 기준 prune
        ring_prune_locked(ms);
    }

    std::vector<AVPacket*> ring_snapshot(int64_t start_ms, int64_t end_ms, bool align_to_key) {
        std::vector<AVPacket*> out;

        std::lock_guard<std::mutex> lk(ring_mtx_);
        if (ring_size_ == 0) return out;

        size_t oldest = (ring_head_ + ring_cap_ - ring_size_) % ring_cap_;

        std::vector<size_t> idxs;
        idxs.reserve(ring_size_);

        for (size_t i = 0; i < ring_size_; i++) {
            size_t pos = (oldest + i) % ring_cap_;
            const auto& s = ring_[pos];
            if (!s.pkt) continue;
            if (s.ms >= start_ms && s.ms <= end_ms) idxs.push_back(pos);
        }
        if (idxs.empty()) return out;

        if (align_to_key) {
            size_t first_pos = idxs[0];
            size_t best_pos = first_pos;
            bool found = false;

            for (size_t i = 0; i < ring_size_; i++) {
                size_t pos = (oldest + i) % ring_cap_;
                if (pos == first_pos) break;
                if (ring_[pos].pkt && ring_[pos].key) {
                    best_pos = pos;
                    found = true;
                }
            }

            if (found) {
                idxs.clear();
                int64_t key_ms = ring_[best_pos].ms;
                for (size_t i = 0; i < ring_size_; i++) {
                    size_t pos = (oldest + i) % ring_cap_;
                    const auto& s = ring_[pos];
                    if (!s.pkt) continue;
                    if (s.ms >= key_ms && s.ms <= end_ms) idxs.push_back(pos);
                }
            }
        }

        out.reserve(idxs.size());
        for (size_t pos : idxs) {
            AVPacket* cp = av_packet_alloc();
            if (!cp) continue;
            av_packet_ref(cp, ring_[pos].pkt);
            out.push_back(cp);
        }
        return out;
    }

    int request_clip(int64_t center_ms, int pre_ms, int post_ms, const char* path) {
        if (!path || !*path) return -1;
        if (!fmt || videoStreamIndex < 0) return -2;

        std::lock_guard<std::mutex> lk(cap_mtx_);
        if (cap_active_) return -3;

        cap_center_ms_ = center_ms;
        cap_pre_ms_ = pre_ms;
        cap_post_ms_ = post_ms;
        cap_end_ms_ = center_ms + post_ms;
        cap_path_ = path;

        AVStream* vs = fmt->streams[videoStreamIndex];

        cap_codecpar_ = avcodec_parameters_alloc();
        if (!cap_codecpar_) return -4;
        avcodec_parameters_copy(cap_codecpar_, vs->codecpar);
        cap_time_base_ = vs->time_base;

        int64_t start_ms = center_ms - pre_ms;
        cap_pkts_ = ring_snapshot(start_ms, center_ms, /*align_to_key=*/true);

        cap_active_ = true;
        return 0;
    }

    void loop() {
        avformat_network_init();

        while (!stopReq) {
            if (!open_stream()) {
                if (!opt.reconnect || stopReq) break;
                log_status(onStatus, user, channelId, RTSP_LOG_WARN, 1, "Reconnect wait...");
                std::this_thread::sleep_for(std::chrono::milliseconds(opt.reconnect_wait_ms));
                continue;
            }

            auto drain_frames = [&]() -> bool {
                while (!stopReq) {
                    int ret = avcodec_receive_frame(dec, frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) return true;
                    if (ret < 0) {
                        log_status(onStatus, user, channelId, RTSP_LOG_WARN, ret, "receive_frame failed");
                        return false;
                    }

                    int64_t nowMs = (int64_t)av_gettime_relative() / 1000;
                    if (!should_emit(nowMs)) {
                        av_frame_unref(frame);
                        continue;
                    }

                    // ✅ HW frame → SW frame 전송 (GPU→CPU)
                    AVFrame* src_frame = frame;
                    if (using_hw_decode && frame->format == hw_pix_fmt) {
                        ret = av_hwframe_transfer_data(sw_frame, frame, 0);
                        if (ret < 0) {
                            log_status(onStatus, user, channelId, RTSP_LOG_WARN, ret, "HW frame transfer failed");
                            av_frame_unref(frame);
                            continue;
                        }
                        sw_frame->best_effort_timestamp = frame->best_effort_timestamp;
                        sw_frame->pts = frame->pts;
                        src_frame = sw_frame;
                    }

                    int w = src_frame->width;
                    int h = src_frame->height;
                    AVPixelFormat srcFmt = (AVPixelFormat)src_frame->format;

                    RtspPixelFormat emitFmt = onFrameEx ? outPixfmt : RTSP_PIXFMT_BGRA32;
                    ensure_out(w, h, srcFmt, emitFmt);

                    const uint8_t* srcSlice[4] = { src_frame->data[0], src_frame->data[1], src_frame->data[2], src_frame->data[3] };
                    int srcStride[4] = { src_frame->linesize[0], src_frame->linesize[1], src_frame->linesize[2], src_frame->linesize[3] };

                    sws_scale(sws, srcSlice, srcStride, 0, h, frameOut->data, frameOut->linesize);

                    int64_t ptsMs = pts_to_ms(src_frame);

                    if (onFrameEx) {
                        onFrameEx(user, channelId,
                            frameOut->data[0], w, h, frameOut->linesize[0],
                            ptsMs, emitFmt);
                    }
                    else if (onFrame) {
                        onFrame(user, channelId,
                            frameOut->data[0], w, h, frameOut->linesize[0],
                            ptsMs);
                    }

                    if (src_frame == sw_frame) av_frame_unref(sw_frame);
                    av_frame_unref(frame);
                }
                return true;
                };

            bool ok = true;
            while (!stopReq && ok) {
                int ret = av_read_frame(fmt, pkt);
                if (ret < 0) {
                    log_status(onStatus, user, channelId, RTSP_LOG_WARN, ret, "av_read_frame failed");
                    ok = false;
                    break;
                }

                // ✅ 비디오만
                if (pkt->stream_index != videoStreamIndex) {
                    av_packet_unref(pkt);
                    continue;
                }

                int64_t pms = pkt_to_ms(pkt);
                bool is_key = (pkt->flags & AV_PKT_FLAG_KEY) != 0;

                // 1) 링 저장 (✅ 재연결돼도 ring 유지)
                ring_push(pkt, pms, is_key);

                // 2) 캡처 중이면 post 구간 모으기 + 완료되면 전역 writer에 job enqueue
                bool need_finish = false;
                {
                    std::lock_guard<std::mutex> lk(cap_mtx_);
                    if (cap_active_) {
                        if (pms <= cap_end_ms_) {
                            AVPacket* cp = av_packet_alloc();
                            if (cp) {
                                av_packet_ref(cp, pkt);
                                cap_pkts_.push_back(cp);
                            }
                        }
                        else {
                            cap_active_ = false;
                            need_finish = true;
                        }
                    }
                }

                if (need_finish) {
                    Mp4WriteJob job;

                    {
                        std::lock_guard<std::mutex> lk(cap_mtx_);
                        job.pkts.swap(cap_pkts_);
                        job.path.swap(cap_path_);
                        job.par = cap_codecpar_; cap_codecpar_ = nullptr;
                        job.tb = cap_time_base_;
                    }

                    GlobalMp4Writer::instance().enqueue(std::move(job));
                }

                // 디코더로 보내기
                int sendRet = avcodec_send_packet(dec, pkt);
                if (sendRet == AVERROR(EAGAIN)) {
                    if (!drain_frames()) ok = false;
                    sendRet = avcodec_send_packet(dec, pkt);
                }

                av_packet_unref(pkt);

                if (sendRet < 0) {
                    log_status(onStatus, user, channelId, RTSP_LOG_WARN, sendRet, "send_packet failed");
                    continue;
                }

                if (!drain_frames()) {
                    ok = false;
                    break;
                }
            }

            // ✅ reconnect 대비: decoder/format만 정리 (ring 유지)
            cleanup_decoder_only();

            if (!opt.reconnect || stopReq) break;
            log_status(onStatus, user, channelId, RTSP_LOG_WARN, 2, "Stream closed. Reconnecting...");
            std::this_thread::sleep_for(std::chrono::milliseconds(opt.reconnect_wait_ms));
        }

        avformat_network_deinit();
        log_status(onStatus, user, channelId, RTSP_LOG_INFO, 999, "RTSP session stopped");
    }
};

// thread_local static member definition
thread_local AVPixelFormat RtspSession::s_hw_pix_fmt_tl = AV_PIX_FMT_NONE;

// ============================
// C API
// ============================
RTSP_API RtspHandle RTSP_CALL rtsp_create(
    int channelId,
    const char* rtspUrl,
    const RtspOptions* opt,
    RtspFrameCallback onFrame,
    RtspStatusCallback onStatus,
    void* user
) {
    RtspOptions o{};
    rtsp_default_options(&o);
    if (opt) o = *opt;

    auto* s = new RtspSession(channelId, rtspUrl, o, onFrame, onStatus, user);
    return (RtspHandle)s;
}

void RTSP_CALL rtsp_default_options2(RtspOptions2* opt) {
    if (!opt) return;
    opt->struct_size = (uint32_t)sizeof(RtspOptions2);
    opt->tcp_only = 1;
    opt->stimeout_us = 5'000'000;
    opt->reconnect = 1;
    opt->reconnect_wait_ms = 1000;
    opt->target_fps = 0;
    opt->out_pixfmt = RTSP_PIXFMT_BGRA32;
}

RTSP_API RtspHandle RTSP_CALL rtsp_create2(
    int channelId,
    const char* rtspUrl,
    const RtspOptions2* opt,
    RtspFrameCallbackEx onFrameEx,
    RtspStatusCallback onStatus,
    void* user
) {
    RtspOptions2 o2{};
    rtsp_default_options2(&o2);

    if (opt) {
        uint32_t sz = opt->struct_size;
        if (sz == 0) sz = (uint32_t)sizeof(RtspOptions2);
        uint32_t copy = (sz < (uint32_t)sizeof(RtspOptions2)) ? sz : (uint32_t)sizeof(RtspOptions2);
        std::memcpy(&o2, opt, copy);
        o2.struct_size = (uint32_t)sizeof(RtspOptions2);
    }

    auto* s = new RtspSession(channelId, rtspUrl, o2, onFrameEx, onStatus, user);
    return (RtspHandle)s;
}

RTSP_API int RTSP_CALL rtsp_start(RtspHandle h) {
    if (!h) return -1;
    auto* s = (RtspSession*)h;
    s->stopReq = false;
    s->worker = std::thread([s]() { s->loop(); });
    return 0;
}

RTSP_API void RTSP_CALL rtsp_stop(RtspHandle h) {
    if (!h) return;
    auto* s = (RtspSession*)h;
    s->stopReq = true;
    if (s->worker.joinable()) s->worker.join();
}

RTSP_API void RTSP_CALL rtsp_destroy(RtspHandle h) {
    if (!h) return;
    rtsp_stop(h);
    auto* s = (RtspSession*)h;
    delete s;
}

RTSP_API int RTSP_CALL rtsp_request_clip(RtspHandle h, int64_t center_pts_ms, int pre_ms, int post_ms, const char* out_path) {
    if (!h) return -1;
    auto* s = (RtspSession*)h;
    return s->request_clip(center_pts_ms, pre_ms, post_ms, out_path);
}