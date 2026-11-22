#ifndef CPYMO_S60_NATIVE_FONT_H
#define CPYMO_S60_NATIVE_FONT_H
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int advance, left, top, width, height;
    const unsigned char *pixels;
} cpymo_s60_native_glyph;

enum cpymo_s60_font_profile { CPYMO_FONT_COMBINED, CPYMO_FONT_SMOOTH };

int cpymo_s60_next_font_profile(void);
int cpymo_s60_active_font_profile(void);
int cpymo_s60_save_font_profile(int profile);

int cpymo_s60_native_font_open(void);
void cpymo_s60_native_font_close(void);
/* The pixel pointer is borrowed until the next glyph request. */
int cpymo_s60_native_font_glyph(unsigned codepoint, int size,
    cpymo_s60_native_glyph *glyph);

#ifdef __cplusplus
}
#endif
#endif
