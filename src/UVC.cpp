/* class UVC
 * Public functions
 */
#include "UVC.h"

UVC::UVC(char *devname, uint16_t maxpkt, uint8_t nbufs) {
    g_nbufs = nbufs;
    g_max_packet_size = maxpkt;
    
    g_uvc_fd = open(devname, O_RDWR | O_NONBLOCK);
    if (g_uvc_fd == -1) {
        throw std::invalid_argument(strerror(errno));
    }
    struct v4l2_capability cap;
    if ((ioctl(g_uvc_fd, VIDIOC_QUERYCAP, &cap) < 0) || !(cap.capabilities & V4L2_CAP_VIDEO_OUTPUT)) {
        close(g_uvc_fd);
        throw std::invalid_argument("Selected device is incapable of video output");
    }
    LOG("Selected device is called "<<cap.card<<" and is located at bus "<<cap.bus_info);
    
    struct v4l2_event_subscription sub;
    memset(&sub, 0, sizeof(struct v4l2_event_subscription));
    sub.type = UVC_EVENT_SETUP;
    ioctl(g_uvc_fd, VIDIOC_SUBSCRIBE_EVENT, &sub);
    sub.type = UVC_EVENT_DATA;
    ioctl(g_uvc_fd, VIDIOC_SUBSCRIBE_EVENT, &sub);
    sub.type = UVC_EVENT_STREAMON;
    ioctl(g_uvc_fd, VIDIOC_SUBSCRIBE_EVENT, &sub);
    sub.type = UVC_EVENT_STREAMOFF;
    ioctl(g_uvc_fd, VIDIOC_SUBSCRIBE_EVENT, &sub);
    
    // Declare stream formats
    g_formats.add(V4L2_PIX_FMT_MJPEG, 1404, 1872, 200000);
    g_formats.add(V4L2_PIX_FMT_MJPEG, 1872, 1404, 200000);
    fill_streaming_control(&g_probe, g_formats[0]);
    fill_streaming_control(&g_commit, g_formats[0]);
}
UVC::~UVC() {
    close(g_uvc_fd);
}
void UVC::set_formats(UVCFormats formats) {
}
void UVC::loop_main() {
    fd_set fdsu;
    FD_ZERO(&fdsu);
    FD_SET(g_uvc_fd, &fdsu);
    fd_set efds = fdsu;
    fd_set dfds = fdsu;
    
    if (select(g_uvc_fd + 1, NULL, &dfds, &efds, NULL) <=0) {
        if (errno == EINTR) {
            throw std::invalid_argument(strerror(errno));
        }
    }
    if (FD_ISSET(g_uvc_fd, &efds)) {
        events_process();
    } else if (FD_ISSET(g_uvc_fd, &dfds)) {
        video_process();
    }
}
