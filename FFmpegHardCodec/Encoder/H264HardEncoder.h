#ifndef H264_HARD_ENC_H
#define H264_HARD_ENC_H

#include "DecEncInterface.h"
#include <opencv2/opencv.hpp>
#include <sys/time.h>
#include <string.h>
#include <pthread.h>
#include <list>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#define DROP_FRAME
#ifdef USE_FFMPEG_NVIDIA
class HardVideoEncoder
{
public:
    HardVideoEncoder();
    ~HardVideoEncoder();
    int AddVideoFrame(cv::Mat bgr_frame);
    int Init(cv::Mat init_frame, int fps);
    void SetDataCallback(EncDataCallListner *call_func);

private:
    int HardEncInit(int width, int height, int fps);
    int SoftEncInit(int width, int height, int fps);
    static void *VideoScaleThread(void *arg);
    static void *VideoEncThread(void *arg);

private:
    EncDataCallListner *callback_ = NULL;
    AVCodecContext *h264_codec_ctx_ = NULL;
    AVCodec *h264_codec_ = NULL;
    SwsContext *sws_context_ = NULL;
    enum AVPixelFormat sw_pix_format_ = AV_PIX_FMT_YUV420P;
    // hard enc
    enum AVHWDeviceType type_ = AV_HWDEVICE_TYPE_NONE;
    bool is_hard_enc_ = false;
    enum AVCodecID decodec_id_;

    std::list<cv::Mat> bgr_frames_;
    std::list<AVFrame *> yuv_frames_;
    pthread_mutex_t bgr_mutex_;
    pthread_cond_t bgr_cond_;
    pthread_mutex_t yuv_mutex_;
    pthread_cond_t yuv_cond_;
    pthread_t scale_id_;
    pthread_t encode_id_;

    bool abort_;
    uint64_t nframe_counter_;
    struct timeval time_now_;
    struct timeval time_pre_;
    uint64_t time_ts_accum_;

    struct timeval time_now_1_;
    struct timeval time_pre_1_;
    int time_inited_;
    int now_frames_;
    int pre_frames_;
};
#endif
#ifdef USE_FFMPEG_SOFT
class HardVideoEncoder
{
public:
    HardVideoEncoder();
    ~HardVideoEncoder();
    int AddVideoFrame(cv::Mat bgr_frame);
    int Init(cv::Mat init_frame, int fps);
    void SetDataCallback(EncDataCallListner *call_func);

private:
    int SoftEncInit(int width, int height, int fps);
    static void *VideoScaleThread(void *arg);
    static void *VideoEncThread(void *arg);

private:
    EncDataCallListner *callback_ = NULL;
    AVCodecContext *h264_codec_ctx_ = NULL;
    AVCodec *h264_codec_ = NULL;
    SwsContext *sws_context_ = NULL;
    enum AVPixelFormat sw_pix_format_ = AV_PIX_FMT_YUV420P;
    // hard enc
    enum AVHWDeviceType type_ = AV_HWDEVICE_TYPE_NONE;
    bool is_hard_enc_ = false;
    enum AVCodecID decodec_id_;

    std::list<cv::Mat> bgr_frames_;
    std::list<AVFrame *> yuv_frames_;
    pthread_mutex_t bgr_mutex_;
    pthread_cond_t bgr_cond_;
    pthread_mutex_t yuv_mutex_;
    pthread_cond_t yuv_cond_;
    pthread_t scale_id_;
    pthread_t encode_id_;

    bool abort_;
    uint64_t nframe_counter_;
    struct timeval time_now_;
    struct timeval time_pre_;
    uint64_t time_ts_accum_;

    struct timeval time_now_1_;
    struct timeval time_pre_1_;
    int time_inited_;
    int now_frames_;
    int pre_frames_;
};
#endif
#ifdef USE_DVPP_MPI
#include <acl.h>
#include <acl_rt.h>
#include <hi_dvpp.h>
#include "sample_comm.h"
#include "sample_api.h"
#include "sample_encoder_manage.h"
// w-Integer multiples of 16 
// h-Integer multiples of 2
void vencStreamOut(uint32_t channelId, void* buffer, void *arg);
class HardVideoEncoder
{
public:
    HardVideoEncoder();
    ~HardVideoEncoder();
    int AddVideoFrame(cv::Mat bgr_frame);
    void SetDevice(int device_id);
    int Init(cv::Mat init_frame, int fps);
    void SetDataCallback(EncDataCallListner *call_func);

private:
    friend void vencStreamOut(uint32_t channelId, void* buffer, void *arg);
    static void *VideoScaleThread(void *arg);
    static void *VideoEncThread(void *arg);
    void *GetColorAddr();
    void PutColorAddr(void *addr);
    void InitEncParams(VencParam* encParam);
private:
    int32_t device_id_ = 0;
    EncDataCallListner *callback_ = NULL;

    std::list<cv::Mat> bgr_frames_;
    std::list<void *> yuv_frames_;
    pthread_mutex_t bgr_mutex_;
    pthread_cond_t bgr_cond_;
    pthread_mutex_t yuv_mutex_;
    pthread_cond_t yuv_cond_;
    bool abort_;
    pthread_t scale_id_;
    pthread_t encode_id_;
    // color convert
    // mem pool
    uint32_t pool_num_ = 10;
    uint32_t out_buffer_size_ = 0;
    std::list<void*> out_buffer_pool_;
    pthread_mutex_t out_buffer_pool_mutex_;
    pthread_cond_t out_buffer_pool_cond_;
    int width_;
    int height_;
    int fps_;
    // color input
    void * in_img_buffer_ = NULL;
    uint32_t in_img_buffer_size_;
    hi_pixel_format in_format_ = HI_PIXEL_FORMAT_BGR_888;
    // color output
    hi_pixel_format out_format_ = HI_PIXEL_FORMAT_YUV_SEMIPLANAR_420;
    // color context
    hi_vpc_chn channel_id_color_;
    hi_vpc_pic_info input_pic_;
    hi_vpc_pic_info output_pic_;

    // dvpp enc
    int32_t enc_channel_;
    int32_t codec_type_;
    int32_t bit_rate_ = 0;
    IHWCODEC_HANDLE enc_handle_;
    VencParam enc_param_;

    unsigned char *image_ptr_ = NULL;
    uint64_t image_ptr_size_ = 1024 * 1024 * 2;

    uint64_t nframe_counter_;
    uint64_t nframe_counter_recv_;
    struct timeval time_now_;
    struct timeval time_pre_;
    uint64_t time_ts_accum_;

    struct timeval time_now_1_;
    struct timeval time_pre_1_;
    int time_inited_;
    int now_frames_;
    int pre_frames_;
};
#endif

#endif
