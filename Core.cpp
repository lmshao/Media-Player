//
// Created by Liming Shao on 2018/7/8.
//

#include "Core.h"

#include "Utils.h"
#include "Control.h"

#include <list>
using namespace std;

static  Uint32  gAudioLen;
static  Uint8  *gAudioPos;
int gAudioBuffSize = 0;
uint8_t *gAudioBuff = nullptr;
static struct SwrContext *gAudioSwrCtx = nullptr;
static AVCodecContext *gAudioCodexCtx = nullptr;
static list<AVPacket> gAudioPacketList;

Core::Core():mFormatCtx(nullptr),mSDLEnv(nullptr),
             mAudioInfo(nullptr),mVideoInfo(nullptr){
    mSDLEnv = new SDL_Env;
}

Core::~Core() {
    delete mSDLEnv;
    LOG("~CORE()\n");
}

bool Core::openMediaFile(const char *file) {

    if ((avformat_open_input(&mFormatCtx, file, nullptr, nullptr)) < 0) {
        LOGE("Failed to open input file [%s]\n", file);
        return false;
    }

    if ((avformat_find_stream_info(mFormatCtx, nullptr)) < 0) {
        LOGE("Failed to retrieve input stream information");
        return false;
    }

    av_dump_format(mFormatCtx, 0, file, 0);

    return true;
}


bool Core::preprocessStream() {
    int videoIndex, audioIndex;
    AVCodecContext *vCodecCtx = nullptr;
    AVCodecContext *aCodecCtx = nullptr;

    // Find video stream in the file
    videoIndex = av_find_best_stream(mFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoIndex >= 0) {

        vCodecCtx = openCodecContext(videoIndex);
        if (!vCodecCtx){
            LOGE("openCodecContext error.\n");
            return false;
        }

        mVideoInfo = new CodecInfo(videoIndex, vCodecCtx);
    }

    // Find audio stream in the file
    audioIndex = av_find_best_stream(mFormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audioIndex >= 0) {

        aCodecCtx = openCodecContext(audioIndex);
        if (!vCodecCtx){
            LOGE("openCodecContext error.\n");
            return false;
        }

        mAudioInfo = new CodecInfo(audioIndex, aCodecCtx);
    }

    if (videoIndex < 0 && audioIndex < 0) {
        LOGE("Failed to find video or audio stream in file.");
        return false;
    }

    return true;
}

AVCodecContext *Core::openCodecContext(int index) {
    AVCodecContext *codecCtx = nullptr;
    AVCodec *dec = nullptr;

    dec = avcodec_find_decoder(mFormatCtx->streams[index]->codecpar->codec_id);
    if (!dec) {
        LOGE("Failed to find codec!\n");
        return nullptr;
    }

    // Allocate a codec context for the decoder
    codecCtx = avcodec_alloc_context3(dec);
    if (!codecCtx) {
        LOGE("Failed to allocate the codec context\n");
        return nullptr;
    }

    if (avcodec_parameters_to_context(codecCtx, mFormatCtx->streams[index]->codecpar) < 0) {
        LOGE("Failed to copy codec parameters to decoder context!\n");
        return nullptr;
    }

    // Initialize the AVCodecContext to use the given Codec
    if (avcodec_open2(codecCtx, dec, nullptr) < 0) {
        LOGE("Failed to open codec\n");
        return nullptr;
    }

    return codecCtx;
}

