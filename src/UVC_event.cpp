/* class UVC
 * Events handling functions
 */
#include "UVC.h"

void UVC::events_process_standard(struct usb_ctrlrequest *ctrl, struct uvc_request_data *resp) {
    LOG("Unimplemented");
    
    (void)ctrl;
    (void)resp;
}
void UVC::events_process_control(uint8_t req, uint8_t cs, uint8_t entity_id, uint8_t len, struct uvc_request_data *resp) {
    LOG(std::hex<<"Request: "<<(int)req<<", Entity: "<<(int)entity_id<<", cs: "<<(int)cs<<std::dec);
    
    switch (entity_id) {
        case 0:
            switch (cs) {
                case UVC_VC_REQUEST_ERROR_CODE_CONTROL:
                    /* Send the request error code last prepared. */
                    resp->data[0] = g_request_error_code.data[0];
                    resp->length = g_request_error_code.length;
                break;
                default:
                    SET_REQ_ERROR(REQ_UNHANDLED);
                break;
            }
        break;
        /* Camera terminal unit 'UVC_VC_INPUT_TERMINAL'. */
        case 1:
            switch (cs) {
            /* We support only 'UVC_CT_AE_MODE_CONTROL' for CAMERA
             * terminal, as our bmControls[0] = 2 for CT. Also we
             * support only auto exposure.
             */
                case UVC_CT_AE_MODE_CONTROL:
                    switch (req) {
                        case UVC_SET_CUR:
                            /* Incase of auto exposure, attempts to
                             * programmatically set the auto-adjusted
                             * controls are ignored.
                             */
                            resp->data[0] = 0x01;
                            resp->length = 1;
                            SET_REQ_ERROR(REQ_SUCCESS);
                        break;
                        case UVC_GET_INFO:
                            /* TODO: We support Set and Get requests, but
                             * don't support async updates on an video
                             * status (interrupt) endpoint as of
                             * now.
                             */
                            resp->data[0] = 0x03;
                            resp->length = 1;
                            SET_REQ_ERROR(REQ_SUCCESS);
                        break;
                        case UVC_GET_CUR:
                        case UVC_GET_DEF:
                        case UVC_GET_RES:
                            /* Auto Mode auto Exposure Time, auto Iris. */
                            resp->data[0] = 0x02;
                            resp->length = 1;
                            SET_REQ_ERROR(REQ_SUCCESS);
                        break;
                        default:
                            /* We don't support this control, so STALL the
                             * control ep.
                             */
                            resp->length = -EL2HLT;
                            SET_REQ_ERROR(REQ_UNSUPPORTED);
                        break;
                    }
                break;
                default:
                    resp->length = -EL2HLT;
                    SET_REQ_ERROR(REQ_UNHANDLED);
                break;
            }
        break;
        /* processing unit 'UVC_VC_PROCESSING_UNIT' */
        case 2:
            switch (cs) {
                /* We support only 'UVC_PU_BRIGHTNESS_CONTROL' for Processing
                 * Unit, as our bmControls[0] = 1 for PU.
                 */
                case UVC_PU_BRIGHTNESS_CONTROL:
                    switch (req) {
                        case UVC_SET_CUR:
                            resp->data[0] = 0x00;
                            resp->length = len;
                            SET_REQ_ERROR(REQ_SUCCESS);
                        break;
                        case UVC_GET_MIN:
                            resp->data[0] = PU_BRIGHTNESS_MIN_VAL;
                            resp->length = 2;
                            SET_REQ_ERROR(REQ_SUCCESS);
                        break;
                        case UVC_GET_MAX:
                            resp->data[0] = PU_BRIGHTNESS_MAX_VAL;
                            resp->length = 2;
                            SET_REQ_ERROR(REQ_SUCCESS);
                        break;
                        case UVC_GET_CUR:
                            resp->data[0] = g_brightness_val&0xff;
                            resp->data[1] = 0;
                            resp->length = 2;
                            SET_REQ_ERROR(REQ_SUCCESS);
                        break;
                        case UVC_GET_INFO:
                            resp->data[0] = 0x03;
                            resp->length = 1;
                            SET_REQ_ERROR(REQ_SUCCESS);
                        break;
                        case UVC_GET_DEF:
                            resp->data[0] = PU_BRIGHTNESS_DEFAULT_VAL;
                            resp->length = 2;
                            SET_REQ_ERROR(REQ_SUCCESS);
                        break;
                        case UVC_GET_RES:
                            resp->data[0] = PU_BRIGHTNESS_STEP_SIZE;
                            resp->length = 2;
                            SET_REQ_ERROR(REQ_SUCCESS);
                        break;
                        default:
                            resp->length = -EL2HLT;
                            SET_REQ_ERROR(REQ_UNSUPPORTED);
                        break;
                    }
                break;
                default:
                    resp->length = -EL2HLT;
                    SET_REQ_ERROR(REQ_UNHANDLED);
                break;
            }
        break;
        default:
            SET_REQ_ERROR(REQ_UNHANDLED);
        break;
    }
}
void UVC::events_process_streaming(uint8_t req, uint8_t cs, struct uvc_request_data *resp) {
    LOG(std::hex<<"Request: "<<(int)req<<", cs: "<<(int)cs<<std::dec);
    
    if (cs != UVC_VS_PROBE_CONTROL && cs != UVC_VS_COMMIT_CONTROL)
        return;

    struct uvc_streaming_control* ctrl = (struct uvc_streaming_control *)&resp->data;
    resp->length = sizeof(*ctrl);

    switch (req) {
        case UVC_SET_CUR:
            g_control = cs;
            resp->length = 34;
        break;
        case UVC_GET_CUR:
            if (cs == UVC_VS_PROBE_CONTROL) {
                memcpy(ctrl, &g_probe, sizeof(*ctrl));
            } else {
                memcpy(ctrl, &g_commit, sizeof(*ctrl));
            }
        break;
        case UVC_GET_MAX:
            fill_streaming_control(ctrl, g_formats[-1]);
        break;
        case UVC_GET_MIN:
            fill_streaming_control(ctrl, g_formats[0]);
        break;
        case UVC_GET_DEF:
            fill_streaming_control(ctrl, g_formats[0]);
        break;
        case UVC_GET_RES:
            memset(ctrl, 0x00, sizeof(*ctrl));
        break;
        case UVC_GET_LEN:
            resp->data[0] = 0x00;
            resp->data[1] = 0x22;
            resp->length = 2;
        break;
        case UVC_GET_INFO:
            resp->data[0] = 0x03;
            resp->length = 1;
        break;
    }
}
void UVC::events_process_setup(struct usb_ctrlrequest *ctrl, struct uvc_request_data *resp) {
    g_control = 0;
    
    switch (ctrl->bRequestType & USB_TYPE_MASK) {
        case USB_TYPE_STANDARD:
            events_process_standard(ctrl, resp);
        break;
        case USB_TYPE_CLASS:
            if ((ctrl->bRequestType & USB_RECIP_MASK) != USB_RECIP_INTERFACE) {
                return;
            }
            switch (ctrl->wIndex & 0xff) {
                case UVC_INTF_CONTROL:
                    events_process_control(ctrl->bRequest,
                                    ctrl->wValue >> 8,
                                    ctrl->wIndex >> 8,
                                    ctrl->wLength, resp);
                break;
                case UVC_INTF_STREAMING:
                    events_process_streaming(ctrl->bRequest,
                                    ctrl->wValue >> 8, resp);
                break;
                default:
                break;
            }
        break;
        default:
        break;
    }
}
void UVC::events_process_control_data(uint8_t cs, uint8_t entity_id, struct uvc_request_data *data) {
    LOG("Entity: "<<(int)entity_id<<", cs: "<<(int)cs<<std::dec);
    
    switch (entity_id) {
        /* Processing unit 'UVC_VC_PROCESSING_UNIT'. */
        case 2:
            switch (cs) {
                /* We support only 'UVC_PU_BRIGHTNESS_CONTROL' for Processing
                * Unit, as our bmControls[0] = 1 for PU.
                */
                case UVC_PU_BRIGHTNESS_CONTROL:
                    g_brightness_val = data->data[0];
                break;
                default:
                break;
            }
        break;
        default:
        break;
    }
}
void UVC::events_process_data(struct uvc_request_data *data) {
    struct uvc_streaming_control *target;
    struct uvc_streaming_control *ctrl;
    struct uvc_format_info format;

    switch (g_control) {
        case UVC_VS_PROBE_CONTROL:
            LOG("Setting probe control of length "<<std::to_string(data->length));
            target = &g_probe;
        break;
        case UVC_VS_COMMIT_CONTROL:
            LOG("Setting commit control of length "<<std::to_string(data->length));
            target = &g_commit;
        break;
        default:
            LOG("Setting unknown control of length "<<std::to_string(data->length));
            /* As we support only BRIGHTNESS control, this request is
             * for setting BRIGHTNESS control.
             * Check for any invalid SET_CUR(BRIGHTNESS) requests
             * from Host. Note that we support Brightness levels
             * from 0x0 to 0x10 in a step of 0x1. So, any request
             * with value greater than 0x10 is invalid.
             */
            unsigned int *val = (unsigned int *)data->data;
            if (*val > PU_BRIGHTNESS_MAX_VAL) {
                throw std::invalid_argument("Value too large");
            } else {
                events_process_control_data(UVC_PU_BRIGHTNESS_CONTROL, 2, data);
            }
        return;
    }

    ctrl = (struct uvc_streaming_control*)&data->data;
    format = g_formats.get(ctrl->bFormatIndex-1, ctrl->bFrameIndex-1, ctrl->dwFrameInterval);

    target->bFormatIndex = format.i_format+1;
    target->bFrameIndex = format.i_frame+1;
    target->dwMaxVideoFrameSize = g_payload_size;
    target->dwFrameInterval = format.interval;
    if (g_control == UVC_VS_COMMIT_CONTROL) {
        video_set_format(format);
    }
}
void UVC::events_process() {
    struct v4l2_event v4l2_event;
    struct uvc_event *uvc_event = (struct uvc_event *)&v4l2_event.u.data;

    if (ioctl(g_uvc_fd, VIDIOC_DQEVENT, &v4l2_event)) {
        throw std::underflow_error(strerror(errno));
    }
    
    struct uvc_request_data resp;
    memset(&resp, 0, sizeof(resp));
    resp.length = -EL2HLT;

    try {
        switch (v4l2_event.type) {
            case UVC_EVENT_CONNECT:
                return;
            break;
            case UVC_EVENT_DISCONNECT:
                ERR("UVC_EVENT_DISCONNECT");
                return;
            break;
            case UVC_EVENT_SETUP:
                events_process_setup(&uvc_event->req, &resp);
            break;
            case UVC_EVENT_DATA:
                events_process_data(&uvc_event->data);
                return;
            break;
            case UVC_EVENT_STREAMON:
                video_enable_stream(true);
                return;
            break;
            case UVC_EVENT_STREAMOFF:
                if (g_is_streaming) {
                    video_enable_stream(false);
                } else {
                    ERR("UVC_EVENT_STREAMOFF: Stream already stopped");
                }
                return;
            break;
        }
    } catch (std::exception& e) {
        ERR(std::hex<<"An exception occured while processing event ("<<(int)v4l2_event.type<<"): "<<e.what()<<std::dec);
    }
    
    LOG("Responding with length of: "<<std::to_string(resp.length));
    if (ioctl(g_uvc_fd, UVCIOC_SEND_RESPONSE, &resp) < 0) {
        throw std::invalid_argument(strerror(errno));
    }
}
