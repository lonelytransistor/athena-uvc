#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef __ARM_NEON__
#include <arm_neon.h>
#else
#define USE_SSE4
#define NEON2SSE_DISABLE_PERFORMANCE_WARNING
#include "NEON_2_SSE.h"
#endif
#include <cstring>

#define DEBUG(msg)

uint16_t m_fb_width = 1404;
uint16_t m_fb_height = 1872;
uint32_t m_fb_size = m_fb_width*m_fb_height*2;
uint32_t m_fb_size2 = m_fb_width*m_fb_height;
uint8_t m_fb[1404*1872*2];
uint8_t m_tmp_buffer[1404*1872];
uint8x8_t m_tmp_buffer_neon[1404*1872/8];
#define vtrn8(a, b) \
{ \
uint8x8x2_t _transpose_tmp = vtrn_u8(a, b); \
a = _transpose_tmp.val[0]; \
b = _transpose_tmp.val[1]; \
}
#define vtrn16(a, b) \
{ \
uint16x4x2_t _transpose_tmp = vtrn_u16(vreinterpret_u16_u8(a), vreinterpret_u16_u8(b)); \
a = vreinterpret_u8_u16(_transpose_tmp.val[0]); \
b = vreinterpret_u8_u16(_transpose_tmp.val[1]); \
}
#define vtrn32(a, b) \
{ \
uint32x2x2_t _transpose_tmp = vtrn_u32(vreinterpret_u32_u8(a), vreinterpret_u32_u8(b)); \
a = vreinterpret_u8_u32(_transpose_tmp.val[0]); \
b = vreinterpret_u8_u32(_transpose_tmp.val[1]); \
}

