#define STB_TRUETYPE_IMPLEMENTATION
#include "cpymo-backends/software/cpymo_backend_text.c"
#include "cpymo-backends/software/cpymo_backend_image.c"
#include "cpymo/cpymo_str.c"
#include "cpymo/cpymo_color.c"
#include "cpymo/cpymo_utils.c"
cpymo_backend_software_context *cpymo_backend_software_cur_context;
void cpymo_engine_trim_memory(cpymo_engine *e) { (void)e; }
int stbir_resize_uint8(const unsigned char *a, int b, int c, int d,
    unsigned char *e, int f, int g, int h, int i) { (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i; return 1; }
float cpymo_backend_software_u8f[256];
int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb");
    fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
    unsigned char *data = malloc((size_t)n); fread(data, 1, (size_t)n, f); fclose(f);
    stbtt_fontinfo fi;
    if (!stbtt_InitFont(&fi, data, stbtt_GetFontOffsetForIndex(data, 0))) { puts("init fail"); return 1; }

    for (int i = 0; i < 256; ++i) cpymo_backend_software_u8f[i] = (float)i / 255.0f;
    cpymo_backend_software_context ctx; memset(&ctx, 0, sizeof(ctx));
    cpymo_backend_software_cur_context = &ctx;
    ctx.font = &fi;
    ctx.logical_screen_w = 540; ctx.logical_screen_h = 360;
    cpymo_backend_software_image tgt; memset(&tgt, 0, sizeof(tgt));
    tgt.w = 540; tgt.h = 360; tgt.line_stride = 540 * 4; tgt.pixel_stride = 4;
    tgt.r_offset = 0; tgt.g_offset = 1; tgt.b_offset = 2; tgt.a_offset = 3;
    tgt.has_alpha_channel = true;
    tgt.pixels = malloc(540 * 360 * 4);
    ctx.render_target = &tgt;
    /* output-resolution text layer, as used by the backend */
    ctx.text_layer = true;
    ctx.text_layer_w = 640; ctx.text_layer_h = 480;
    ctx.text_layer_px = calloc(640 * 480, 4);
    ctx.text_off_x = 0;
    ctx.text_dirty_y0 = 480; ctx.text_dirty_y1 = 0;

    /* every glyph the font has, one by one, at several sizes */
    int glyphs = 0, fails = 0, total = 0, band_bad = 0, empty_band = 0;
    cpymo_backend_text t; float w;
    float scales[3] = { 2.0f, 640.0f / 540.0f, 0.8f };
    for (int si = 0; si < 3; ++si) {
      ctx.text_scale = scales[si];
      for (int size = 24; size <= 24; size += 8) {
        for (int cp = 0x20; cp < 0x10000; ++cp) {
            if (!stbtt_FindGlyphIndex(&fi, cp)) continue;
            char buf[8]; int len = 0;
            if (cp < 0x80) buf[len++] = (char)cp;
            else if (cp < 0x800) { buf[len++] = (char)(0xC0|(cp>>6)); buf[len++] = (char)(0x80|(cp&63)); }
            else { buf[len++] = (char)(0xE0|(cp>>12)); buf[len++] = (char)(0x80|((cp>>6)&63)); buf[len++] = (char)(0x80|(cp&63)); }
            buf[len] = 0;
            cpymo_str s = cpymo_str_pure(buf);
            if (cpymo_backend_text_create(&t, &w, s, (float)size) != CPYMO_ERR_SUCC) { fails++; continue; }
            glyphs++;
            /* draw at every vertical extreme inside and outside the canvas */
            float ys[] = { -50, 0, 359 };
            float xs[] = { -40, 10, 620 };
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j) {
                    cpymo_backend_text_draw(t, xs[j], ys[i], cpymo_color_white,
                        1.0f, cpymo_backend_image_draw_type_ui_element);
                    if (ctx.text_dirty_y1 > ctx.text_dirty_y0 &&
                        (ctx.text_dirty_y0 < 0 || ctx.text_dirty_y1 > 480)) band_bad++;
                    if (ctx.text_dirty_y1 < ctx.text_dirty_y0) empty_band++;
                }
            total++;
            cpymo_backend_text_free(t);
        }
      }
    }
    printf("glyph_draws=%d create_fails=%d band_bad=%d empty=%d\n",
           glyphs, fails, band_bad, empty_band);
    return fails || band_bad;
}
