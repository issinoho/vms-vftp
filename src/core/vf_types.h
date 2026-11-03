/* vf_types.h - types shared by the core, the app and the platforms. No VMS types. */
#ifndef VF_TYPES_H
#define VF_TYPES_H

#define VF_VERSION "0.1.1"
#define VF_PCSI_VERSION "T0.1-1"

#define VF_SPEC_MAX 1024     /* a file specification, or one edited line */

/* A calendar date and time of day, local. */
typedef struct { int y, m, d, hh, mm; } vf_when;

/* Keys. Printable characters and control characters are their own codes. */
enum {
    VK_NONE = -1, VK_TIMEOUT = -2,
    VK_CTRL_A = 1, VK_CTRL_E = 5, VK_CTRL_H = 8, VK_TAB = 9, VK_RETURN = 13,
    VK_CTRL_U = 21, VK_CTRL_W = 23, VK_CTRL_Z = 26, VK_DEL = 127,
    VK_UP = 256, VK_DOWN, VK_LEFT, VK_RIGHT,
    VK_PF1, VK_PF2, VK_PF3, VK_PF4,
    VK_FIND, VK_INSERT, VK_REMOVE, VK_SELECT, VK_PREV, VK_NEXT,
    VK_HELP, VK_DO, VK_F10, VK_ENTER,
    VK_KP0, VK_KP1, VK_KP2, VK_KP3, VK_KP4, VK_KP5, VK_KP6, VK_KP7, VK_KP8, VK_KP9,
    VK_KP_MINUS, VK_KP_COMMA, VK_KP_PERIOD, VK_F5,
    VK_OTHER
};

#endif
