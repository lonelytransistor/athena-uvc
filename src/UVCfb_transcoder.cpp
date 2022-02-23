#include "UVCfb.h"

#include <arm_neon.h>

uint16_t UVCfb::fletcher16(uint8_t *data, size_t len) {
    uint32_t c0, c1;

    for (c0 = c1 = 0; len > 0; ) {
        size_t blocklen = len;
        if (blocklen > 5002) {
            blocklen = 5002;
        }
        len -= blocklen;
        do {
            c0 = c0 + *data++;
            c1 = c1 + c0;
        } while (--blocklen);
        c0 = c0 % 255;
        c1 = c1 % 255;
    }
    return (c1 << 8 | c0);
}

void UVCfb::convertFbTo8Bit(uint8_t* buffer, uint32_t size) {
    const auto fb_ptr = reinterpret_cast<uint16_t*>(m_fb);

#if __LP64__ && defined __ARM_NEON__
    const auto packSize = m_fb_size/16;

    // Read 8x16bit pixels (128bits), store 8x8bit pixels (64bits)
    for (uint32_t i=0; i<packSize; ++i) {
        const uint8x8x2_t lanes8 = vld2_u8(fb_ptr);
        vst1_u64(buffer, lanes8.val[1]);
        fb_ptr += 8;
        buffer += 8;
    }
#else
    for (uint32_t i=0; i<size; ++i) {
        buffer[i] = fb_ptr[i]>>8;
    }
#endif
}

void UVCfb::transcoder() {
    LOG("Transcoder thread starting");
    // Create mutex:
    std::unique_lock<std::mutex> this_mx(m_mutex, std::defer_lock);
    // Critical section:
    this_mx.lock();
    m_transcoder_running = true;
    this_mx.unlock();
    // Allocate buffers:
    m_jpeg_buffer[0] = new uint8_t[g_payload_size];
    m_jpeg_buffer[1] = new uint8_t[g_payload_size];
    m_jpeg_buffer_size[0] = g_payload_size;
    m_jpeg_buffer_size[1] = g_payload_size;
    m_jpeg_buffer_ix = 0;
    uint8_t* jpeg_tmpbuffer = new uint8_t[g_payload_size];
    long unsigned int jpeg_tmpbuffer_size = g_payload_size;
    uint8_t* raw_tmpbuffer = new uint8_t[m_fb_size/2];
    long unsigned int raw_tmpbuffer_size = m_fb_size/2;
    // Initial values for checksums:
    uint16_t checksum_old = 0;
    uint16_t checksum_new = 0;
    // Set-up for turbojpeg
    int flags = TJFLAG_NOREALLOC;
    int jpegQuality = 92;
    bool rotate_fb = (g_width > g_height);
    // Init turbojpeg
    tjhandle tjCompress_ptr = tjInitCompress();
    tjhandle tjTransform_ptr = tjInitTransform();
    if (tjCompress == NULL || tjTransform == NULL) {
        ERR(tjGetErrorStr());
        goto fail;
    }
    tjtransform xForm;
    memset(&xForm, 0, sizeof(tjtransform));
    xForm.options = TJXOP_ROT90;
    // Transcoder loop:
    do {
        checksum_new = UVCfb::fletcher16((uint8_t*)m_fb, m_fb_size);
        // Has the screen changed?
        if (checksum_new != checksum_old) {
            checksum_new = checksum_old;
            
            // Point to the next unused jpeg buffer
            uint8_t buffer_ix = (m_jpeg_buffer_ix+1)&0x01;
            // 16-bit to 8-bit greyscale:
            convertFbTo8Bit(raw_tmpbuffer, raw_tmpbuffer_size);
            do {
                //memset(&m_jpeg_buffer[buffer_ix], 0, g_payload_size);
                if (!rotate_fb) {
                    if (tjCompress2(tjCompress_ptr, raw_tmpbuffer,
                        m_fb_width, 0, m_fb_height, m_pixelFormat,
                        &m_jpeg_buffer[buffer_ix], &m_jpeg_buffer_size[buffer_ix],
                        m_jpegSubsamp, jpegQuality, flags) != 0) {
                            ERR(tjGetErrorStr());
                            goto fail;
                    }
                } else {
                    if (tjCompress2(tjCompress_ptr, raw_tmpbuffer,
                        m_fb_width, 0, m_fb_height, m_pixelFormat,
                        &jpeg_tmpbuffer, &jpeg_tmpbuffer_size,
                        m_jpegSubsamp, jpegQuality, flags) != 0) {
                            ERR(tjGetErrorStr());
                            goto fail;
                    }
                    if (tjTransform(tjTransform_ptr,
                        jpeg_tmpbuffer, jpeg_tmpbuffer_size, 1,
                        &m_jpeg_buffer[buffer_ix], &m_jpeg_buffer_size[buffer_ix], &xForm, 0) != 0) {
                            ERR(tjGetErrorStr());
                            goto fail;
                    }
                }
                // If just started fill both buffers
                if (!m_jpeg_buffer_ix) {
                    LOG("Initial frames have been created");
                    m_jpeg_buffer_ix = 1;
                    buffer_ix = 0;
                }
            } while (!m_jpeg_buffer_ix);
            // Critical section:
            this_mx.lock();
            if (m_jpeg_buffer_ix_copied == m_jpeg_buffer_ix) {
                m_jpeg_buffer_ix++;
            } else {
                LOG("Frame dropped");
            }
            this_mx.unlock();
            // Sleep for frame time
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }
    } while (g_is_streaming);
    
fail:
    delete m_jpeg_buffer[0];
    delete m_jpeg_buffer[1];
    tjDestroy(tjCompress_ptr);
    tjDestroy(tjTransform_ptr);
    delete jpeg_tmpbuffer;
    delete raw_tmpbuffer;
    // Critical section:
    this_mx.lock();
    m_transcoder_running = false;
    this_mx.unlock();
    LOG("Transcoder thread finishing");
}
void UVCfb::fill_buffer(struct v4l2_buffer* ubuf) {
    if (!m_jpeg_buffer_ix) {
        m_jpeg_buffer_ix_copied = 0;
        return;
    }
    uint8_t buffer_ix = m_jpeg_buffer_ix&0x01;
    
    ubuf->length = g_payload_size;
    ubuf->bytesused = m_jpeg_buffer_size[buffer_ix];
    memcpy(m_buf[ubuf->index].start, m_jpeg_buffer[buffer_ix], m_jpeg_buffer_size[buffer_ix]);
    
    // Critical section:
    std::unique_lock<std::mutex> this_mx(m_mutex);
    m_jpeg_buffer_ix_copied = m_jpeg_buffer_ix;
}
