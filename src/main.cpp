#include "UVCfb.h"

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    char uvc_dev[] = "/dev/video0";
    
    try {
        UVCfb uvc(uvc_dev);
        
        while (1) {
            try {
                uvc.loop();
            } catch (std::exception& e) {
                ERR(e.what());
            }
        }
    } catch (std::exception& e) {
        ERR(e.what());
    }
    return 0;
}
