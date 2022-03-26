CROSS_COMPILE   ?= arm-remarkable-linux-gnueabihf-
ARCH            ?= arm
KERNEL_DIR      ?= /usr/src/linux

CC              := $(CROSS_COMPILE)g++
LD		:= $(CROSS_COMPILE)ld
KERNEL_INCLUDE  := -I$(KERNEL_DIR)/include -I$(KERNEL_DIR)/arch/$(ARCH)/include -I./libjpeg-turbo/ -I./Ne10/inc/
CPPFLAGS        := $(KERNEL_INCLUDE)
LDFLAGS         := -L./libjpeg-turbo/ -ljpeg -lturbojpeg -lpthread -L./Ne10/ -lne10

all: uvc

clean:
	rm -f *.o
	rm -f uvc

%.o: %.cpp
	$(CC) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

uvc: UVC.o UVC_video.o UVC_event.o UVCfb.o UVCfb_transcoder.o UVCfb_buffers.o main.o
	$(CC) -o $@ $^ $(LDFLAGS)