bool Core::initSDL() {

    AVCodecContext *codec = nullptr;
    codec = mVideoInfo->codec;
    if (!codec) {
        LOGE("NO video info.\n");
        return true;
    }

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);

    if (mVideoInfo) {
        //  flags: 0 / SDL_WINDOW_RESIZABLE / SDL_WINDOW_FULLSCREEN or others
        mSDLEnv->window = SDL_CreateWindow("Gemini Player", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                           codec->width, codec->height, SDL_WINDOW_RESIZABLE);
        if (!mSDLEnv->window) {
            LOGE("Failed to create windows - %s\n", SDL_GetError());
            return false;
        }

        //  flags: 0 / SDL_RENDERER_ACCELERATED / SDL_RENDERER_ACCELERATED or others
        mSDLEnv->renderer = SDL_CreateRenderer(mSDLEnv->window, -1, 0);
        if (!mSDLEnv->renderer) {
            LOGE("Failed to create renderer - %s\n", SDL_GetError());
            return false;
        }

        mSDLEnv->texture = SDL_CreateTexture(mSDLEnv->renderer, SDL_PIXELFORMAT_YV12,
                                             SDL_TEXTUREACCESS_STREAMING, codec->width, codec->height);
        if (!mSDLEnv->texture) {
            LOGE("Failed to create texture - %s\n", SDL_GetError());
            return false;
        }

        mSDLEnv->rectangle->x = 0;
        mSDLEnv->rectangle->y = 0;
        mSDLEnv->rectangle->w = codec->width;
        mSDLEnv->rectangle->h = codec->height;
    }

    if (mAudioInfo) {
        SDL_AudioSpec audioSpec;
        audioSpec.freq = mAudioInfo->codec->sample_rate;
        audioSpec.format = AUDIO_S16SYS; //s32le AV_SAMPLE_FMT_FLTP
        audioSpec.channels = (Uint8)mAudioInfo->codec->channels;
        audioSpec.silence = 0;
        audioSpec.samples = (Uint16)mAudioInfo->codec->frame_size;
        audioSpec.callback = sdlAudioCallback;
        audioSpec.userdata = mAudioInfo->codec;
        if (SDL_OpenAudio(&audioSpec, nullptr) < 0){
            LOGE("SDL_OpenAudio error.\n");
            return false;
        }
    }

    return true;
}

bool Core::playMedia() {
    int time = 1;
    AVPacket packet;

    AVFrame *srcFrame = nullptr, *dstFrame = nullptr; // for video
    struct SwsContext *videoSwsCtx = nullptr;

    AVFrame *audioFrame = nullptr; // for audio
    uint8_t *audioBuff = nullptr;
    int audioBuffSize = 0;
    struct SwrContext *audioSwrCtx = nullptr;

    if (mVideoInfo) {
        srcFrame = av_frame_alloc();
        dstFrame = av_frame_alloc();
        allocImage(dstFrame);

        // Initialize an SwsContext for software scaling (image format conversion)
        videoSwsCtx = sws_getContext(
                mVideoInfo->codec->width, mVideoInfo->codec->height, mVideoInfo->codec->pix_fmt,
                mVideoInfo->codec->width, mVideoInfo->codec->height, AV_PIX_FMT_YUV420P,
                SWS_BILINEAR, nullptr, nullptr, nullptr);

        time = 1000 * mFormatCtx->streams[mVideoInfo->index]->avg_frame_rate.den
               / mFormatCtx->streams[mVideoInfo->index]->avg_frame_rate.num;
    }

    if (mAudioInfo) {
        audioFrame = av_frame_alloc();

        audioSwrCtx = swr_alloc_set_opts(audioSwrCtx,
                                         AV_CH_LAYOUT_STEREO,  /* output channel layout (AV_CH_LAYOUT_*) */
                                         AV_SAMPLE_FMT_S16,    /* output sample format (AV_SAMPLE_FMT_*) */
                                         mAudioInfo->codec->sample_rate,  /* output sample rate */
                                         av_get_default_channel_layout(mAudioInfo->codec->channels),  /* input channel layout (AV_CH_LAYOUT_*) */
                                         mAudioInfo->codec->sample_fmt,   /* input sample format (AV_SAMPLE_FMT_*) */
                                         mAudioInfo->codec->sample_rate,  /* input sample rate */
                                         0, nullptr);
        swr_init(audioSwrCtx);

        audioBuffSize = av_samples_get_buffer_size(nullptr, mAudioInfo->codec->channels, mAudioInfo->codec->frame_size, AV_SAMPLE_FMT_S16, 1);
        audioBuff = (uint8_t *)av_malloc((size_t)audioBuffSize);

        gAudioSwrCtx = audioSwrCtx;
        gAudioCodexCtx = mAudioInfo->codec;
        gAudioBuff = audioBuff;
        gAudioBuffSize = audioBuffSize;

        SDL_PauseAudio(0);  //play audio
    }

    while (av_read_frame(mFormatCtx, &packet) >= 0 && Control::Instance()->isRunning()) {
        Control::Instance()->handleEvents();
        while (Control::Instance()->isPause()) {
            Control::Instance()->handleEvents();
            SDL_Delay(time);
        }

        if (mVideoInfo && packet.stream_index == mVideoInfo->index) {
            LOGD("video ++++\n");
            // Decode the video frame
            avcodec_send_packet(mVideoInfo->codec, &packet);
            int ret = avcodec_receive_frame(mVideoInfo->codec, srcFrame);
            if (ret) continue;

            // Convert the image from its native format to YUV420P
            sws_scale(videoSwsCtx, (uint8_t const * const *)srcFrame->data, srcFrame->linesize,
                      0, mVideoInfo->codec->height, dstFrame->data, dstFrame->linesize);

            displayImage(dstFrame);
            SDL_Delay(time);

            av_packet_unref(&packet);
        }

        if (mAudioInfo && packet.stream_index == mAudioInfo->index){
            LOGD("audio ---\n");
            gAudioPacketList.push_back(packet);
//            SDL_Delay(10);
        }
    }

    if (mVideoInfo) {
        av_frame_free(&srcFrame);
        av_freep(&dstFrame->data[0]);
        av_frame_free(&dstFrame);

        if (videoSwsCtx)
            sws_freeContext(videoSwsCtx);
    }

    if (mAudioInfo) {
        av_frame_free(&audioFrame);
        av_free(audioBuff);

        if (audioSwrCtx)
            swr_free(&audioSwrCtx);
    }

    return true;
}

