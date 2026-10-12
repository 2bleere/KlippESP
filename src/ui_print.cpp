// File browser, file details, print status and Z offset screens.
#include "ui.h"
#include "ui_icons.h"
#include "board.h"
#include "clock.h"
#include <time.h>

#if KLIPPESP_THUMBNAILS
// ---------------------------------------------------------------- thumbnail holder (PSRAM boards)

struct Thumb {
    MrThumb t = {nullptr, 0, 0, 0};
    lv_img_dsc_t dsc;
};

static void thumb_apply(Thumb& th, MrThumb& nt, lv_obj_t* img, lv_coord_t box) {
    if (img) lv_img_set_src(img, nullptr);
    lv_img_cache_invalidate_src(nullptr);
    if (th.t.data) free(th.t.data);
    th.t = nt;
    memset(&th.dsc, 0, sizeof(th.dsc));
    th.dsc.header.always_zero = 0;
    th.dsc.header.cf = LV_IMG_CF_RAW_ALPHA;
    th.dsc.header.w = nt.w;
    th.dsc.header.h = nt.h;
    th.dsc.data_size = nt.len;
    th.dsc.data = nt.data;
    if (img) {
        lv_img_set_src(img, &th.dsc);
        uint16_t m = nt.w > nt.h ? nt.w : nt.h;
        if (m) lv_img_set_zoom(img, (uint16_t)(256L * box / m));
    }
}

static void thumb_show(Thumb& th, lv_obj_t* img, lv_coord_t box) {
    if (!th.t.data || !img) return;
    lv_img_set_src(img, &th.dsc);
    uint16_t m = th.t.w > th.t.h ? th.t.w : th.t.h;
    if (m) lv_img_set_zoom(img, (uint16_t)(256L * box / m));
}

static Thumb detailThumb, printThumb;
static char printThumbFile[128];

void ui_print_thumb(lv_obj_t* img, lv_coord_t box) {
    static lv_obj_t* shown = nullptr;
    MrThumb nt;
    if (mr_take_thumb(THUMB_PRINT, &nt)) {
        thumb_apply(printThumb, nt, img, box);
        strlcpy(printThumbFile, g.file, sizeof(printThumbFile));
        shown = img;
    } else if (shown != img && printThumb.t.data && !strcmp(printThumbFile, g.file)) {
        thumb_show(printThumb, img, box);
        shown = img;
    }
}

bool ui_print_thumb_shown() { return printThumb.t.data && printThumb.t.len && !strcmp(printThumbFile, g.file); }
#endif

static const char* basename_of(const char* p) {
    const char* s = strrchr(p, '/');
    return s ? s + 1 : p;
}

// ================================================================ FILES

static lv_obj_t* fList;
static lv_obj_t* fCount;
static uint32_t fVer = 0;
static int fCountN;  // entries in the Moonraker file list

// detail dialog
static lv_obj_t* dOverlay;
static lv_obj_t* dInfo;
#if KLIPPESP_THUMBNAILS
static lv_obj_t* dImg;
static lv_coord_t dImgBox;
#endif
static char dFile[128];
static uint32_t dMetaVer;

static void detail_close(lv_event_t*) {
    if (!dOverlay) return;
    lv_obj_del_async(dOverlay);
    dOverlay = nullptr;
}

static void detail_print(lv_event_t*) {
    if (!dOverlay) return;
    mr_print_file(dFile);
    lv_obj_del_async(dOverlay);
    dOverlay = nullptr;
    ui_toast("Starting print...");
}

