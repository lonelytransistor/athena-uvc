#include "UVCfb.h"
#include "UVC.h"

//width: 1404
//height: 1872

UVCfb::UVCfb(char *devname, uint16_t maxpkt, uint8_t nbufs) : UVC(devname, maxpkt, nbufs) {
    // Open the framebuffer
    struct fb_fix_screeninfo fixedFBInfo;
    struct fb_var_screeninfo variableFBInfo;
    m_fb_fd = open(FB_NAME, O_RDWR);
    if ((m_fb_fd < 0) ||
        (ioctl(m_fb_fd, FBIOGET_FSCREENINFO, &fixedFBInfo) < 0) ||
        (ioctl(m_fb_fd, FBIOGET_VSCREENINFO, &variableFBInfo) < 0)) {
            throw std::runtime_error(strerror(errno));
    }
    m_fb_width = variableFBInfo.xres;
    m_fb_height = variableFBInfo.yres;
    
    g_payload_size = tjBufSize(m_fb_width, m_fb_height, m_jpegSubsamp);
    m_fb_size = fixedFBInfo.smem_len;
    
    // Map the framebuffer
    m_fb = mmap(NULL, fixedFBInfo.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, m_fb_fd,0);
    if (m_fb == NULL) {
        throw std::runtime_error(strerror(errno));
    }
    
    // Guess framebuffer format
    LOG("Framebuffer has "<<std::to_string(variableFBInfo.bits_per_pixel)<<" bits per pixel");
    switch (variableFBInfo.bits_per_pixel) {
        case 32:
            m_nbands = 4;
            if (!variableFBInfo.transp.offset)
                m_pixelFormat = (variableFBInfo.red.offset>variableFBInfo.blue.offset) ? TJPF_BGRA : TJPF_RGBA;
            else
                m_pixelFormat = (variableFBInfo.red.offset>variableFBInfo.blue.offset) ? TJPF_ABGR : TJPF_ARGB;
            m_jpegSubsamp = TJSAMP_444;
        break;
        case 24:
            m_nbands = 3;
            m_pixelFormat = (variableFBInfo.red.offset>variableFBInfo.blue.offset) ? TJPF_BGRA : TJPF_RGBA;
            m_jpegSubsamp = TJSAMP_444;
        break;
        case 16: //rm2fb
            m_nbands = 0;
            m_pixelFormat = TJPF_GRAY;
            m_jpegSubsamp = TJSAMP_GRAY;
        break;
        case 8:
            m_nbands = 1;
            m_pixelFormat = TJPF_GRAY;
            m_jpegSubsamp = TJSAMP_GRAY;
        break;
        default:
            throw std::runtime_error("Unknown fb pixel format");
    }
    return;
fail:
    if (errno)
        throw std::runtime_error(strerror(errno));
    if (tjGetErrorStr())
        throw std::runtime_error(tjGetErrorStr());
}
UVCfb::~UVCfb() {
    munmap(m_fb, 0);
    close(m_fb_fd);
    for (buffer buf : m_buf) {
        munmap(buf.start, buf.length);
    }
}
void UVCfb::loop() {
    loop_main();
}
