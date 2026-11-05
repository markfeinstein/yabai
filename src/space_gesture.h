#ifndef SPACE_GESTURE_H
#define SPACE_GESTURE_H

#define SPACE_GESTURE_EVENT_TAG 0x59424149LL

bool space_gesture_requires_payload(void);
bool space_gesture_perform(int direction, int count);

#endif
