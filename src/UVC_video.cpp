/* class UVC
 * Video functions
 */
#include "UVC.h"

void UVC::video_set_format(struct uvc_format_info format) {
    g_fcc = format.fcc;
    g_width = format.width;
    g_height = format.height;
    
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));

    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    fmt.fmt.pix.width = g_width;
    fmt.fmt.pix.height = g_height;
    fmt.fmt.pix.pixelformat = g_fcc;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    fmt.fmt.pix.sizeimage = g_payload_size;
    if (ioctl(g_uvc_fd, VIDIOC_S_FMT, &fmt) < 0) {
        ERR("Format setup failed: "<<pixfmtstr(g_fcc)<<","<<std::to_string(g_width)<<":"<<std::to_string(g_height));
        throw std::runtime_error(strerror(errno));
    }
    LOG("New format set: "<<pixfmtstr(g_fcc)<<","<<std::to_string(g_width)<<":"<<std::to_string(g_height));
}
void UVC::video_enable_stream(bool enable) {
    g_is_streaming = enable;
    
    int type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    if (!enable) {
        LOG("Stopping stream");
        if (ioctl(g_uvc_fd, VIDIOC_STREAMOFF, &type) < 0) {
            throw std::runtime_error(strerror(errno));
        }
        video_reqbufs(0);
    } else {
        LOG("Starting stream");
        video_reqbufs(g_nbufs);
        video_qbuf();
        if (ioctl(g_uvc_fd, VIDIOC_STREAMON, &type) < 0) {
            throw std::runtime_error(strerror(errno));
        }
    }
}

void UVC::video_process() {
    if (!g_is_streaming)
        return;
    if ((g_qbuf_count-g_dqbuf_count) < 1)
        return;
    
    struct v4l2_buffer ubuf;
    memset(&ubuf, 0, sizeof(ubuf));

    ubuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ubuf.memory = V4L2_MEMORY_MMAP;
    ubuf.field = V4L2_FIELD_NONE;
    
    if (ioctl(g_uvc_fd, VIDIOC_DQBUF, &ubuf) < 0) {
        ERR("Dequeue failed");
        throw std::underflow_error(strerror(errno));
    }
    g_dqbuf_count++;
    
    fill_buffer(&ubuf);
    
    if (ioctl(g_uvc_fd, VIDIOC_QBUF, &ubuf) < 0) {
        ERR("Queue failed");
        throw std::overflow_error(strerror(errno));
    }
    g_qbuf_count++;
}
void UVC::fill_streaming_control(struct uvc_streaming_control *ctrl, struct uvc_format_info format) {
    memset(ctrl, 0, sizeof(*ctrl));
    
    ctrl->bmHint = 1;
    ctrl->bFormatIndex = format.i_format+1;
    ctrl->bFrameIndex = format.i_frame+1;
    ctrl->dwFrameInterval = format.interval;
    ctrl->dwMaxVideoFrameSize = g_payload_size;

    /* TODO: the UVC maxpayload transfer size should be filled
     * by the driver.
     */
    ctrl->dwMaxPayloadTransferSize = g_max_packet_size;
    ctrl->bmFramingInfo = 3;
    ctrl->bPreferedVersion = 1;
    ctrl->bMaxVersion = 1;
}
