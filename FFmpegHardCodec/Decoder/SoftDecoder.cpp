#ifdef USE_FFMPEG_SOFT
#include "HardDecoder.h"
static const uint64_t NANO_SECOND = UINT64_C(1000000000);
int HardVideoDecoder::SoftDecInit(bool is_h265)
{
    if (codec_) {
        log_warn("has been init Decoder...");
        return -1;
    }
    if (is_h265) {
        decodec_id_ = AV_CODEC_ID_H265;
    } else {
        decodec_id_ = AV_CODEC_ID_H264;
    }
    codec_ = avcodec_find_decoder(decodec_id_);
#if 0
    codec_->capabilities |= AV_CODEC_CAP_DELAY;
#endif
    codec_ctx_ = avcodec_alloc_context3(codec_);
    codec_ctx_->flags |= AV_CODEC_FLAG_LOW_DELAY;
    if (is_h265) {
        codec_ctx_->thread_count = 3;
    } else {
        codec_ctx_->thread_count = 1;
    }
    if (avcodec_open2(codec_ctx_, codec_, NULL) < 0) {
        log_error("no decodec can be used");
        avcodec_close(codec_ctx_);
        avcodec_free_context(&codec_ctx_);
        exit(1);
    }
    log_info("open soft dec ok");
    return 1;
}
HardVideoDecoder::HardVideoDecoder(bool is_h265)
{
    codec_ctx_ = NULL;
    codec_ = NULL;
    SoftDecInit(is_h265);
    abort_ = false;
    av_init_packet(&packet_);
    frame_ = NULL;
    img_convert_ctx_ = NULL;
    callback_ = NULL;
    time_inited_ = 0;
    now_frames_ = pre_frames_ = 0;
    pthread_cond_init(&packet_cond_, NULL);
    pthread_mutex_init(&packet_mutex_, NULL);
    pthread_cond_init(&frame_cond_, NULL);
    pthread_mutex_init(&frame_mutex_, NULL);
    pthread_create(&dec_thread_id_, NULL, &HardVideoDecoder::DecodeThread, this);
    pthread_create(&sws_thread_id_, NULL, &HardVideoDecoder::ScaleThread, this);
}
HardVideoDecoder::~HardVideoDecoder()
{
    abort_ = true;

    int ret = pthread_join(dec_thread_id_, NULL);
    if (ret != 0) {
        log_error("Jion dec_thread_id_ Error!");
    }

    ret = pthread_join(sws_thread_id_, NULL);
    if (ret != 0) {
        log_error("Jion sws_thread_id_ Error!");
    }

    pthread_mutex_destroy(&frame_mutex_);
    pthread_cond_destroy(&frame_cond_);
    pthread_mutex_destroy(&packet_mutex_);
    pthread_cond_destroy(&packet_cond_);

    for (std::list<AVFrame *>::iterator it = yuv_frames_.begin(); it != yuv_frames_.end(); ++it) {
        AVFrame *frame = *it;
        // uint8_t *p = frame->data[0];
        // av_freep(&p);
        av_frame_free(&frame);
    }
    yuv_frames_.clear();
    for (std::list<HardDataNode *>::iterator it = es_packets_.begin(); it != es_packets_.end(); ++it) {
        HardDataNode *packet = *it;
        delete packet;
    }
    es_packets_.clear();

    if (codec_ctx_) {
        if (codec_ctx_->extradata) {
            av_free(codec_ctx_->extradata);
            codec_ctx_->extradata = NULL;
            codec_ctx_->extradata_size = 0;
        }
        avcodec_close(codec_ctx_);
        if (codec_ctx_->opaque != NULL) {
            free(codec_ctx_->opaque);
            codec_ctx_->opaque = NULL;
        }
        avcodec_free_context(&codec_ctx_);
    }
    if (frame_) {
        av_frame_free(&frame_);
        frame_ = NULL;
    }
    if (img_convert_ctx_ != NULL) {
        sws_freeContext(img_convert_ctx_);
        img_convert_ctx_ = NULL;
    }
    av_free_packet(&packet_);
    log_debug("~HardVideoDecoder");
}
void HardVideoDecoder::SetFrameFetchCallback(DecDataCallListner *call_func)
{
    callback_ = call_func;
    return;
}

