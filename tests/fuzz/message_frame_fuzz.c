#include "../../src/message_frame.h"

#include <stdint.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    (void) daemon_message_body_is_valid((const char *) data, size);

    if (size >= sizeof(struct daemon_response_frame)) {
        struct daemon_response_frame frame;
        memcpy(&frame, data, sizeof(frame));
        (void) daemon_response_frame_is_valid(&frame);
    }

    if (size >= sizeof(int)) {
        int length = 0;
        memcpy(&length, data, sizeof(length));
        (void) daemon_message_length_is_valid(length);
    }
    return 0;
}