int getRotatedFb(uint8_t* out_buffer) {
    const auto fb_ptr = reinterpret_cast<uint16_t*>(m_fb);
    const auto fb_height = m_fb_height;
    const auto fb_width = m_fb_width;
    const auto fb_size = fb_height * fb_width;
    
    uint16_t y;
    uint16_t x;

    for (y=0; y<fb_height; y+=8) {
        for (x=0; x<fb_width; x+=8) {
            uint8x8_t row[] = {
                vld2_u8((uint8_t*)&fb_ptr[x + (y+0)*fb_width]).val[1],
                vld2_u8((uint8_t*)&fb_ptr[x + (y+1)*fb_width]).val[1],
                vld2_u8((uint8_t*)&fb_ptr[x + (y+2)*fb_width]).val[1],
                vld2_u8((uint8_t*)&fb_ptr[x + (y+3)*fb_width]).val[1],
                vld2_u8((uint8_t*)&fb_ptr[x + (y+4)*fb_width]).val[1],
                vld2_u8((uint8_t*)&fb_ptr[x + (y+5)*fb_width]).val[1],
                vld2_u8((uint8_t*)&fb_ptr[x + (y+6)*fb_width]).val[1],
                vld2_u8((uint8_t*)&fb_ptr[x + (y+7)*fb_width]).val[1]
            };
            vtrn8(row[0], row[1]);
            vtrn8(row[2], row[3]);
            vtrn8(row[4], row[5]);
            vtrn8(row[6], row[7]);
            
            vtrn16(row[0], row[2]);
            vtrn16(row[1], row[3]);
            vtrn16(row[4], row[6]);
            vtrn16(row[5], row[7]);
            
            vtrn16(row[0], row[4]);
            vtrn16(row[1], row[5]);
            vtrn16(row[2], row[6]);
            vtrn16(row[3], row[7]);
            
            for (uint8_t ix=0; ix<sizeof(row); ix++) {
                vst1_u8(&out_buffer[(x+ix)*fb_height + y], row[ix]);
            }
        }
    }
    
    return x*y;
}
enum t_rescalers{RESIZE_BILINEAR_16BIT, RESIZE_BILINEAR_8BIT, RESIZE_BICUBIC_8BIT};
int getFb(uint8_t* buffer) {
    const auto fb_ptr = reinterpret_cast<uint16_t*>(m_fb);
    const auto fb_height = m_fb_height;
    const auto fb_width = m_fb_width;
    const auto fb_size = fb_height * fb_width;

    // Read 2x8x16bit pixels (2x128bits), store 2x8x8bit pixels (2x64bits)
    for (uint32_t i=0; i<fb_size/8; i++) {
        vst1_u8(&buffer[i*8], vld2_u8((uint8_t*)&fb_ptr[i*8]).val[1]);
    }
    
    return fb_size/2;
}
int getFb(uint8x8_t* buffer) {
    const auto fb_ptr = reinterpret_cast<uint16_t*>(m_fb);
    const auto fb_height = m_fb_height;
    const auto fb_width = m_fb_width;
    const auto fb_width_div8 = m_fb_width/8;
    
    uint32_t x, y;
    for (y=0; y<fb_height; y++) {
        uint32_t y_dbuff = y*fb_width_div8;
        uint32_t y_fbuff = y*fb_width;
        for (x=0; x<fb_width_div8; x++) {
            buffer[y_dbuff + x] = vld2_u8((uint8_t*)&fb_ptr[y_fbuff + x*8]).val[1];
        }
    }
    
    return y*fb_width_div8 + x;
}
int getResizedFb_bilinear_8bit(uint8_t* buffer, uint16_t w, uint16_t h, uint16_t x_start, uint16_t y_start) {
    const auto fb_ptr = reinterpret_cast<uint16_t*>(m_fb);
    const auto fb_height = m_fb_height;
    const auto fb_width = m_fb_width;
    const auto fb_size = fb_height * fb_width;
    
    // Generate helper vectors
    const uint16_t vect_rising_b[] = {0, 1, 2, 3};
    const uint16x4_t vect_rising = vld1_u16(vect_rising_b);
    const uint16x4_t vect_0x01 = vdup_n_u16(0x01);
    const uint8x8_t vect_0xFF = vdup_n_u8(0xFF);
    
    // Calculate ratios minus 1. These will always be 0.0<ratio<1.0 then scaled up to 8bits
    // 1 + at the beginning ensures better precision on average
    const uint8_t x_ratio = 1 + ((fb_width << 8)/w) & 0xFF;
    const uint8_t y_ratio = 1 + ((fb_height << 8)/h) & 0xFF;

    for (uint16_t y_dst=0; y_dst<h; y_dst++) {
        // Get y0 and y1 of the averaging square
        uint16_t y0 = ((y_dst * y_ratio) >> 8) + y_dst;
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
            uint32_t fb_offset = fb_width*y0 + x_offset + x_fb_offset;
            if (fb_offset+16 >= fb_size) {
                DEBUG("Buffer overflow");
                return y_dst*w + x_offset;
            }
            uint8x8_t y0_buff_0 = vld2_u8((uint8_t*)&fb_ptr[         fb_offset  ]).val[1];
            uint8x8_t y0_buff_1 = vld2_u8((uint8_t*)&fb_ptr[         fb_offset+8]).val[1];
            uint8x8_t y1_buff_0 = vld2_u8((uint8_t*)&fb_ptr[fb_width+fb_offset  ]).val[1];
            uint8x8_t y1_buff_1 = vld2_u8((uint8_t*)&fb_ptr[fb_width+fb_offset+8]).val[1];
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
            
            //DEBUG("new:["<<std::to_string(x_offset)<<", "<<std::to_string(y_dst)<<"], old:["<<std::to_string(x_offset + x_fb_offset)<<", "<<std::to_string(y0)<<"] "<<std::to_string(fb_offset));
            
            // Finally sum up all the corners and write them to the buffer.
            uint8x8_t x0_pixels = vqadd_u8(x0y0_pixels, x0y1_pixels);
            uint8x8_t x1_pixels = vqadd_u8(x1y0_pixels, x1y1_pixels);
            vst1_u8(&buffer[(y_dst + y_start)*w + x_offset + x_start], vqadd_u8(x0_pixels, x1_pixels));
        }
    }
    
    return w*h;
}
int getResizedFb_bilinear_16bit(uint8_t* buffer, uint16_t w, uint16_t h, uint16_t x_start, uint16_t y_start) {
    const auto fb_ptr = reinterpret_cast<uint16_t*>(m_fb);
    const auto fb_height = m_fb_height;
    const auto fb_width = m_fb_width;
    const auto fb_size = fb_height * fb_width;
    
    // Make these variables known at the end of loop
    uint16_t x_offset;
    uint16_t y_dst;
    
    // Generate helper vectors
    const uint16_t vect_rising_b[] = {0, 1, 2, 3};
    const uint16x4_t vect_rising = vld1_u16(vect_rising_b);
    const uint16x4_t vect_0x01 = vdup_n_u16(0x0001);
    const uint16x4_t vect_0xFFFF = vdup_n_u16(0xFFFF);
    
    // Calculate ratios minus 1. These will always be 0.0<ratio<1.0 then scaled up to 16bits
    // 1 + at the beginning ensures better precision on average
    const uint16_t x_ratio = 1 + ((fb_width << 16)/w) & 0xFFFF;
    const uint16_t y_ratio = 1 + ((fb_height << 16)/h) & 0xFFFF;

    for (y_dst=0; y_dst<h; y_dst++) {
        // Get y0 and y1 of the averaging square
        uint16_t y0 = ((y_dst * y_ratio) >> 16) + y_dst;
        // Get w_y0 and w_y1 weights for the square
        uint16x4_t wy0_dst = vdup_n_u16(         (y_dst * y_ratio) & 0xFFFF);
        uint16x4_t wy1_dst = vdup_n_u16(0xFFFF - (y_dst * y_ratio) & 0xFFFF);
        
        for (x_offset=0; x_offset<w; x_offset+=8) {
            // NOTE: There are no intrinsics for 32x8 or 16x8, so we need to repeat most of the steps twice.

            // Initialize our x coordinates with a rising list and add to it the current x_offset.
            // Then multiply exery resulting x coordinate with x_ratio to get a vector of x_dst.
            uint32x4_t x_data_0 = vmull_n_u16( vadd_u16( vect_rising, vdup_n_u16(x_offset  ) ), x_ratio );
            uint32x4_t x_data_1 = vmull_n_u16( vadd_u16( vect_rising, vdup_n_u16(x_offset+4) ), x_ratio );
            // The high part of the operation will contain the delta(x0) coordinates for the square in the fb. x1 is 1 larger.
            // This value will be actually x0-x_offset because our ratio has 1 substructed from them.
            uint16x4_t x0_dst_0 = vshrn_n_u32(x_data_0, 16);
            uint16x4_t x0_dst_1 = vshrn_n_u32(x_data_1, 16);
            uint16x4_t x1_dst_0 = vadd_u16(x0_dst_0, vect_0x01);
            uint16x4_t x1_dst_1 = vadd_u16(x0_dst_1, vect_0x01);
            // The low part will contain the weight for this pixel multiplied by 256.
            // Because there is no conversion of 16x4 to 8x4, we need to stitch two 16x4 into one 16x8.
            // Then we can get only the most significant bits to 8
            uint16x4_t wx0_dst_0 = vmovn_u32(x_data_0);
            uint16x4_t wx0_dst_1 = vmovn_u32(x_data_1);
            uint16x4_t wx1_dst_0 = vsub_u16(vect_0xFFFF, wx0_dst_0);
            uint16x4_t wx1_dst_1 = vsub_u16(vect_0xFFFF, wx0_dst_1);
            
            // Now we can merge all of the above into 16x8
            uint16x8_t x0_dst_16b = vcombine_u16(x0_dst_0, x0_dst_1);
            uint16x8_t x1_dst_16b = vcombine_u16(x1_dst_0, x1_dst_1);
            
            // We can now generate weights for every corner.
            // We are multiplying two pseudo floating point values, shifted by 16 bits to the left, so that they are integers.
            // Therefore we need to shift one of the 8 bits back to the right, to again obtain a pseudo-floating point integer.
            uint16x4_t wx0y0_dst_0 = vshrn_n_u32(vmull_u16(wx0_dst_0, wy0_dst), 16);
            uint16x4_t wx0y0_dst_1 = vshrn_n_u32(vmull_u16(wx0_dst_1, wy0_dst), 16);
            uint16x4_t wx0y1_dst_0 = vshrn_n_u32(vmull_u16(wx0_dst_0, wy1_dst), 16);
            uint16x4_t wx0y1_dst_1 = vshrn_n_u32(vmull_u16(wx0_dst_1, wy1_dst), 16);
            uint16x4_t wx1y0_dst_0 = vshrn_n_u32(vmull_u16(wx1_dst_0, wy0_dst), 16);
            uint16x4_t wx1y0_dst_1 = vshrn_n_u32(vmull_u16(wx1_dst_1, wy0_dst), 16);
            uint16x4_t wx1y1_dst_0 = vshrn_n_u32(vmull_u16(wx1_dst_0, wy1_dst), 16);
            uint16x4_t wx1y1_dst_1 = vshrn_n_u32(vmull_u16(wx1_dst_1, wy1_dst), 16);
            
            // Calculate mod 4 for every element in xN_dst vectors.
            uint16_t x_fb_offset = vgetq_lane_u16(x0_dst_16b, 0);
            uint8x8_t x0_dst = vmovn_u16(vsubq_u16(x0_dst_16b, vdupq_n_u16(x_fb_offset)));
            uint8x8_t x1_dst = vmovn_u16(vsubq_u16(x1_dst_16b, vdupq_n_u16(x_fb_offset)));
            // Read the buffer and drop the low bytes to get an 8-bit value.
            uint32_t fb_offset = fb_width*y0 + x_offset + x_fb_offset;
            if (fb_offset+16 >= fb_size) {
                DEBUG("Buffer overflow");
                return y_dst*w + x_offset;
            }
            uint8x8_t y0_buff_0 = vld2_u8((uint8_t*)&fb_ptr[         fb_offset  ]).val[1];
            uint8x8_t y0_buff_1 = vld2_u8((uint8_t*)&fb_ptr[         fb_offset+8]).val[1];
            uint8x8_t y1_buff_0 = vld2_u8((uint8_t*)&fb_ptr[fb_width+fb_offset  ]).val[1];
            uint8x8_t y1_buff_1 = vld2_u8((uint8_t*)&fb_ptr[fb_width+fb_offset+8]).val[1];
            // Lookup the values for the x0,y0 pixels in the fb.
            uint8x8_t x0y0_pixels_0 = vtbl1_u8(y0_buff_0, x0_dst);
            uint8x8_t x0y0_pixels_1 = vtbl1_u8(y0_buff_1, vsub_u8(x0_dst, vdup_n_u8(8)));
            uint16x8_t x0y0_pixels = vmovl_u8(veor_u8(x0y0_pixels_0, x0y0_pixels_1));
            // Lookup the values for the x1,y0 pixels in the fb.
            uint8x8_t x1y0_pixels_0 = vtbl1_u8(y0_buff_0, x1_dst);
            uint8x8_t x1y0_pixels_1 = vtbl1_u8(y0_buff_1, vsub_u8(x1_dst, vdup_n_u8(8)));
            uint16x8_t x1y0_pixels = vmovl_u8(veor_u8(x1y0_pixels_0, x1y0_pixels_1));
            // Lookup the values for the x0,y1 pixels in the fb.
            uint8x8_t x0y1_pixels_0 = vtbl1_u8(y1_buff_0, x0_dst);
            uint8x8_t x0y1_pixels_1 = vtbl1_u8(y1_buff_1, vsub_u8(x0_dst, vdup_n_u8(8)));
            uint16x8_t x0y1_pixels = vmovl_u8(veor_u8(x0y1_pixels_0, x0y1_pixels_1));
            // Lookup the values for the x1,y1 pixels in the fb.
            uint8x8_t x1y1_pixels_0 = vtbl1_u8(y1_buff_0, x1_dst);
            uint8x8_t x1y1_pixels_1 = vtbl1_u8(y1_buff_1, vsub_u8(x1_dst, vdup_n_u8(8)));
            uint16x8_t x1y1_pixels = vmovl_u8(veor_u8(x1y1_pixels_0, x1y1_pixels_1));
            
            // Multiply the pixels by their weights.
            uint16x4_t x0y0_pixels_0b = vshrn_n_u32(vmull_u16(vget_high_u16(x0y0_pixels), wx0y0_dst_0), 16);
            uint16x4_t x0y0_pixels_1b = vshrn_n_u32(vmull_u16( vget_low_u16(x0y0_pixels), wx0y0_dst_1), 16);
            uint16x4_t x0y1_pixels_0b = vshrn_n_u32(vmull_u16(vget_high_u16(x0y1_pixels), wx0y1_dst_0), 16);
            uint16x4_t x0y1_pixels_1b = vshrn_n_u32(vmull_u16( vget_low_u16(x0y1_pixels), wx0y1_dst_1), 16);
            uint16x4_t x1y0_pixels_0b = vshrn_n_u32(vmull_u16(vget_high_u16(x1y0_pixels), wx1y0_dst_0), 16);
            uint16x4_t x1y0_pixels_1b = vshrn_n_u32(vmull_u16( vget_low_u16(x1y0_pixels), wx1y0_dst_1), 16);
            uint16x4_t x1y1_pixels_0b = vshrn_n_u32(vmull_u16(vget_high_u16(x1y1_pixels), wx1y1_dst_0), 16);
            uint16x4_t x1y1_pixels_1b = vshrn_n_u32(vmull_u16( vget_low_u16(x1y1_pixels), wx1y1_dst_1), 16);
            
            //DEBUG("new:["<<std::to_string(x_offset)<<", "<<std::to_string(y_dst)<<"], old:["<<std::to_string(x_offset + x_fb_offset)<<", "<<std::to_string(y0)<<"] "<<std::to_string(fb_offset));
            
            // Finally sum up all the corners and write them to the buffer.
            uint8x8_t x0_pixels = vqadd_u8(vmovn_u16(vcombine_u16(x0y0_pixels_1b, x0y0_pixels_0b)), vmovn_u16(vcombine_u16(x0y1_pixels_1b, x0y1_pixels_0b)));
            uint8x8_t x1_pixels = vqadd_u8(vmovn_u16(vcombine_u16(x1y0_pixels_1b, x1y0_pixels_0b)), vmovn_u16(vcombine_u16(x1y1_pixels_1b, x1y1_pixels_0b)));
            vst1_u8(&buffer[(y_dst + y_start)*w + x_offset + x_start], vqadd_u8(x0_pixels, x1_pixels));
        }
    }
    
    return y_dst*w + x_offset;
}

