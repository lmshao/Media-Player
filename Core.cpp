//
// Created by Liming Shao on 2018/7/8.
//

#include "Core.h"

#include "Utils.h"
#include "Control.h"

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

    return true;
}

bool Core::playVideo() {
    int index;
    AVCodecContext *codec;

    index = mVideoInfo->index;
    codec = mVideoInfo->codec;

    int time = 1000 * mFormatCtx->streams[index]->avg_frame_rate.den
               / mFormatCtx->streams[index]->avg_frame_rate.num;

    AVPacket packet;
    AVFrame *srcFrame = av_frame_alloc();
    AVFrame *dstFrame = av_frame_alloc();
    allocImage(dstFrame);

    AVFrame *audioFrame = av_frame_alloc();
    FILE *fd = fopen("out_s16le.pcm", "wb");  //just test: ffplay -f f32le -ac 2 -ar 44100 out.pcm

    // Initialize an SwsContext for software scaling
    struct SwsContext *sws_ctx = sws_getContext(
            codec->width, codec->height, codec->pix_fmt,
            codec->width, codec->height, AV_PIX_FMT_YUV420P,
            SWS_BILINEAR, nullptr, nullptr, nullptr);

    while (av_read_frame(mFormatCtx, &packet) >= 0 && Control::Instance()->isRunning()) {
        Control::Instance()->handleEvents();
        while (Control::Instance()->isPause()) {
            Control::Instance()->handleEvents();
            SDL_Delay(time);
        }

        // Is this a packet from the video stream?
        if (mVideoInfo && packet.stream_index == mVideoInfo->index) {

            // Decode the video frame
            avcodec_send_packet(mVideoInfo->codec, &packet);
            int ret = avcodec_receive_frame(mVideoInfo->codec, srcFrame);
            if (ret) continue;

            // Convert the image from its native format to YUV420P
            sws_scale(sws_ctx, (uint8_t const * const *)srcFrame->data,
                      srcFrame->linesize, 0, mVideoInfo->codec->height,
                      dstFrame->data, dstFrame->linesize);

            displayImage(dstFrame);
            SDL_Delay(time);

            // Free the packet that was allocated by av_read_frame
        }

        if (mAudioInfo && packet.stream_index == mAudioInfo->index){
            avcodec_send_packet(mAudioInfo->codec, &packet);
            int ret = avcodec_receive_frame(mAudioInfo->codec, audioFrame);
            if (ret) continue;

            int sampleBytes = av_get_bytes_per_sample(mAudioInfo->codec->sample_fmt);

            // write audio file for Planar sample format
            for (int i = 0; i < audioFrame->nb_samples; i++)
                for (int ch = 0; ch < audioFrame->channels; ch++)
                    fwrite(audioFrame->data[ch] + sampleBytes*i, 1, sampleBytes, fd);

        }

        av_packet_unref(&packet);

    }
    fclose(fd);
    av_frame_free(&audioFrame);

    av_freep(&dstFrame->data[0]);
    av_frame_free(&srcFrame);
    av_frame_free(&dstFrame);

    return true;
}

bool Core::playAudio() {
    return false;
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
