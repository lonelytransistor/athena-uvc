#ifndef __UVCFB_H__
#define __UVCFB_H__

#include "UVC.h"

#define FB_NAME "/dev/fb0"

using namespace std;

//width: 1404
//height: 1872
//2560x1440

class UVCfb : UVC {
private:
    // Framebuffer info:
    int m_fb_fd = 0;
    int m_fb_width = 0;
    int m_fb_height = 0;
    int m_nbands = 0;
    int m_pixelFormat = 0;
    int m_jpegSubsamp = 0;
    // MMAPped framebuffer:
    void* m_fb = NULL;
    u_long m_fb_size = 0;
    // Buffers:
    std::vector<struct buffer> m_buf;
    uint8_t* m_jpeg_buffer[2] = {NULL, NULL};
    long unsigned int m_jpeg_buffer_size[2] = {0, 0};
    uint32_t m_jpeg_buffer_ix = 0;
    uint32_t m_jpeg_buffer_ix_copied = 0;
    // Transcoder:
    bool m_transcoder_running = false;
    std::thread m_transcoder_thread;
    std::mutex m_mutex;
private:
    void video_qbuf();
    void video_reqbufs(uint8_t nbufs);
    
    void convertFbTo8Bit(uint8_t* buffer, uint32_t size);
    uint16_t fletcher16(uint8_t *data, size_t len);
    void transcoder();
    void fill_buffer(struct v4l2_buffer* ubuf);
public:
    UVCfb(char *devname, uint16_t maxpkt=1024, uint8_t nbufs=2);
    ~UVCfb();
    void loop();
};

#endif //__UVCFB_H__
