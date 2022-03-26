#include "UVCfb.h"

#include <arm_neon.h>

uint16_t UVCfb::getChecksum() {
    const auto fb_ptr = reinterpret_cast<uint8_t*>(m_fb);
    const auto packSize = m_fb_size/(16*4);
    uint8x16x4_t s0, s1;
    uint64x2_t ss0, ss1;
    uint8_t sss0, sss1;
    
    // Read (8 bit)x16x4 pixels (4x128bits), parallelly calculate (8 bit)x16x4 checksums (4x128bits)
    for (uint32_t i=0; i<packSize; i++) {
        const uint8x16x4_t lanes8 = vld4q_u8(&fb_ptr[i*(16*4)]);
        for (uint8_t el=0; el<4; el++) {
            s0.val[el] = vaddq_u8(s0.val[el], lanes8.val[el]);
            s1.val[el] = vaddq_u8(s1.val[el], s0.val[el]);
        }
    }
    // Sum parallel calculations to a single vector
    for (uint8_t el=1; el<4; el++) {
        s0.val[0] = vaddq_u8(s0.val[0], s0.val[el]);
        s1.val[0] = vaddq_u8(s1.val[0], s0.val[el]);
    }
    // Sum up the elements of the vectors
    ss0 = vpaddlq_u32(vpaddlq_u16(vpaddlq_u8(s0.val[0])));
    ss1 = vpaddlq_u32(vpaddlq_u16(vpaddlq_u8(s1.val[0])));
    // Do a final sum and get rid of insignificant bytes
    sss0 = (uint8_t)vgetq_lane_u64(ss0, 0) | (uint8_t)vgetq_lane_u64(ss0, 1);
    sss1 = (uint8_t)vgetq_lane_u64(ss1, 0) | (uint8_t)vgetq_lane_u64(ss1, 1);
    
    return (sss1 << 8 | sss0);
}
int UVCfb::getResizedFb(uint8_t* buffer, uint16_t w, uint16_t h) {
    const auto fb_ptr = reinterpret_cast<uint16_t*>(m_fb);
    
    // Clear the output buffer
    memset(buffer, 0, w*h);
    
    // Generate helper vectors
    const uint16_t vect_rising_b[] = {0, 1, 2, 3};
    const uint16x4_t vect_rising = vld1_u16(vect_rising_b);
    const uint16x4_t vect_0x01 = vdup_n_u16(0x01);
    const uint8x8_t vect_0xFF = vdup_n_u8(0xFF);
    
    // Calculate ratios minus 1. These will always be 0.0<ratio<1.0
    const uint8_t x_ratio = ((m_fb_width << 8)/w) & 0xFF;
    const uint8_t y_ratio = ((m_fb_height << 8)/h) & 0xFF;

    for (uint16_t y_dst=0; y_dst<h; y_dst++) {
        // Get y0 and y1 of the averaging square
        uint16_t y0 = ((y_dst * y_ratio) >> 8) + y_dst;
        uint16_t y1 = y0 + 1;
        // Get w_y0 and w_y1 weights for the square
        uint8x8_t wy0_dst = vdup_n_u8(       (y_dst * y_ratio) & 0xFF);
        uint8x8_t wy1_dst = vdup_n_u8(0xFF - (y_dst * y_ratio) & 0xFF);
        
        for (uint16_t x_offset=0; x_offset<w; x_offset+=8) {
            // NOTE: There are no intrinsics for 32x8 or 16x8, so we need to repeat most of the steps twice.

            // Initialize our x coordinates with a rising list and add to it the current x_offset.
            // Then multiply exery resulting x coordinate with x_ratio to get a vector of x_dst.
            uint32x4_t x_data_0 = vmull_n_u16( vadd_u16( vect_rising, vdup_n_u16(x_offset  ) ), x_ratio );
            uint32x4_t x_data_1 = vmull_n_u16( vadd_u16( vect_rising, vdup_n_u16(x_offset+4) ), x_ratio );
            // The high part of the operation will contain the delta(x0) coordinates for the square in the fb. x1 is 1 larger.
            // This value will be actually x0-x_offset because our ratio has 1 substructed from them.
            uint16x4_t x0_dst_0 = vshrn_n_u32(x_data_0, 8);
            uint16x4_t x0_dst_1 = vshrn_n_u32(x_data_1, 8);
            uint16x4_t x1_dst_0 = vadd_u16(x0_dst_0, vect_0x01);
            uint16x4_t x1_dst_1 = vadd_u16(x0_dst_1, vect_0x01);
            // The low part will contain the weight for this pixel multiplied by 256.
            // Because there is no conversion of 16x4 to 8x4, we need to stitch two 16x4 into one 16x8.
            // Then we can get only the most significant bits to 8
            uint8x8_t wx0_dst = vmovn_u16( vcombine_u16( vmovn_u32(x_data_0), vmovn_u32(x_data_1) ) );
            uint8x8_t wx1_dst = vsub_u8( vect_0xFF, wx0_dst );
            
            // Now we can merge all of the above into 16x8 and convert to 8bits.
            uint16x8_t x0_dst_16b = vcombine_u16(x0_dst_0, x0_dst_1);
            uint16x8_t x1_dst_16b = vcombine_u16(x1_dst_0, x1_dst_1);
            
            // We can now generate weights for every corner.
            // We are multiplying two pseudo floating point values, shifted by 8 bits to the left, so that they are integers.
            // Therefore we need to shift one of the 8 bits back to the right, to again obtain a pseudo-floating point integer.
            uint8x8_t wx0y0_dst = vshrn_n_u16(vmull_u8(wx0_dst, wy0_dst), 8);
            uint8x8_t wx0y1_dst = vshrn_n_u16(vmull_u8(wx0_dst, wy1_dst), 8);
            uint8x8_t wx1y0_dst = vshrn_n_u16(vmull_u8(wx1_dst, wy0_dst), 8);
            uint8x8_t wx1y1_dst = vshrn_n_u16(vmull_u8(wx1_dst, wy1_dst), 8);
            
            // Calculate mod 4 for every element in xN_dst vectors.
            uint16_t x_fb_offset = vgetq_lane_u16(x0_dst_16b, 0);
            uint8x8_t x0_dst = vmovn_u16(vsubq_u16(x0_dst_16b, vdupq_n_u16(x_fb_offset)));
            uint8x8_t x1_dst = vmovn_u16(vsubq_u16(x1_dst_16b, vdupq_n_u16(x_fb_offset)));
            // Read the buffer and drop the low bytes to get an 8-bit value.
            uint8x8_t y0_buff_0 = vld2_u8((uint8_t*)&fb_ptr[m_fb_width*y0 + x_offset+x_fb_offset  ]).val[1];
            uint8x8_t y0_buff_1 = vld2_u8((uint8_t*)&fb_ptr[m_fb_width*y0 + x_offset+x_fb_offset+8]).val[1];
            uint8x8_t y1_buff_0 = vld2_u8((uint8_t*)&fb_ptr[m_fb_width*y1 + x_offset+x_fb_offset  ]).val[1];
            uint8x8_t y1_buff_1 = vld2_u8((uint8_t*)&fb_ptr[m_fb_width*y1 + x_offset+x_fb_offset+8]).val[1];
            // Lookup the values for the x0,y0 pixels in the fb.
            uint8x8_t x0y0_pixels_0 = vtbl1_u8(y0_buff_0, x0_dst);
            uint8x8_t x0y0_pixels_1 = vtbl1_u8(y0_buff_1, vsub_u8(x0_dst, vdup_n_u8(8)));
            uint8x8_t x0y0_pixels = veor_u8(x0y0_pixels_0, x0y0_pixels_1);
            // Lookup the values for the x1,y0 pixels in the fb.
            uint8x8_t x1y0_pixels_0 = vtbl1_u8(y0_buff_0, x1_dst);
            uint8x8_t x1y0_pixels_1 = vtbl1_u8(y0_buff_1, vsub_u8(x1_dst, vdup_n_u8(8)));
            uint8x8_t x1y0_pixels = veor_u8(x1y0_pixels_0, x1y0_pixels_1);
            // Lookup the values for the x0,y1 pixels in the fb.
            uint8x8_t x0y1_pixels_0 = vtbl1_u8(y1_buff_0, x0_dst);
            uint8x8_t x0y1_pixels_1 = vtbl1_u8(y1_buff_1, vsub_u8(x0_dst, vdup_n_u8(8)));
            uint8x8_t x0y1_pixels = veor_u8(x0y1_pixels_0, x0y1_pixels_1);
            // Lookup the values for the x1,y1 pixels in the fb.
            uint8x8_t x1y1_pixels_0 = vtbl1_u8(y1_buff_0, x1_dst);
            uint8x8_t x1y1_pixels_1 = vtbl1_u8(y1_buff_1, vsub_u8(x1_dst, vdup_n_u8(8)));
            uint8x8_t x1y1_pixels = veor_u8(x1y1_pixels_0, x1y1_pixels_1);
            
            // Multiply the pixels by their weights.
            x0y0_pixels = vshrn_n_u16(vmull_u8(x0y0_pixels, wx0y0_dst), 8);
            x0y1_pixels = vshrn_n_u16(vmull_u8(x0y1_pixels, wx0y1_dst), 8);
            x1y0_pixels = vshrn_n_u16(vmull_u8(x1y0_pixels, wx1y0_dst), 8);
            x1y1_pixels = vshrn_n_u16(vmull_u8(x1y1_pixels, wx1y1_dst), 8);
            
            // Finally sum up all the corners and write them to the buffer.
            uint8x8_t x0_pixels = vqadd_u8(x0y0_pixels, x0y1_pixels);
            uint8x8_t x1_pixels = vqadd_u8(x1y0_pixels, x1y1_pixels);
            vst1_u8(&buffer[x_offset], vqadd_u8(x0_pixels, x1_pixels));
        }
    }
    
    return w*h;
}
void UVCfb::getFb(uint8_t* buffer) {
    const auto fb_ptr = reinterpret_cast<uint8_t*>(m_fb);

    // Read 2x8x16bit pixels (2x128bits), store 2x8x8bit pixels (2x64bits)
    for (uint32_t i=0; i<m_fb_size/16; i++) {
        vst1_u8(&buffer[i*8], vld2_u8(&fb_ptr[i*16]).val[1]);
    }
}
int UVCfb::getJPEG(tjhandle tjCompress_ptr, uint8_t* out_buffer, long unsigned int* out_buffer_sz, uint8_t w, uint8_t h) {
    int ret;
    
    if ((w == m_fb_width) && (h == m_fb_height)) {
        getFb(m_tmp_buffer[0]);
    } else {
        getResizedFb(m_tmp_buffer[0], w, h);
    }
    ret = tjCompress2(tjCompress_ptr, m_tmp_buffer[0],
        w, 0, h, m_pixelFormat,
        &out_buffer, out_buffer_sz,
        m_jpegSubsamp, m_jpegQuality, m_jpegFlags);
    
    return ret;
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
    
    m_tmp_buffer[0] = new uint8_t[m_fb_size/2];
    m_tmp_buffer[1] = new uint8_t[m_fb_size/2];
    m_tmp_buffer_size[0] = m_fb_size/2;
    m_tmp_buffer_size[1] = m_fb_size/2;
    
    m_jpeg_buffer_ix = 0;
    
    // Initial values for checksums:
    uint16_t checksum_old = 0;
    uint16_t checksum_new = 0;
    
    // Init turbojpeg
    tjhandle tjCompress_ptr = tjInitCompress();
    if (tjCompress == NULL) {
        ERR(tjGetErrorStr());
        goto fail;
    }
    
    // Transcoder loop:
    do {
        checksum_new = getChecksum();
        // Has the screen changed?
        if (checksum_new != checksum_old) {
            checksum_new = checksum_old;
            // Point to the next unused jpeg buffer
            uint8_t buffer_ix = (m_jpeg_buffer_ix+1)&0x01;
            if (getJPEG(tjCompress_ptr, m_jpeg_buffer[buffer_ix], &m_jpeg_buffer_size[buffer_ix], g_width, g_height)) {
                ERR(tjGetErrorStr());
                goto fail;
            }
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
    // Free buffers:
    delete m_jpeg_buffer[0];
    delete m_jpeg_buffer[1];
    delete m_tmp_buffer[0];
    delete m_tmp_buffer[1];
    
    tjDestroy(tjCompress_ptr);
    
    // Critical section:
    this_mx.lock();
    m_transcoder_running = false;
    this_mx.unlock();
    
    LOG("Transcoder thread finished");
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
