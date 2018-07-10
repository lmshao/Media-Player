//
// Created by Liming Shao on 2018/7/8.
//

#include "Control.h"

static Control *gInstance = nullptr;

Control *Control::Instance() {
    if (!gInstance) {
        gInstance = new Control();
    }
    return gInstance;
}

void Control::Destroy() {
    if (gInstance) {
        delete gInstance;
    }
}

Control::Control() {
    mRunning = true;
    mPause = false;
}

Control::~Control() {

}

void Control::handleEvents() {
    SDL_Event event;
    if (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                mRunning = false;
                break;
            case SDL_MOUSEBUTTONUP:
                mPause = !mPause;
                break;
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_SPACE)
                    mPause = !mPause;
                break;
            default:
                break;
        }
    }
}