bool Core::allocImage(AVFrame *image) {
    int ret = 0;
    image->format = AV_PIX_FMT_YUV420P;
    image->width = mVideoInfo->codec->width;
    image->height = mVideoInfo->codec->height;

    // Allocate an image, and fill pointers and linesizes accordingly.
    ret = av_image_alloc(image->data, image->linesize, image->width,
            image->height, (AVPixelFormat)image->format, 32);

    return (ret >= 0);
}

void Core::displayImage(AVFrame *data){

    SDL_UpdateYUVTexture(mSDLEnv->texture, mSDLEnv->rectangle,
                         data->data[0], data->linesize[0],
                         data->data[1], data->linesize[1],
                         data->data[2], data->linesize[2]);

    SDL_RenderClear(mSDLEnv->renderer);

    // Set apative size image
     SDL_RenderCopy(mSDLEnv->renderer, mSDLEnv->texture, nullptr, nullptr);

//    SDL_RenderCopy(mSDLEnv->renderer, mSDLEnv->texture, NULL, mSDLEnv->rectangle);

    SDL_RenderPresent(mSDLEnv->renderer);
}

int Core::decodeAudioPacket(){
    if (!gAudioCodexCtx || !gAudioSwrCtx || !gAudioBuff)
        return -1;

    if (gAudioPacketList.empty())
        return -1;
    AVPacket packet = gAudioPacketList.front();
    gAudioPacketList.pop_front();

    AVFrame *audioFrame = av_frame_alloc();

    avcodec_send_packet(gAudioCodexCtx, &packet);
    int ret = avcodec_receive_frame(gAudioCodexCtx, audioFrame);
    if (ret) return 1;

    int dstNbSample = (int)av_rescale_rnd(audioFrame->nb_samples,
                                          gAudioCodexCtx->sample_rate, gAudioCodexCtx->sample_rate, AV_ROUND_UP);
    swr_convert(gAudioSwrCtx, &gAudioBuff, dstNbSample, (const uint8_t **)audioFrame->data, audioFrame->nb_samples);

    gAudioLen = (Uint32)gAudioBuffSize;
    gAudioPos = gAudioBuff;

    av_packet_unref(&packet);
    av_frame_free(&audioFrame);
    return 0;
}

void Core::sdlAudioCallback(void *userdata, Uint8 *stream, int len) {
    SDL_memset(stream, 0, len);  // VERY IMPORTANT

    if (gAudioLen <= 0) {
        decodeAudioPacket();
    }

    if (gAudioLen <= 0) {
        return; // No data
    }

    len = (len > gAudioLen ? gAudioLen : len);
    SDL_MixAudio(stream, gAudioPos, len, SDL_MIX_MAXVOLUME);
    gAudioPos += len;
    gAudioLen -= len;
}


void Core::cleanUp() {
    Control::Destroy();

    if (mVideoInfo) {
        avcodec_close(mVideoInfo->codec);
        delete mVideoInfo;
    }

    if (mAudioInfo) {
        avcodec_close(mAudioInfo->codec);
        delete mAudioInfo;
    }

    avformat_close_input(&mFormatCtx);

    SDL_DestroyWindow(mSDLEnv->window);
    SDL_DestroyRenderer(mSDLEnv->renderer);
    SDL_Quit();
    LOG("Clean Up.\n");
}
