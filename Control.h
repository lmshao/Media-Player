//
// Created by Liming Shao on 2018/7/8.
//

#ifndef MEDIA_PLAYER_CONTROL_H
#define MEDIA_PLAYER_CONTROL_H

#include "Core.h"

class Control {
public:
    static Control* Instance();

    void handleEvents();

    bool isPause() const {
        return mPause;
    }

    bool isRunning() const {
        return mRunning;
    }

private:
    Control();
    ~Control();

    bool mPause;
    bool mRunning;
};


#endif //MEDIA_PLAYER_CONTROL_H
