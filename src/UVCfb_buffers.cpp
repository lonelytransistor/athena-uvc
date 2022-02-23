#include "UVCfb.h"

void UVCfb::video_qbuf() {
    for (int i=0; i<m_buf.size(); i++) {
        LOG("Queuing buffer "<<i);
        buffer buf = m_buf[i];
        
        struct v4l2_buffer vbuf;
        memset(&vbuf, 0, sizeof(vbuf));
        vbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        vbuf.memory = V4L2_MEMORY_MMAP;
        vbuf.m.userptr = (unsigned long)buf.start;
        vbuf.length = buf.length;
        vbuf.index = i;
        if (ioctl(g_uvc_fd, VIDIOC_QBUF, &vbuf) < 0) {
            ERR("Unable to queue the buffer "<<i);
            throw std::overflow_error(strerror(errno));
        }
        g_qbuf_count++;
    }
}
void UVCfb::video_reqbufs(uint8_t nbufs) {
    while (m_buf.size()>nbufs) {
        //delete m_buf.back().start;
        munmap(m_buf.back().start, m_buf.back().length);
        m_buf.pop_back();
    }
    // Request buffer slots
    v4l2_requestbuffers rbuf;
    memset(&rbuf, 0, sizeof(v4l2_requestbuffers));
    rbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    rbuf.memory = V4L2_MEMORY_MMAP;
    rbuf.count = nbufs;
    if (ioctl(g_uvc_fd, VIDIOC_REQBUFS, &rbuf) < 0) {
        throw std::invalid_argument(strerror(errno));
    }
    // Allocate buffers
    for (int i=0; i<rbuf.count; i++) {
        buffer buf;
        memset(&(buf.buf), 0, sizeof(struct v4l2_buffer));
        memset(&buf, 0, sizeof(buffer));

        buf.buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        buf.buf.memory = V4L2_MEMORY_MMAP;
        buf.buf.index = i;

        if (ioctl(g_uvc_fd, VIDIOC_QUERYBUF, &(buf.buf)) < 0) {
            throw std::invalid_argument(strerror(errno));
        }
        buf.start = (uint8_t*)mmap(NULL, buf.buf.length, PROT_READ|PROT_WRITE, MAP_SHARED, g_uvc_fd, buf.buf.m.offset);
        if (MAP_FAILED == buf.start) {
            ERR("MMAP failed");
            throw std::bad_alloc();
        }
        buf.length = buf.buf.length;
        
        m_buf.push_back(buf);
    }
    g_qbuf_count = 0;
    g_dqbuf_count = 0;
    // Start the transcoder thread
    if (m_buf.size()) {
        std::unique_lock<std::mutex> this_mx(m_mutex);
        if (!m_transcoder_running) {
            LOG("Starting transcoder thread");
            m_transcoder_thread = std::thread(&UVCfb::transcoder, this);
        }
    }
    LOG("Allocated "<<std::to_string(m_buf.size())<<" buffers");
}