void HardVideoDecoder::InputVideoData(unsigned char *data, int data_len, int64_t duration, int64_t pts)
{

    HardDataNode *node = new HardDataNode();
    node->es_data = (unsigned char *)malloc(data_len);
    memcpy(node->es_data, data, data_len);
    node->es_data_len = data_len;

    pthread_mutex_lock(&packet_mutex_);
    es_packets_.push_back(node);
    pthread_mutex_unlock(&packet_mutex_);
    pthread_cond_signal(&packet_cond_);
    return;
}
void HardVideoDecoder::DecodeVideo(HardDataNode *data)
{
    packet_.data = data->es_data;
    packet_.size = data->es_data_len;
    int ret = avcodec_send_packet(codec_ctx_, &packet_);
    if (ret != 0) {
        av_packet_unref(&packet_);
        return;
    }
    // 新版本avcodec_send_packet一次，需要循环调用avcodec_receive_frame多次，返回EAGAIN后，结束当前这次的解码
    int cnt = 0;
    while (1) {
        if (!frame_) {
            frame_ = av_frame_alloc();
        }
        ret = avcodec_receive_frame(codec_ctx_, frame_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF || ret < 0) {
            // log_debug("avcodec_receive_frame ret cnt:{} type:{}",cnt,type);
            // if(ret < 0){
            //     log_error("Error while decoding");
            // }
            if (frame_) {
                av_frame_free(&frame_);
                frame_ = NULL;
            }
            av_packet_unref(&packet_);
            return;
        }
        if (out_pix_fmt_ == AV_PIX_FMT_NONE) {
            const char *pixname = av_get_pix_fmt_name(AVPixelFormat(frame_->format));
            log_debug("out_pix_fmt_:{}", pixname);
            out_pix_fmt_ = (AVPixelFormat)frame_->format; // AV_PIX_FMT_NV12
        }
        pthread_mutex_lock(&frame_mutex_);
        yuv_frames_.push_back(frame_);
        pthread_mutex_unlock(&frame_mutex_);
        pthread_cond_signal(&frame_cond_);
        frame_ = NULL;

        cnt++;
    }
    return;
}
void *HardVideoDecoder::DecodeThread(void *arg)
{

    HardVideoDecoder *self = (HardVideoDecoder *)arg;
    while (!self->abort_) {
        pthread_mutex_lock(&self->packet_mutex_);
        if (!self->es_packets_.empty()) {
            HardDataNode *pVideoPacket = self->es_packets_.front();
            self->es_packets_.pop_front();
            pthread_mutex_unlock(&self->packet_mutex_);
            self->DecodeVideo(pVideoPacket);

            delete pVideoPacket;
        } else {

            struct timespec n_ts;
            clock_gettime(CLOCK_REALTIME, &n_ts);
            if (n_ts.tv_nsec + 10 * 1000000 >= NANO_SECOND) {
                n_ts.tv_sec += 1;
                n_ts.tv_nsec = n_ts.tv_nsec + 10 * 1000000 - NANO_SECOND;
            } else {
                n_ts.tv_nsec += 10 * 1000000;
            }
            pthread_cond_timedwait(&self->packet_cond_, &self->packet_mutex_, &n_ts);
            pthread_mutex_unlock(&self->packet_mutex_);

            continue;
        }
    }
    log_info("DecodeThread Finished ");
    // 刷新缓冲区
    HardDataNode *node = new HardDataNode();
    node->es_data = NULL;
    node->es_data_len = 0;
    self->DecodeVideo(node);
    delete node;
    return NULL;
}

void HardVideoDecoder::ScaleVideo(AVFrame *frame)
{
    if (!img_convert_ctx_) {
        img_convert_ctx_ = sws_getContext(frame->width, frame->height, out_pix_fmt_, frame->width, frame->height, AV_PIX_FMT_BGR24, SWS_FAST_BILINEAR, NULL, NULL, NULL); // YUV-->RGB
    }
    int size = frame->width * frame->height * 3;
    unsigned char *image_ptr = new unsigned char[size];
    /**
     * linesize[]数组中保存的是对应通道的数据宽度 ， 输出BGR为packed格式，所以指定linesize[0]既可，如果是planar格式，例如YUV420P
     * linesize[0]——-Y分量的宽度
     * linesize[1]——-U分量的宽度
     * linesize[2]——-V分量的宽度
     * linesize[i]的值并不一定等于图片的宽度，有时候为了对齐各解码器的CPU，实际尺寸会大于图片的宽度
     */
    int linesize[4] = {3 * frame->width, 0, 0, 0};

    sws_scale(img_convert_ctx_, frame->data, frame->linesize, 0, frame->height, (uint8_t **)&image_ptr, linesize); // 处理后的数据放到image_ptr中
    cv::Mat frame_mat(frame->height, frame->width, CV_8UC3, image_ptr);
    cv::Mat frame_ret = frame_mat.clone();
    if (callback_ != NULL) {
        now_frames_++;
        if (!time_inited_) {
            time_inited_ = 1;
            gettimeofday(&time_now_, NULL);
            gettimeofday(&time_pre_, NULL);
        } else {
            gettimeofday(&time_now_, NULL);
            long tmp_time = 1000 * (time_now_.tv_sec - time_pre_.tv_sec) + (time_now_.tv_usec - time_pre_.tv_usec) / 1000;
            if (tmp_time > 1000) { // 1s
                int tmp_frame_rate = (now_frames_ - pre_frames_ + 1) * 1000 / tmp_time;
                log_debug("input frame rate {} ", tmp_frame_rate);
                time_pre_ = time_now_;
                pre_frames_ = now_frames_;
            }
        }
        callback_->OnRGBData(frame_ret);
    }
    delete[] image_ptr;
    // uint8_t *p = frame->data[0];
    // av_freep(&p);
    av_frame_free(&frame);
    return;
}

void *HardVideoDecoder::ScaleThread(void *arg)
{
    HardVideoDecoder *self = (HardVideoDecoder *)arg;
    while (!self->abort_) {
        pthread_mutex_lock(&self->frame_mutex_);
        if (!self->yuv_frames_.empty()) {
            AVFrame *frame = self->yuv_frames_.front();
            self->yuv_frames_.pop_front();
            pthread_mutex_unlock(&self->frame_mutex_);
            self->ScaleVideo(frame);
        } else {

            struct timespec n_ts;
            clock_gettime(CLOCK_REALTIME, &n_ts);
            if (n_ts.tv_nsec + 10 * 1000000 >= NANO_SECOND) {
                n_ts.tv_sec += 1;
                n_ts.tv_nsec = n_ts.tv_nsec + 10 * 1000000 - NANO_SECOND;
            } else {
                n_ts.tv_nsec += 10 * 1000000;
            }
            pthread_cond_timedwait(&self->frame_cond_, &self->frame_mutex_, &n_ts);

            pthread_mutex_unlock(&self->frame_mutex_);
            continue;
        }
    }

    log_info("ScaleThread Finished");
    return NULL;
}
#endif