static void open_detail(const char* file) {
    strlcpy(dFile, file, sizeof(dFile));
#if KLIPPESP_THUMBNAILS
    MrThumb stale;
    if (mr_take_thumb(THUMB_DETAIL, &stale)) free(stale.data);
#endif
    MrMeta m;
    dMetaVer = mr_meta(&m);
    mr_request_meta(file);

    dOverlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(dOverlay, SW, SH);
    set_bg_color(dOverlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(dOverlay, LV_OPA_60, 0);
    lv_obj_set_style_border_width(dOverlay, 0, 0);
    lv_obj_set_style_radius(dOverlay, 0, 0);

    const lv_coord_t cw = PORTRAIT ? 308 : 464, ch = PORTRAIT ? 380 : 300;
    lv_obj_t* c = ui_card(dOverlay, 0, 0, cw, ch);
    lv_obj_center(c);
    lv_obj_t* name = ui_label(c, basename_of(file), &lv_font_montserrat_16);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(name, cw - 18);

    lv_obj_t* box = lv_obj_create(c);
    lv_obj_set_pos(box, 0, 26);
    lv_obj_set_size(box, PORTRAIT ? 90 : 140, PORTRAIT ? 90 : 140);
    set_bg_color(box, COL_BG, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* ph = ui_label(box, ICON_CUBE, &icons_28, COL_DIM);
    lv_obj_center(ph);
#if KLIPPESP_THUMBNAILS
    dImgBox = PORTRAIT ? 90 : 140;
    dImg = lv_img_create(box);
    lv_obj_center(dImg);
#endif

    dInfo = ui_label(c, "Loading...", &lv_font_montserrat_14, COL_TEXT);
    lv_obj_set_pos(dInfo, PORTRAIT ? 100 : 152, 30);
    lv_obj_set_width(dInfo, cw - 18 - (PORTRAIT ? 100 : 152));
    lv_obj_set_style_text_line_space(dInfo, 4, 0);

    lv_obj_t* r = ui_row(c, cw - 18, 50);
    lv_obj_align(r, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_btn(r, "Cancel", PORTRAIT ? 120 : 130, 46, detail_close);
    lv_obj_t* pb = ui_btn(r, LV_SYMBOL_PLAY " Print", 150, 46, detail_print, nullptr, COL_GREEN);
    if (is_printing() || !g.ready) lv_obj_add_state(pb, LV_STATE_DISABLED);
}

static void detail_update() {
    if (!dOverlay) return;
    MrMeta m;
    uint32_t v = mr_meta(&m);
    if (v != dMetaVer && !strcmp(m.file, dFile)) {
        dMetaVer = v;
        char t[24], buf[320];
        if (m.valid) {
            fmt_dur(t, sizeof(t), m.estTime);
            char done[24] = "";
            if (m.estTime > 0 && clock_valid()) {
                char hm[16];
                clock_hm(hm, sizeof(hm), time(nullptr) + (time_t)m.estTime);
                snprintf(done, sizeof(done), " (done ~%s)", hm);
            }
            snprintf(buf, sizeof(buf),
                     "Time  %s%s\nFilament  %.2f m%s\nLayers  %d\nHeight  %.1f mm\n"
                     "Temps  %d / %d " DEG "C\nSlicer  %s",
                     m.estTime > 0 ? t : "-", done, m.filamentMm / 1000.0f,
                     m.filamentG > 0 ? (String("  (") + String(m.filamentG, 1) + " g)").c_str() : "",
                     m.layers, m.height, m.extTemp, m.bedTemp, m.slicer[0] ? m.slicer : "-");
        } else {
            snprintf(buf, sizeof(buf), "No metadata available");
        }
        set_text(dInfo, buf);
    }
#if KLIPPESP_THUMBNAILS
    MrThumb nt;
    if (mr_take_thumb(THUMB_DETAIL, &nt)) thumb_apply(detailThumb, nt, dImg, dImgBox);
#endif
}

static void file_click(lv_event_t* e) {
    int i = ui_vlist_index(lv_event_get_current_target(e));
    MrFile f;
    if (mr_file(i, &f)) open_detail(f.path.c_str());
}

static void files_refresh(lv_event_t*) {
    mr_request_files();
    set_text(fCount, "Loading...");
}

static lv_obj_t* file_cell(lv_obj_t* parent) {
    lv_obj_t* b = lv_btn_create(parent);
    lv_obj_set_size(b, CONTENT_W - 24, 46);
    set_bg_color(b, COL_CARD, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_pad_all(b, 3, 0);
    lv_obj_add_event_cb(b, file_click, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* n = ui_label(b, "", &lv_font_montserrat_14);
    lv_label_set_long_mode(n, LV_LABEL_LONG_DOT);
    lv_obj_set_width(n, CONTENT_W - 40);
    lv_obj_align(n, LV_ALIGN_TOP_LEFT, 4, 2);
    lv_obj_t* d = ui_label(b, "", &lv_font_montserrat_12, COL_DIM);
    lv_obj_align(d, LV_ALIGN_BOTTOM_LEFT, 4, 0);
    return b;
}

static void file_bind(lv_obj_t* b, int i) {
    MrFile f;
    if (!mr_file(i, &f)) return;
    set_text(lv_obj_get_child(b, 0), f.path.c_str());
    time_t t = f.modified;
    struct tm tmv;
    localtime_r(&t, &tmv);  // TZ from settings (clock.cpp)
    char d[32], buf[64];
    strftime(d, sizeof(d), "%Y-%m-%d %H:%M", &tmv);
    snprintf(buf, sizeof(buf), "%s   %.1f MB", d, f.size / 1048576.0f);
    set_text(lv_obj_get_child(b, 1), buf);
}

static void files_rebuild() {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d files", fCountN);
    set_text(fCount, buf);
    ui_vlist_set_count(fList, fCountN);
}

static void files_update() {
    uint32_t ver = mr_files_version(&fCountN);
    if (ver != fVer) {
        fVer = ver;
        files_rebuild();
    }
    detail_update();
}

void scr_files(lv_obj_t* p) {
    lv_obj_t* hdr = ui_row(p, CONTENT_W - 12, 38);
    lv_obj_set_pos(hdr, 6, 2);
    fCount = ui_label(hdr, "Loading...", &lv_font_montserrat_14, COL_DIM);
    lv_obj_set_flex_grow(fCount, 1);
    lv_obj_t* rb = ui_btn(hdr, LV_SYMBOL_REFRESH " Refresh", 110, 34, files_refresh);
    lv_obj_set_style_text_font(rb, &lv_font_montserrat_14, 0);

    fList = ui_vlist(p, 0, 40, CONTENT_W, CONTENT_H - 40, CONTENT_W - 24, 46, 5, 1, file_cell, file_bind);

    fVer = mr_files_version(&fCountN);
    if (fCountN) files_rebuild();
    mr_request_files();
    ui_set_update(files_update);
}

// ================================================================ Z OFFSET

static const float zSteps[] = {0.005f, 0.01f, 0.025f, 0.05f, 0.1f};
static int zStep = 1;
static lv_obj_t* zVal;
static lv_obj_t* zStepBtns[5];
static lv_obj_t* zSave;

static void zstep_cb(lv_event_t* e) {
    zStep = (int)(intptr_t)lv_event_get_user_data(e);
    for (int i = 0; i < 5; i++) ui_select(zStepBtns[i], i == zStep);
}

static void zadj_cb(lv_event_t* e) {
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    bool homedZ = strchr(g.homed, 'z') != nullptr;
    mr_gcodef("SET_GCODE_OFFSET Z_ADJUST=%.3f%s", dir * zSteps[zStep], homedZ ? " MOVE=1" : "");
}

static void zreset_cb(lv_event_t*) {
    bool homedZ = strchr(g.homed, 'z') != nullptr;
    mr_gcodef("SET_GCODE_OFFSET Z=0%s", homedZ ? " MOVE=1" : "");
}

static void zsave_yes(void*) {
    mr_gcode(g.hasProbe ? "Z_OFFSET_APPLY_PROBE" : "Z_OFFSET_APPLY_ENDSTOP");
    mr_gcode("SAVE_CONFIG");
    ui_toast("Saving offset, Klipper will restart");
}

static void zsave_cb(lv_event_t*) {
    ui_confirm("Save Z offset",
               g.hasProbe ? "Apply the offset to the probe z_offset and run SAVE_CONFIG (restarts Klipper)?"
                          : "Apply the offset to the Z endstop position and run SAVE_CONFIG (restarts Klipper)?",
               zsave_yes);
}

static void zoffset_update() {
    char buf[48];
    snprintf(buf, sizeof(buf), "%+.3f mm", g.zOffset);
    set_text(zVal, buf);
    if (is_printing()) lv_obj_add_state(zSave, LV_STATE_DISABLED);
    else lv_obj_clear_state(zSave, LV_STATE_DISABLED);
}

void scr_zoffset(lv_obj_t* p) {
    const bool P = PORTRAIT;
    const lv_coord_t w1 = P ? CONTENT_W - 12 : 226, h1 = P ? 150 : 268;
    lv_obj_t* c = ui_card(p, 6, 6, w1, h1);
    ui_label(c, "Current Z offset", &lv_font_montserrat_14, COL_DIM);
    zVal = ui_label(c, "", &lv_font_montserrat_28);
    lv_obj_set_pos(zVal, 0, 24);
    lv_obj_t* hint = ui_label(c, P ? "Positive raises the nozzle; applied immediately."
                                   : "Positive values raise the\nnozzle. Applied immediately,\nalso during a print.",
                              &lv_font_montserrat_12, COL_DIM);
    lv_obj_set_pos(hint, 0, P ? 62 : 70);
    lv_obj_t* r = ui_row(c, w1 - 16, 46);
    lv_obj_align(r, LV_ALIGN_BOTTOM_MID, 0, 0);
    ui_btn(r, "Reset", (w1 - 22) / 2, 44, zreset_cb);
    zSave = ui_btn(r, LV_SYMBOL_SAVE " Save", (w1 - 22) / 2, 44, zsave_cb, nullptr, COL_GREEN);

    const lv_coord_t x2 = P ? 6 : 238, y2 = P ? 162 : 6, w2 = P ? CONTENT_W - 12 : 236, h2 = CONTENT_H - 6 - y2;
    const lv_coord_t iw = w2 - 16;
    lv_obj_t* s = ui_card(p, x2, y2, w2, h2);
    ui_btn(s, LV_SYMBOL_UP "  Raise nozzle", iw, 74, zadj_cb, (void*)(intptr_t)1);
    lv_obj_t* lower = ui_btn(s, LV_SYMBOL_DOWN "  Lower nozzle", iw, 74, zadj_cb, (void*)(intptr_t)-1);
    lv_obj_set_pos(lower, 0, 80);
    lv_obj_t* l = ui_label(s, "Step (mm)", &lv_font_montserrat_12, COL_DIM);
    lv_obj_set_pos(l, 0, 162);
    lv_obj_t* sr = ui_row(s, iw, h2 - 16 - 180);
    lv_obj_align(sr, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_pad_column(sr, 3, 0);
    static const char* names[] = {"0.005", "0.01", "0.025", "0.05", "0.1"};
    for (int i = 0; i < 5; i++) {
        zStepBtns[i] = ui_btn(sr, names[i], (iw - 12) / 5, h2 - 16 - 184, zstep_cb, (void*)(intptr_t)i);
        lv_obj_set_style_text_font(zStepBtns[i], &lv_font_montserrat_12, 0);
        ui_select(zStepBtns[i], i == zStep);
    }
    ui_set_update(zoffset_update);
}
