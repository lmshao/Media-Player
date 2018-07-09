cmake_minimum_required(VERSION 3.10)
project(Media-Player)

SET(CMAKE_CXX_STANDARD 11)

#INCLUDE_DIRECTORIES(include)
#LINK_DIRECTORIES(libs/mac)

LINK_LIBRARIES(SDL2 SDL2main)
LINK_LIBRARIES(avcodec avdevice avfilter avformat avutil swscale swresample postproc)

SET(SRC_LIST main.cpp Core.cpp Core.h Control.cpp Control.h Utils.cpp Utils.h)

ADD_EXECUTABLE(Media-Player ${SRC_LIST})