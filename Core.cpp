//
// Created by Liming Shao on 2018/7/8.
//

#include "Core.h"

#include "Utils.h"
#include "Control.h"

Core::Core():mFormatCtx(nullptr),mSDLEnv(nullptr){
    mSDLEnv = new SDL_Env;
}

Core::~Core() {
    delete mSDLEnv;
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


bool Core::openCodecContext() {
    int videoIndex, audioIndex;
    AVCodecContext *vCodecCtx = nullptr;
    AVCodecContext *aCodecCtx = nullptr;

    // Find video stream in the file
    videoIndex = av_find_best_stream(mFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoIndex >= 0) {

        AVCodec *dec = avcodec_find_decoder(mFormatCtx->streams[videoIndex]->codecpar->codec_id);
        if (!dec) {
            printf("Failed to find codec!\n");
            return true;
        }

        // Allocate a codec context for the decoder
        vCodecCtx = avcodec_alloc_context3(dec);
        if (!vCodecCtx) {
            LOGE("Failed to allocate the codec context\n");
            return false;
        }

        if (avcodec_parameters_to_context(vCodecCtx, mFormatCtx->streams[videoIndex]->codecpar) < 0) {
            LOGE("Failed to copy codec parameters to decoder context!\n");
            return false;
        }

        // Initialize the AVCodecContext to use the given Codec
        if (avcodec_open2(vCodecCtx, dec, NULL) < 0) {
            LOGE("Failed to open codec\n");
            return true;
        }

        CodecInfo codecInfo = {"video", videoIndex, vCodecCtx};
        mCodecArray.push_back(codecInfo);
    }

    // Find audio stream in the file
    audioIndex = av_find_best_stream(mFormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audioIndex >= 0) {
        LOGD("file has a audio track\n");

        CodecInfo codecInfo = {"audio", audioIndex, aCodecCtx};
        mCodecArray.push_back(codecInfo);
    }

    if (videoIndex < 0 && audioIndex < 0) {
        LOGE("Failed to find video or audio stream in file.");
        return false;
    }

    return true;
}

bool Core::initSDL() {

    AVCodecContext *codec;

    CodecInfo *cinfo = getCodecInfo("video");
    if (!cinfo) {
        LOGE("No video CODEC INFO.\n");
        return false;
    }

    codec = cinfo->codec;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        LOGE("Failed to initialize SDL - %s\n", SDL_GetError());
        return false;
    }

    mSDLEnv->window = SDL_CreateWindow("Gemini Player", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       codec->width, codec->height, 0);
    if (!mSDLEnv->window) {
        printf("Failed to create windows - %s\n", SDL_GetError());
        return false;
    }

    mSDLEnv->renderer = SDL_CreateRenderer(mSDLEnv->window, -1, 0);
    if (!mSDLEnv->renderer) {
        printf("Failed to create renderer - %s\n", SDL_GetError());
        return false;
    }

    mSDLEnv->texture = SDL_CreateTexture(mSDLEnv->renderer, SDL_PIXELFORMAT_YV12,
                                 SDL_TEXTUREACCESS_STREAMING, codec->width, codec->height);
    if (!mSDLEnv->texture) {
        printf("Failed to create texture - %s\n", SDL_GetError());
        return false;
    }

    mSDLEnv->rectangle->x = 0;
    mSDLEnv->rectangle->y = 0;
    mSDLEnv->rectangle->w = codec->width;
    mSDLEnv->rectangle->h = codec->height;

    return true;
}

bool Core::playVideo() {
    int index;
    AVCodecContext *codec;

    CodecInfo *cinfo = getCodecInfo("video");
    if (!cinfo) {
        LOGE("No video CODEC INFO.\n");
        return false;
    }

    index = cinfo->index;
    codec = cinfo->codec;

    int time = 1000 * mFormatCtx->streams[index]->avg_frame_rate.den
               / mFormatCtx->streams[index]->avg_frame_rate.num;

    AVPacket packet;
    AVFrame *srcFrame = av_frame_alloc();
    AVFrame *dstFrame = av_frame_alloc();;
    allocImage(dstFrame);

    // Initialize an SwsContext for software scaling
    struct SwsContext *sws_ctx = sws_getContext(
            codec->width, codec->height, codec->pix_fmt,
            codec->width, codec->height, AV_PIX_FMT_YUV420P,
            SWS_BILINEAR, NULL, NULL, NULL);

    while (av_read_frame(mFormatCtx, &packet) >= 0 && Control::Instance()->isRunning() ) {
        Control::Instance()->handleEvents();
        while (Control::Instance()->isPause()) {
            Control::Instance()->handleEvents();
            SDL_Delay(time);
        }

        // Is this a packet from the video stream?
        if (packet.stream_index == index) {

            // Decode the video frame
            avcodec_send_packet(codec, &packet);
            int ret = avcodec_receive_frame(codec, srcFrame);
            if (ret) continue;

            // Convert the image from its native format to RGB
            sws_scale(sws_ctx, (uint8_t const * const *)srcFrame->data,
                      srcFrame->linesize, 0, codec->height,
                      dstFrame->data, dstFrame->linesize);

            SDL_UpdateYUVTexture(mSDLEnv->texture, mSDLEnv->rectangle,
                                 dstFrame->data[0], dstFrame->linesize[0],
                                 dstFrame->data[1], dstFrame->linesize[1],
                                 dstFrame->data[2], dstFrame->linesize[2]
            );

            render();
            SDL_Delay(time);

            // Free the packet that was allocated by av_read_frame
            av_packet_unref(&packet);
        }
    }
    av_freep(&dstFrame->data[0]);
    av_frame_free(&srcFrame);
    av_frame_free(&dstFrame);

    return false;
}

bool Core::allocImage(AVFrame *image) {
    AVCodecContext *codec;

    CodecInfo *cinfo = getCodecInfo("video");
    if (!cinfo) {
        LOGE("No video CODEC INFO.\n");
        return false;
    }

    codec = cinfo->codec;

    int ret = 0;
    image->format = AV_PIX_FMT_YUV420P;
    image->width = codec->width;
    image->height = codec->height;

    // Allocate an image, and fill pointers and linesizes accordingly.
    ret = av_image_alloc(image->data, image->linesize, image->width,
            image->height, (AVPixelFormat)image->format, 32);

    return (ret >= 0);
}

void Core::render() {
    SDL_RenderClear(mSDLEnv->renderer);

    SDL_RenderCopy(mSDLEnv->renderer, mSDLEnv->texture, NULL, mSDLEnv->rectangle);

    // Set apative size image
    // SDL_RenderCopy(mSDLEnv->renderer, mSDLEnv->texture, NULL, NULL);

    SDL_RenderPresent(mSDLEnv->renderer);
}

Core::CodecInfo *Core::getCodecInfo(string type) {
    for (int i = 0; i < mCodecArray.size(); ++i) {
        if (mCodecArray[i].type == type) {
            return &mCodecArray[i];
        }
    }
    return nullptr;
}





