#ifndef __UVC_H__
#define __UVC_H__

#include "uvc_gadget.h"
#include <iostream>
#include <string.h>
#include <errno.h>
#include <exception>
#include <linux/fb.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdexcept>
#include <vector>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <linux/usb/ch9.h>
#include <linux/usb/video.h>
#include <linux/videodev2.h>
#include <thread>
#include <mutex>
#include <chrono>
#include <jpeglib.h>
#include <turbojpeg.h>

//#define _DEBUG_ENA_
#ifdef _DEBUG_ENA_
#define DEBUG(msg) std::cerr<<"\033[1;33m"<<__FILE__<<"\033[0m"<<":"<<"\033[1;33m"<<__LINE__<<"\t"<<__FUNCTION__<<" \033[0m\t"<<msg<<"."<<std::endl
#else
#define DEBUG(msg)
#endif
#define LOG(msg) std::cout<<"\033[1;32m"<<__FILE__<<"\033[0m"<<":"<<"\033[1;32m"<<__LINE__<<"\t"<<__FUNCTION__<<" \033[0m\t"<<msg<<"."<<std::endl
#define ERR(msg) std::cerr<<"\033[1;31m"<<__FILE__<<"\033[0m"<<":"<<"\033[1;31m"<<__LINE__<<"\t"<<__FUNCTION__<<" \033[0m\t"<<msg<<"."<<std::endl
#define pixfmtstr(x) (char)(x&0xff)<<(char)((x>>8)&0xff)<<(char)((x>>16)&0xff)<<(char)((x>>24)&0xff)

#define PU_BRIGHTNESS_MIN_VAL 0
#define PU_BRIGHTNESS_MAX_VAL 255
#define PU_BRIGHTNESS_STEP_SIZE 1
#define PU_BRIGHTNESS_DEFAULT_VAL 55

#define REQ_SUCCESS 0x00
#define REQ_UNHANDLED 0x06
#define REQ_UNSUPPORTED 0x07

struct uvc_format_info {
    unsigned int fcc;
    unsigned int width;
    unsigned int height;
    unsigned int interval;
    unsigned int i_frame;
    unsigned int i_format;
};
struct buffer {
    struct v4l2_buffer buf;
    uint8_t *start;
    size_t length;
};

class UVCFormats {
private:
    unsigned int formats_num = 0;
    std::vector<struct uvc_format_info> formats;
public:
    struct uvc_format_info operator[](std::size_t idx) {
        return get(idx, idx==-1?-1:0, idx==-1?-1:0);
    }
    struct uvc_format_info get(int16_t i_format, int16_t i_frame, unsigned int interval) {
        uint8_t ix = -1; uint8_t ix2 = 0;
        if (i_format < 0)
            i_format = i_format%formats_num;
        if (i_format > formats_num)
            i_format = formats_num-1;
        for (struct uvc_format_info format : formats) {
            if (format.i_format==i_format) {
                ix = ix2;
                if (format.i_frame==i_frame && format.interval==interval)
                    break;
            }
            ix2++;
        }
        if (ix == -1)
            throw std::invalid_argument("Format list is empty");
        return formats[ix];
    }
    void add(struct uvc_format_info new_format) {
        add(new_format.fcc, new_format.width, new_format.height, new_format.interval);
    }
    void add(unsigned int fcc, unsigned int width, unsigned int height, unsigned int interval) {
        struct uvc_format_info new_format;
        uint8_t ix = 255; uint8_t ix2 = 0;
        
        for (struct uvc_format_info format : formats) {
            if (format.fcc == fcc) {
                ix = ix2;
            }
            ix2++;
        }
        new_format.fcc = fcc;
        new_format.width = width;
        new_format.height = height;
        new_format.interval = interval;
        if (ix != 255) {
            new_format.i_frame = formats[ix].i_frame+1;
            new_format.i_format = formats[ix].i_format;
        } else {
            new_format.i_frame = 0;
            new_format.i_format = (formats_num++);
            ix = 0;
        }
        formats.insert(formats.begin()+ix, new_format);
    }
    UVCFormats() {}
};
class UVC {
protected:
    // State
    int g_uvc_fd;
    bool g_is_streaming;
    // UVC control
    struct uvc_streaming_control g_probe;
    struct uvc_streaming_control g_commit;
    int g_control;
    struct uvc_request_data g_request_error_code;
    uint8_t g_brightness_val;
    // Video format
    unsigned int g_nbufs;
    unsigned int g_fcc;
    unsigned int g_width;
    unsigned int g_height;
    // Config
    bool g_bulk;
    unsigned int g_payload_size;
    int g_max_packet_size;
    // UVC buffer queue and dequeue counters
    unsigned long long int g_qbuf_count;
    unsigned long long int g_dqbuf_count;
    UVCFormats g_formats;
private:
    void video_set_format(struct uvc_format_info format);
    void video_enable_stream(bool enable);
    void video_process();
    
    void fill_streaming_control(struct uvc_streaming_control *ctrl, struct uvc_format_info format);
    
    void events_process_standard(struct usb_ctrlrequest *ctrl, struct uvc_request_data *resp);
    void events_process_control(uint8_t req, uint8_t cs, uint8_t entity_id, uint8_t len, struct uvc_request_data *resp);
    void events_process_streaming(uint8_t req, uint8_t cs, struct uvc_request_data *resp);
    void events_process_setup(struct usb_ctrlrequest *ctrl, struct uvc_request_data *resp);
    void events_process_control_data(uint8_t cs, uint8_t entity_id, struct uvc_request_data *data);
    void events_process_data(struct uvc_request_data *data);
    void events_process();
    
    inline void SET_REQ_ERROR(uint8_t e) {
        g_request_error_code.data[0] = e;
        g_request_error_code.length = 1;
    }
protected:
    virtual void video_qbuf()=0;
    virtual void video_reqbufs(uint8_t nbufs)=0;
    virtual void fill_buffer(struct v4l2_buffer* ubuf)=0;
    void set_formats(UVCFormats formats);
    void loop_main();
public:
    UVC(char *devname, uint16_t maxpkt=1024, uint8_t nbufs=3);
    ~UVC();
};

#endif //__UVC_H__
