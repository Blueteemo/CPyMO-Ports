#ifndef CPYMO_GARLIC_ADAPTER_H
#define CPYMO_GARLIC_ADAPTER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Only original RG35XX / GarlicOS 1.4.x. No device-specific SDK headers. */
#define GARLIC_OUTPUT_WIDTH 640
#define GARLIC_OUTPUT_HEIGHT 480

enum garlic_button {
    GARLIC_BUTTON_A      = 1u << 0,
    GARLIC_BUTTON_B      = 1u << 1,
    GARLIC_BUTTON_X      = 1u << 2,
    GARLIC_BUTTON_Y      = 1u << 3,
    GARLIC_BUTTON_UP     = 1u << 4,
    GARLIC_BUTTON_DOWN   = 1u << 5,
    GARLIC_BUTTON_LEFT   = 1u << 6,
    GARLIC_BUTTON_RIGHT  = 1u << 7,
    GARLIC_BUTTON_L      = 1u << 8,
    GARLIC_BUTTON_R      = 1u << 9,
    GARLIC_BUTTON_START  = 1u << 10,
    GARLIC_BUTTON_SELECT = 1u << 11,
    GARLIC_BUTTON_L2 = 1u << 12,
    GARLIC_BUTTON_R2 = 1u << 13
};

typedef struct garlic_actions {
    bool up, down, left, right;
    bool ok, cancel, skip, hide_window;
    /* One-shot keys; the caller clears them when the press is consumed. */
    bool quick_save, quick_load, auto_play, settings;
} garlic_actions;

typedef struct garlic_viewport {
    int x, y, w, h;
} garlic_viewport;

/* RG35XX story keys differ from menu/choice navigation. MENU belongs to
 * RetroArch; its hotkey gate must suppress core input while MENU is held. */
garlic_actions garlic_map_buttons(uint32_t buttons, bool ui, bool script_menu,
    bool pick_a_slot);

/* Full contain / letterbox, no distortion; dimensions can exceed screen. */
bool garlic_compute_viewport(int src_w, int src_h,
                             int screen_w, int screen_h,
                             garlic_viewport *out);

/* Source: 32-bit BGRA bytes like current CPyMO libretro soft_image
   (B at +0, G at +1, R at +2). Destination: native-endian RGB565.
   Returns false and does not draw for invalid parameters. */
bool garlic_blit_rgb565(uint16_t *dst, size_t dst_pitch_bytes,
                        int dst_w, int dst_h,
                        const uint8_t *src, size_t src_pitch_bytes,
                        int src_w, int src_h);

#endif
