#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { REMOTE_NONE, REMOTE_PREVIOUS, REMOTE_NEXT, REMOTE_VOLUME_UP,
               REMOTE_VOLUME_DOWN, REMOTE_HOME } remote_action_t;
typedef struct { uint8_t buttons; int8_t x, y, wheel; } remote_mouse_t;

/* Button and event integers match bsp_button.h. Long OK is handled by the app. */
remote_action_t remote_action(int button, int event);
/* Fully specified relative drag, including a release before home and at the end. */
size_t remote_gesture(remote_action_t action, remote_mouse_t *reports, size_t capacity);
