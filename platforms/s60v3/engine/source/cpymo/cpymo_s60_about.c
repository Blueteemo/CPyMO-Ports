#include "cpymo_prelude.h"
#ifdef S60V3
#include "cpymo_engine.h"
#include "../cpymo-backends/sdl1/cpymo_s60_app.h"
#include "../cpymo-backends/sdl1/cpymo_s60_text_metrics.h"

enum { about_line_count = 8 };
static const struct {
    const char *label;
    float size;
    float center_y;
} about_lines[about_line_count] = {
    {"PyMO / CPyMO", 25, 30},
    {"S60v3  1.0.0", 16, 56},
    {"S60v3 移植：千岚之夏（Blueteemo）", 16, 90},
    {"PyMO：chen_xin_ming", 16, 115},
    {"CPyMO：Seng Jik 及贡献者", 16, 140},
    {"游戏与汉化署名照原样保留", 14, 171},
    {"许可详情请见附带文档", 14, 193},
    {"返回", 17, 0}
};

typedef struct {
    cpymo_backend_text text[about_line_count];
    float ink[about_line_count][4];
} cpymo_s60_about_page;

static bool about_active;

bool cpymo_s60_about_is_open(void)
{
    return about_active;
}

static void about_delete(cpymo_engine *e, void *data)
{
    (void)e;
    cpymo_s60_about_page *page = data;
    for (size_t i = 0; i < about_line_count; ++i)
        if (page->text[i]) cpymo_backend_text_free(page->text[i]);
    about_active = false;
}

static error_t about_update(cpymo_engine *e, void *data, float dt)
{
    (void)data;
    (void)dt;
    if (CPYMO_INPUT_JUST_RELEASED(e, cancel) || CPYMO_INPUT_JUST_RELEASED(e, ok))
        cpymo_ui_exit(e);
    return CPYMO_ERR_SUCC;
}

static void about_draw(const cpymo_engine *e, const void *data)
{
    const cpymo_s60_about_page *page = data;
    const float width = e->gameconfig.imagesize_w;
    const float height = e->gameconfig.imagesize_h;
    const cpymo_color background = {14, 21, 32};
    const cpymo_color dim_text = {177, 191, 207};
    const cpymo_color accent = {106, 190, 239};
    const float bg[] = {0, 0, width, height};
    const float content_offset = 5.0f;
    const float separator[] = {(width - 36) * 0.5f, 71 + content_offset, 36, 1};
    cpymo_backend_image_fill_rects(bg, 1, background, 1.0f,
        cpymo_backend_image_draw_type_ui_bg);
    cpymo_backend_image_fill_rects(separator, 1, accent, 1.0f,
        cpymo_backend_image_draw_type_ui_bg);
    for (size_t i = 0; i < about_line_count; ++i) {
        const float *ink = page->ink[i];
        const bool softkey = i == about_line_count - 1;
        const float x = softkey ? width - 6 - ink[2] : (width - ink[0] - ink[2]) * 0.5f;
        const float y = softkey ? height - 5 - ink[3] :
            about_lines[i].center_y + content_offset - (ink[1] + ink[3]) * 0.5f;
        cpymo_backend_text_draw(page->text[i], x, y,
            i == 1 || i == 5 || i == 6 ? dim_text : cpymo_color_white,
            1.0f, cpymo_backend_image_draw_type_ui_element);
    }
}

error_t cpymo_s60_about_open(cpymo_engine *e)
{
    cpymo_s60_about_page *page = NULL;
    error_t err = cpymo_ui_enter((void **)&page, e, sizeof(*page),
        &about_update, &about_draw, &about_delete);
    CPYMO_THROW(err);
    *page = (cpymo_s60_about_page){0};
    for (size_t i = 0; i < about_line_count; ++i) {
        float width;
        err = cpymo_backend_text_create(&page->text[i], &width,
            cpymo_str_pure(about_lines[i].label), about_lines[i].size);
        if (err != CPYMO_ERR_SUCC) {
            cpymo_ui_exit(e);
            return err;
        }
        cpymo_s60_text_ink(page->text[i], page->ink[i]);
    }
    about_active = true;
    return CPYMO_ERR_SUCC;
}
#endif
