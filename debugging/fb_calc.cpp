#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arm_neon.h>
#include <cstring>

uint16_t m_fb_width = 1404;
uint16_t m_fb_height = 1872;
uint32_t m_fb_size = m_fb_width*m_fb_height*2;
uint32_t m_fb_size2 = m_fb_width*m_fb_height;
uint8_t m_fb[1404*1872*2];

int getResizedFb(uint8_t* buffer, uint16_t w, uint16_t h) {
    const auto fb_ptr = reinterpret_cast<uint16_t*>(m_fb);
    const auto fb_height = m_fb_height;
    const auto fb_width = m_fb_width;
    const auto fb_size = fb_height * fb_width;
    
    // Clear the output buffer
    memset(buffer, 0, w*h);
    
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
                printf("Buffer overflow!\n");
                return y_dst*w + x_offset;
            }
            uint8x8_t y_buff_0 = vld2_u8((uint8_t*)&fb_ptr[fb_offset  ]).val[0];
            uint8x8_t y_buff_1 = vld2_u8((uint8_t*)&fb_ptr[fb_offset+8]).val[0];
            // Lookup the values for the x0,y0 pixels in the fb.
            uint8x8_t x0y0_pixels_0 = vtbl1_u8(y_buff_0, x0_dst);
            uint8x8_t x0y0_pixels_1 = vtbl1_u8(y_buff_1, vsub_u8(x0_dst, vdup_n_u8(8)));
            uint8x8_t x0y0_pixels = veor_u8(x0y0_pixels_0, x0y0_pixels_1);
            // Lookup the values for the x1,y0 pixels in the fb.
            uint8x8_t x1y0_pixels_0 = vtbl1_u8(y_buff_0, x1_dst);
            uint8x8_t x1y0_pixels_1 = vtbl1_u8(y_buff_1, vsub_u8(x1_dst, vdup_n_u8(8)));
            uint8x8_t x1y0_pixels = veor_u8(x1y0_pixels_0, x1y0_pixels_1);
            // Lookup the values for the x0,y1 pixels in the fb.
            uint8x8_t x0y1_pixels_0 = vtbl1_u8(y_buff_0, x0_dst);
            uint8x8_t x0y1_pixels_1 = vtbl1_u8(y_buff_1, vsub_u8(x0_dst, vdup_n_u8(8)));
            uint8x8_t x0y1_pixels = veor_u8(x0y1_pixels_0, x0y1_pixels_1);
            // Lookup the values for the x1,y1 pixels in the fb.
            uint8x8_t x1y1_pixels_0 = vtbl1_u8(y_buff_0, x1_dst);
            uint8x8_t x1y1_pixels_1 = vtbl1_u8(y_buff_1, vsub_u8(x1_dst, vdup_n_u8(8)));
            uint8x8_t x1y1_pixels = veor_u8(x1y1_pixels_0, x1y1_pixels_1);
            
            // Multiply the pixels by their weights.
            x0y0_pixels = vshrn_n_u16(vmull_u8(x0y0_pixels, wx0y0_dst), 8);
            x0y1_pixels = vshrn_n_u16(vmull_u8(x0y1_pixels, wx0y1_dst), 8);
            x1y0_pixels = vshrn_n_u16(vmull_u8(x1y0_pixels, wx1y0_dst), 8);
            x1y1_pixels = vshrn_n_u16(vmull_u8(x1y1_pixels, wx1y1_dst), 8);
            
            // Finally sum up all the corners and write them to the buffer.
            uint8x8_t x0_pixels = vqadd_u8(x0y0_pixels, x0y1_pixels);
            uint8x8_t x1_pixels = vqadd_u8(x1y0_pixels, x1y1_pixels);
            vst1_u8(&buffer[y_dst*w + x_offset], vqadd_u8(x0_pixels, x1_pixels));
            //printf("new:[%d, %d], old:[%d, %d] %x\n", x_offset, y_dst, x_offset + x_fb_offset, y0, fb_offset);
        }
    }
    
    return w*h;
}
int getFb(uint8_t* buffer) {
    const auto fb_ptr = reinterpret_cast<uint16_t*>(m_fb);
    const auto fb_height = m_fb_height;
    const auto fb_width = m_fb_width;
    const auto fb_size = fb_height * fb_width;

    // Read 2x8x16bit pixels (2x128bits), store 2x8x8bit pixels (2x64bits)
    for (uint32_t i=0; i<fb_size/8; i++) {
        vst1_u8(&buffer[i*8], vld2_u8((uint8_t*)&fb_ptr[i*8]).val[0]);
    }
    
    return fb_size/2;
}

int main(int argc, char* argv[]) {
    uint8_t buffer[m_fb_size/2];

    FILE* in_file = fopen("img.data", "rb");
    if (!in_file) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }
    fread(m_fb, m_fb_size, 1, in_file);
    fclose(in_file);
    
    FILE* output_file = fopen("img1.data", "wb");
    if (!output_file) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }
    uint32_t buffer_sz = getFb(buffer);
    fwrite(buffer, 1, buffer_sz, output_file);
    printf("Done 1!\n");
    fclose(output_file);
    
    output_file = fopen("img2.data", "wb");
    if (!output_file) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }
    buffer_sz = getResizedFb(buffer, 720, 1280);
    fwrite(buffer, 1, buffer_sz, output_file);
    printf("Done 2!\n");
    fclose(output_file);

    return 0;
}
