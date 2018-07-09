//
// Created by Liming Shao on 2018/7/8.
//

#ifndef MEDIA_PLAYER_UTILS_H
#define MEDIA_PLAYER_UTILS_H



#include <stdio.h>

#define LOG(fmt...)   \
    do {\
        printf("[%s:%d]:", __FUNCTION__, __LINE__);\
        printf(fmt);\
    }while(0)

#define LOGD(fmt...)   \
    do {\
        printf("\033[32m");\
        printf(fmt);\
        printf("\033[0m");\
    }while(0)

#define LOGE(fmt...)   \
    do {\
        printf("\033[31m");\
        printf("[%s:%s:%d]:", __FILE__, __FUNCTION__, __LINE__);\
        printf(fmt);\
        printf("\033[0m");\
    }while(0)

#endif //MEDIA_PLAYER_UTILS_H
