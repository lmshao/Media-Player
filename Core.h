//
// Created by Liming Shao on 2018/7/8.
//

#ifndef MEDIA_PLAYER_CORE_H
#define MEDIA_PLAYER_CORE_H

#include "SDL2/SDL.h"
#include "SDL2/SDL_thread.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
}

class SDL_Env{
public:
    SDL_Env() : window(nullptr), renderer(nullptr), texture(nullptr), rectangle(nullptr) {
        rectangle = new SDL_Rect;
    };

    virtual ~SDL_Env() {
        delete rectangle;
    }

    SDL_Window      *window;
    SDL_Renderer    *renderer;
    SDL_Texture     *texture;
    SDL_Rect        *rectangle;
};


class Core {
public:
    Core();
    virtual ~Core();

    bool openMediaFile(const char* file);

    bool preprocessStream();

    AVCodecContext* openCodecContext(int index);

    bool initSDL();

    bool playMedia();

    void cleanUp();

private:
    bool allocImage(AVFrame *image);
    void displayImage(AVFrame *data);
    static void sdlAudioCallback(void *userdata, Uint8 *stream, int len);

    AVFormatContext *mFormatCtx;
    SDL_Env         *mSDLEnv;

    struct CodecInfo{
        CodecInfo(int index, AVCodecContext *codec):
                index(index), codec(codec) {}

        int index;
        AVCodecContext *codec;
    };

    CodecInfo *mAudioInfo, *mVideoInfo;
};


#endif //MEDIA_PLAYER_CORE_H
