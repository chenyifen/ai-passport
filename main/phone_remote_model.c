#include "phone_remote_model.h"

remote_action_t remote_action(int button, int event) {
    if (event == 3) {
        if (button == 0) return REMOTE_VOLUME_UP;
        if (button == 1) return REMOTE_VOLUME_DOWN;
    }
    if (event != 1 && event != 2) return REMOTE_NONE;
    if (button == 0) return REMOTE_PREVIOUS;
    if (button == 1) return REMOTE_NEXT;
    return button == 2 && event == 1 ? REMOTE_HOME : REMOTE_NONE;
}

size_t remote_gesture(remote_action_t action, remote_mouse_t *r, size_t cap) {
    if (action != REMOTE_NEXT && action != REMOTE_PREVIOUS && action != REMOTE_HOME) return 0;
    const size_t needed = action == REMOTE_HOME ? 41 : 67;
    if (!r || cap < needed) return 0;
    size_t n = 0;
    r[n++] = (remote_mouse_t){0};
    /* Saturate at the top-left edge. No click is sent during positioning. */
    for (int i = 0; i < 24; ++i) r[n++] = (remote_mouse_t){0, -127, -127, 0};
    for (int i = 0; i < 8; ++i) r[n++] = (remote_mouse_t){0, 12, 0, 0};
    for (int i = 0; i < 8; ++i) r[n++] = (remote_mouse_t){0, 0, 20, 0};
    if (action == REMOTE_HOME) return n;
    r[n++] = (remote_mouse_t){1, 0, 0, 0};
    for (int i = 0; i < 24; ++i)
        r[n++] = (remote_mouse_t){1, 0, action == REMOTE_NEXT ? -9 : 9, 0};
    r[n++] = (remote_mouse_t){0};
    return n;
}

