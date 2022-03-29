#ifndef __UVCFB_H__
#define __UVCFB_H__

#include "UVC.h"

#define FB_NAME "/dev/fb0"

using namespace std;

//width: 1404
//height: 1872
//2560x1440

#define vtrn8(a, b) \
{ \
uint8x8x2_t _transpose_tmp = vtrn_u8(a, b); \
(a) = _transpose_tmp.val[0]; \
(b) = _transpose_tmp.val[1]; \
}
#define vtrn16(a, b) \
{ \
uint16x4x2_t _transpose_tmp = vtrn_u16(vreinterpret_u16_u8(a), vreinterpret_u16_u8(b)); \
(a) = vreinterpret_u8_u16(_transpose_tmp.val[0]); \
(b) = vreinterpret_u8_u16(_transpose_tmp.val[1]); \
}
#define vtrn32(a, b) \
{ \
uint32x2x2_t _transpose_tmp = vtrn_u32(vreinterpret_u32_u8(a), vreinterpret_u32_u8(b)); \
(a) = vreinterpret_u8_u32(_transpose_tmp.val[0]); \
(b) = vreinterpret_u8_u32(_transpose_tmp.val[1]); \
}

class UVCfb : UVC {
private:
    // Framebuffer info:
    int m_fb_fd = 0;
    int m_fb_width = 0;
    int m_fb_height = 0;
    int m_nbands = 0;
    int m_pixelFormat = 0;
    int m_jpegSubsamp = 0;
    
    // Rescalers
    enum t_rescalers{RESIZE_BILINEAR_16BIT, RESIZE_BILINEAR_8BIT, RESIZE_BICUBIC_8BIT};
    
    // TurboJPEG constants
    const int m_jpegFlags = TJFLAG_NOREALLOC;
    const int m_jpegQuality = 90;
    
    // MMAPped framebuffer:
    void* m_fb = NULL;
    u_long m_fb_size = 0;
    
    // Buffers:
    std::vector<struct buffer> m_buf;
    uint8_t* m_tmp_buffer[2] = {NULL, NULL};
    long unsigned int m_tmp_buffer_size[2] = {0, 0};
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
    
    uint16_t getChecksum();
    int getJPEG(tjhandle tjCompress_ptr, uint8_t* out_buffer, long unsigned int* out_buffer_sz, struct uvc_format_info format);

    int rotateBuffer(uint8_t* in_buffer, uint8_t* out_buffer, uint16_t w, uint16_t h);

    int getFb(uint8_t* buffer);
    int getRotatedFb(uint8_t* out_buffer);
    int getResizedFb(uint8_t* buffer, uint16_t w, uint16_t h, uint16_t x_start=0, uint16_t y_start=0, uint8_t type=RESIZE_BILINEAR_16BIT);
    int getResizedFb_bicubic_8bit(uint8_t* buffer, uint16_t w, uint16_t h, uint16_t x_start=0, uint16_t y_start=0);
    int getResizedFb_bilinear_8bit(uint8_t* buffer, uint16_t w, uint16_t h, uint16_t x_start=0, uint16_t y_start=0);
    int getResizedFb_bilinear_16bit(uint8_t* buffer, uint16_t w, uint16_t h, uint16_t x_start=0, uint16_t y_start=0);

    void transcoder();
    void fill_buffer(struct v4l2_buffer* ubuf);
public:
    UVCfb(char *devname, uint16_t maxpkt=1024, uint8_t nbufs=2);
    ~UVCfb();
    void loop();
};

#endif //__UVCFB_H__