inline static int16x8_t bicubicHermite(uint16x8_t* pixels, uint16x8_t w) {
    // -A/2 + 3B/2 - 3C/2 + D/2
    int16x8_t hermite_a_0 = vshrq_n_s16(vsubq_s16(pixels[3], pixels[0]), 1); // -A/2 + D/2
    int16x8_t hermite_a_1 = vshrq_n_s16(vsubq_s16(pixels[1], pixels[2]), 1); //  B/2 - C/2
    int16x8_t hermite_a_2 = vsubq_s16(pixels[1], pixels[2]);               //  B   - C
    // Sum components:
    int16x8_t hermite_a = vaddq_s16(hermite_a_0, hermite_a_1); // -A/2 + B/2 - C/2 + D/2
    hermite_a = vaddq_s16(hermite_a, hermite_a_2); // -A/2 + 3B/2 - 3C/2 + D/2
    // Power a*w^2:
    hermite_a = vshrq_n_s16(vmulq_s16(hermite_a, w), 8); // a*w
    hermite_a = vshrq_n_s16(vmulq_s16(hermite_a, w), 8); //  *w
    hermite_a = vshrq_n_s16(vmulq_s16(hermite_a, w), 8); //  *w
    
    // A - 5B/2 + 2C - D/2
    int16x8_t hermite_b_0 = vqshlq_n_s16(vsubq_s16(pixels[2], pixels[1]), 1); // -2B   + 2C
    int16x8_t hermite_b_1 = vshrq_n_s16(vaddq_s16(pixels[3], pixels[1]), 1);  //  B/2  +  D/2
    // Sum components:
    int16x8_t hermite_b = vsubq_s16(hermite_b_0, hermite_b_1); // -5B/2 + 2C - D/2
    hermite_b = vaddq_s16(hermite_b, pixels[0]); // A - 5B/2 + 2C - D/2
    // Power b*w^2:
    hermite_b = vshrq_n_s16(vmulq_s16(hermite_b, w), 8); // b*w
    hermite_b = vshrq_n_s16(vmulq_s16(hermite_b, w), 8); //  *w
    
    // -A/2 + C/2
    int16x8_t hermite_c = vqshlq_n_s16(vsubq_s16(pixels[2], pixels[0]), 1); // -A/2 + C/2
    // Power c*w^1:
    hermite_c = vshrq_n_s16(vmulq_s16(hermite_b, w), 8); // c*w
    
    // a*t^3 + b*t^2 + c*t + B
    int16x8_t hermite_sum = vaddq_s16(hermite_a, hermite_b); // a*t^3 + b*t^2
    hermite_sum = vaddq_s16(hermite_sum, hermite_c); // a*t^3 + b*t^2 + c*t
    hermite_sum = vaddq_s16(hermite_sum, pixels[1]); // a*t^3 + b*t^2 + c*t + B
    
    return hermite_sum;
}
inline static int16x8_t bicubicHermite(uint8x8_t* pixels_0, uint16x8_t w) {
    uint16x8_t pixels[4] = {
        vreinterpret_s16_u16(vmovl_u8(pixels_0[0])),
        vreinterpret_s16_u16(vmovl_u8(pixels_0[1])),
        vreinterpret_s16_u16(vmovl_u8(pixels_0[2])),
        vreinterpret_s16_u16(vmovl_u8(pixels_0[3]))
    };
    return bicubicHermite(pixels, w);
}
int getResizedFb_bicubic_8bit(uint8_t* buff_dst, uint16_t w, uint16_t h, uint16_t x_start, uint16_t y_start) {
    const auto fb_ptr = reinterpret_cast<uint16_t*>(m_fb);
    const auto fb_height = m_fb_height;
    const auto fb_width_div8 = m_fb_width/8;
    const auto fb_width = fb_width_div8*8;
    
    // Make these variables known at the end of loop
    uint16_t x_dst;
    uint16_t y_dst;
    
    // Generate helper vectors
    const uint16_t vect_rising_b[] = {0, 1, 2, 3};
    const uint16x4_t vect_rising = vld1_u16(vect_rising_b);
    const uint8x8_t  vect_0x08 =  vdup_n_u8(0x08);
    const int16x8_t vect_0xFF = vdupq_n_s16(0xFF);
    const int16x8_t vect_0x00 = vdupq_n_s16(0x00);
    // Calculate ratios minus 1. These will always be 0.0<ratio<1.0 then scaled up to 8bits
    // 1 + at the beginning ensures better precision on average.
    const uint8_t x_ratio = 1 + ((fb_width  << 8)/w) & 0xFF;
    const uint8_t y_ratio = 1 + ((fb_height << 8)/h) & 0xFF;
    // Load the framebuffer:
    if (w > h) {
        getRotatedFb(m_tmp_buffer);
        for (uint32_t y=0; y<fb_height; y++) {
            uint32_t y_dbuff = y*fb_width_div8;
            uint32_t y_fbuff = y*fb_width;
            for (uint32_t x=0; x<fb_width_div8; x++) {
                m_tmp_buffer_neon[y_dbuff + x] = vld1_u8((uint8_t*)&m_tmp_buffer[y_fbuff + x*8]);
            }
        }
    } else {
        getFb(m_tmp_buffer_neon);
    }
    for (y_dst=1; y_dst<h; y_dst++) {
        // Get y coords
        uint32_t y_fb[4];
        y_fb[1] = (uint32_t)(((y_dst * y_ratio) >> 8) + y_dst) * fb_width_div8;
        y_fb[0] = y_fb[1] - fb_width_div8;
        y_fb[2] = y_fb[1] + fb_width_div8;
        y_fb[3] = y_fb[2] + fb_width_div8;
        // Get wy weight for the coord values
        uint16x8_t wy = vdupq_n_u16((y_dst * y_ratio) & 0xFF);
        
        for (x_dst=0; x_dst<w; x_dst+=8) {
            uint16_t x_dst_div8 = x_dst >> 3;
            // Initialize our x coordinates with a rising list and add to it the current x_dst.
            // Then multiply exery resulting x coordinate with x_ratio to get a vector of x_dst.
            uint32x4_t x_data_0 = vmull_n_u16( vadd_u16( vect_rising, vdup_n_u16(x_dst  ) ), x_ratio );
            uint32x4_t x_data_1 = vmull_n_u16( vadd_u16( vect_rising, vdup_n_u16(x_dst+4) ), x_ratio );
            // The high part of the operation will contain the delta(x0) coordinates for the requested pixel in the fb.
            // This value will be actually x0-x_dst because our ratio has 1 substructed from them.
            uint16x4_t x_coord_0 = vshrn_n_u32(x_data_0, 8);
            uint16x4_t x_coord_1 = vshrn_n_u32(x_data_1, 8);
            uint16x8_t x_coord = vcombine_u16(x_coord_0, x_coord_1);
            // The low part will contain the weight for this pixel multiplied by 256.
            // Because there is no conversion of 16x4 to 8x4, we need to stitch two 16x4 into one 16x8.
            // Then we can get only the most significant bits to 8
            uint16x8_t wx = vmovl_u8(vmovn_u16(vcombine_u16( vmovn_u32(x_data_0), vmovn_u32(x_data_1))));
            
            // Get the offset to the offset, then calculate real x_fb values
            uint16_t x_coord_fb_offset = vgetq_lane_u16(x_coord, 0) >> 3;
            uint16_t x_coord_offset = x_coord_fb_offset << 3;
            uint8x8_t x_fb[4];
            x_fb[0] = vmovn_u16(vqsubq_u16(x_coord, vdupq_n_u16(x_coord_offset+1)));
            x_fb[1] = vmovn_u16( vsubq_u16(x_coord, vdupq_n_u16(x_coord_offset  )));
            x_fb[2] = vmovn_u16( vsubq_u16(x_coord, vdupq_n_u16(x_coord_offset-1)));
            x_fb[3] = vmovn_u16( vsubq_u16(x_coord, vdupq_n_u16(x_coord_offset-2)));
            x_coord_fb_offset += x_dst_div8;
            
            // Get the maximum offset in x_fb, so we know how many 8byte chunks of the fb we need to process.
            uint8_t imax = (vget_lane_u8(x_fb[3], 7) + 8) >> 3;
            int16x8_t hermites[4];
            uint8x8_t pixels[4];
            for (uint8_t y=0; y<4; y++) {
                for (uint8_t x=0; x<4; x++) {
                    uint8x8_t sv_offset = x_fb[x];
                    for (uint8_t i=0; i<imax; i++) {
                        uint8x8_t fb_data = m_tmp_buffer_neon[y_fb[y] + x_coord_fb_offset + i];
                        
                        if (i == 0) {
                            pixels[x] = vtbl1_u8(fb_data, sv_offset);
                        } else {
                            sv_offset = vsub_u8(sv_offset, vect_0x08);
                            pixels[x] = veor_u8(pixels[x], vtbl1_u8(fb_data, sv_offset));
                        }
                    }
                }
                hermites[y] = bicubicHermite(pixels, wx);
            }
    
            uint16x8_t hermites_sum = vreinterpret_u16_s16(vminq_s16(vmaxq_s16(bicubicHermite(hermites, wy), vect_0x00), vect_0xFF));
            vst1_u8(&buff_dst[(y_dst + y_start)*w + x_dst + x_start], vmovn_u16(hermites_sum));

            printf("%016x %016x %016x %016x %016x %016x %d:%d:%d:%d:%d\n", m_tmp_buffer_neon[y_fb[0] + x_coord_fb_offset], pixels[0], pixels[1], pixels[2], pixels[3], x_fb[0], x_coord_offset, vgetq_lane_u16(x_coord, 0), vget_lane_u8(x_fb[0], 0), imax, vget_lane_u8(x_fb[3], 7));
        }
    }
    return (y_dst + y_start)*w + x_dst + x_start;
}
int rotateBuffer(uint8_t* in_buffer, uint8_t* out_buffer, uint16_t w, uint16_t h) {
    uint16_t y;
    uint16_t x;
    
    for (y=0; y<h; y+=8) {
        for (x=0; x<w; x+=8) {
            uint8x8_t row[] = {
                vld1_u8(&in_buffer[x + (y+0)*w]),
                vld1_u8(&in_buffer[x + (y+1)*w]),
                vld1_u8(&in_buffer[x + (y+2)*w]),
                vld1_u8(&in_buffer[x + (y+3)*w]),
                vld1_u8(&in_buffer[x + (y+4)*w]),
                vld1_u8(&in_buffer[x + (y+5)*w]),
                vld1_u8(&in_buffer[x + (y+6)*w]),
                vld1_u8(&in_buffer[x + (y+7)*w])
            };
            vtrn8(row[0], row[1]);
            vtrn8(row[2], row[3]);
            vtrn8(row[4], row[5]);
            vtrn8(row[6], row[7]);
            
            vtrn16(row[0], row[2]);
            vtrn16(row[1], row[3]);
            vtrn16(row[4], row[6]);
            vtrn16(row[5], row[7]);
            
            vtrn32(row[0], row[4]);
            vtrn32(row[1], row[5]);
            vtrn32(row[2], row[6]);
            vtrn32(row[3], row[7]);
            
            for (uint8_t ix=0; ix<sizeof(row); ix++) {
                vst1_u8(&out_buffer[(x+ix)*h + y], row[ix]);
            }
        }
    }
    
    return x*y;
}
int getResizedFb(uint8_t* buffer, uint16_t w, uint16_t h, uint16_t x_start, uint16_t y_start, uint8_t type) {
    if (type == RESIZE_BILINEAR_16BIT) {
        return getResizedFb_bilinear_16bit(buffer, w, h, x_start, y_start);
    } else if (type == RESIZE_BILINEAR_8BIT) {
        return getResizedFb_bilinear_8bit(buffer, w, h, x_start, y_start);
    } else if (type == RESIZE_BICUBIC_8BIT) {
        return getResizedFb_bicubic_8bit(buffer, w, h, x_start, y_start);
    } else {
        return 0;
    }
}
void write(char const path[], uint8_t* buffer, uint32_t buffer_sz) {
    FILE* output_file = fopen(path, "wb");
    if (!output_file) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }
    fwrite(buffer, 1, buffer_sz, output_file);
    fclose(output_file);
}
uint8_t buffer[1404*1872*2];
//uint8_t buffer2[1404*1872*2];

