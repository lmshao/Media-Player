CC = cc
LD = cc
SRCS = $(wildcard *.cpp)
OBJS = $(patsubst %.cpp, %.o, $(SRCS))

CFLAGS = -Wall -O2
#INCLUDE = -I./include

LIB = -lSDL2 -lSDL2main

LIB += -lavcodec -lavdevice -lavfilter -lavformat -lavutil -lswscale -lswresample

LIB += -lstdc++

#OS = $(shell uname -s | tr [A-Z] [a-z])
#$(info OS=$(OS))

#ifeq ($(OS), darwin)
#endif

#ifeq ($(OS), linux)
#endif

TARGET = Media-Player

.PHONY:all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(LD) -o $@ $^ $(LIB)
	@echo -e "\033[0m\033[1A"

%.o:%.cpp
	@echo -e "\033[32m\033[1A"
	$(CC) -c $^ $(CFLAGS)

clean:
	@echo -e "\033[32m\033[1A"
	rm -f $(OBJS) $(TARGET)
	@echo -e "\033[0m\033[1A"