int main(int argc, char* argv[]) {
    uint32_t size = 0;

    printf("Reading!\n");
    FILE* in_file = fopen("img.data", "rb");
    if (!in_file) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }
    fread(m_fb, m_fb_size, 1, in_file);
    fclose(in_file);
    printf("Read data!\n");
    
    memset(buffer, 0, m_fb_size);
    size = getFb(buffer);
    write("img.raw.data", buffer, size);
    printf("Done   copy!\n");
    
    memset(buffer, 0, m_fb_size);
    size = getResizedFb(buffer, 720, 960, 0, (1280-960)/2, RESIZE_BILINEAR_8BIT);
    write("img.8.data", buffer, size);
    printf("Done  8 bit!\n");
    
    memset(buffer, 0, m_fb_size);
    size = getResizedFb(buffer, 720, 960, 0, (1280-960)/2, RESIZE_BILINEAR_16BIT);
    write("img.16.data", buffer, size);
    printf("Done 16 bit!\n");
    
    /*memset(buffer2, 0, m_fb_size);
    size = rotateBuffer(buffer, buffer2, 720, 1280);
    write("img.16.rot.data", buffer2, size);
    printf("Done 16 bit rotated!\n");*/
    
    memset(buffer, 0, m_fb_size);
    size = getResizedFb(buffer, 720, 960, 0, (1280-960)/2, RESIZE_BICUBIC_8BIT);
    write("img.bicubic.data", buffer, size);
    printf("Done bicubic!\n");
    
    return 0;
}
