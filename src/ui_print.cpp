// File browser, file details, print status and Z offset screens.
#include "ui.h"
#include "ui_icons.h"
#include "board.h"
#include <time.h>

// ---------------------------------------------------------------- thumbnail holder

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

static const char* basename_of(const char* p) {
    const char* s = strrchr(p, '/');
    return s ? s + 1 : p;
}

// ================================================================ FILES

static lv_obj_t* fList;
static lv_obj_t* fCount;
static uint32_t fVer = 0;
static std::vector<MrFile> fFiles;

// detail dialog
static lv_obj_t* dOverlay;
static lv_obj_t* dImg;
static lv_obj_t* dInfo;
static char dFile[128];
static uint32_t dMetaVer;

static void detail_close(lv_event_t*) {
    if (!dOverlay) return;
    lv_obj_del_async(dOverlay);
    dOverlay = nullptr;
    dImg = nullptr;
}

static void detail_print(lv_event_t*) {
    if (!dOverlay) return;
    mr_print_file(dFile);
    lv_obj_del_async(dOverlay);
    dOverlay = nullptr;
    dImg = nullptr;
    ui_toast("Starting print...");
}

static void open_detail(const char* file) {
    strlcpy(dFile, file, sizeof(dFile));
    MrThumb stale;
    if (mr_take_thumb(THUMB_DETAIL, &stale)) free(stale.data);
    MrMeta m;
    dMetaVer = mr_meta(&m);
    mr_request_meta(file);

    dOverlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(dOverlay, LCD_W, LCD_H);
    set_bg_color(dOverlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(dOverlay, LV_OPA_60, 0);
    lv_obj_set_style_border_width(dOverlay, 0, 0);
    lv_obj_set_style_radius(dOverlay, 0, 0);

    lv_obj_t* c = ui_card(dOverlay, 0, 0, 464, 300);
    lv_obj_center(c);
    lv_obj_t* name = ui_label(c, basename_of(file), &lv_font_montserrat_16);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(name, 446);

    lv_obj_t* box = lv_obj_create(c);
    lv_obj_set_pos(box, 0, 26);
    lv_obj_set_size(box, 140, 140);
    set_bg_color(box, COL_BG, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* ph = ui_label(box, ICON_CUBE, &icons_28, COL_DIM);
    lv_obj_center(ph);
    dImg = lv_img_create(box);
    lv_obj_center(dImg);

    dInfo = ui_label(c, "Loading...", &lv_font_montserrat_14, COL_TEXT);
    lv_obj_set_pos(dInfo, 152, 30);
    lv_obj_set_width(dInfo, 294);
    lv_obj_set_style_text_line_space(dInfo, 4, 0);

    lv_obj_t* r = ui_row(c, 446, 50);
    lv_obj_align(r, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_btn(r, "Cancel", 130, 46, detail_close);
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
            snprintf(buf, sizeof(buf),
                     "Time  %s\nFilament  %.2f m%s\nLayers  %d\nHeight  %.1f mm\n"
                     "Temps  %d / %d " DEG "C\nSlicer  %s",
                     m.estTime > 0 ? t : "-", m.filamentMm / 1000.0f,
                     m.filamentG > 0 ? (String("  (") + String(m.filamentG, 1) + " g)").c_str() : "",
                     m.layers, m.height, m.extTemp, m.bedTemp, m.slicer[0] ? m.slicer : "-");
        } else {
            snprintf(buf, sizeof(buf), "No metadata available");
        }
        set_text(dInfo, buf);
    }
    MrThumb nt;
    if (mr_take_thumb(THUMB_DETAIL, &nt)) thumb_apply(detailThumb, nt, dImg, 136);
}

static void file_click(lv_event_t* e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i >= 0 && i < (int)fFiles.size()) open_detail(fFiles[i].path.c_str());
}

static void files_refresh(lv_event_t*) {
    mr_request_files();
    set_text(fCount, "Loading...");
}

static void files_rebuild() {
    lv_obj_clean(fList);
    char buf[64];
    snprintf(buf, sizeof(buf), "%u files", (unsigned)fFiles.size());
    set_text(fCount, buf);
    for (size_t i = 0; i < fFiles.size(); i++) {
        const MrFile& f = fFiles[i];
        lv_obj_t* b = lv_btn_create(fList);
        lv_obj_set_size(b, 456, 46);
        set_bg_color(b, COL_CARD, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_set_style_pad_all(b, 3, 0);
        lv_obj_add_event_cb(b, file_click, LV_EVENT_CLICKED, (void*)(intptr_t)i);

        lv_obj_t* n = ui_label(b, f.path.c_str(), &lv_font_montserrat_14);
        lv_label_set_long_mode(n, LV_LABEL_LONG_DOT);
        lv_obj_set_width(n, 440);
        lv_obj_align(n, LV_ALIGN_TOP_LEFT, 4, 2);

        time_t t = f.modified;
        struct tm tmv;
        gmtime_r(&t, &tmv);
        char d[32];
        strftime(d, sizeof(d), "%Y-%m-%d %H:%M", &tmv);
        snprintf(buf, sizeof(buf), "%s   %.1f MB", d, f.size / 1048576.0f);
        lv_obj_t* s = ui_label(b, buf, &lv_font_montserrat_12, COL_DIM);
        lv_obj_align(s, LV_ALIGN_BOTTOM_LEFT, 4, 0);
    }
}

static void files_update() {
    std::vector<MrFile> v;
    uint32_t ver = mr_files(v);
    if (ver != fVer) {
        fVer = ver;
        fFiles.swap(v);
        files_rebuild();
    }
    detail_update();
}

void scr_files(lv_obj_t* p) {
    lv_obj_t* hdr = ui_row(p, 468, 38);
    lv_obj_set_pos(hdr, 6, 2);
    fCount = ui_label(hdr, "Loading...", &lv_font_montserrat_14, COL_DIM);
    lv_obj_set_flex_grow(fCount, 1);
    lv_obj_t* rb = ui_btn(hdr, LV_SYMBOL_REFRESH " Refresh", 110, 34, files_refresh);
    lv_obj_set_style_text_font(rb, &lv_font_montserrat_14, 0);

    fList = lv_obj_create(p);
    lv_obj_set_pos(fList, 0, 40);
    lv_obj_set_size(fList, CONTENT_W, CONTENT_H - 40);
    lv_obj_set_style_bg_opa(fList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(fList, 0, 0);
    lv_obj_set_style_pad_all(fList, 6, 0);
    lv_obj_set_style_pad_row(fList, 5, 0);
    lv_obj_set_flex_flow(fList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(fList, LV_DIR_VER);

    std::vector<MrFile> v;
    fVer = mr_files(v);
    fFiles.swap(v);
    if (!fFiles.empty()) files_rebuild();
    mr_request_files();
    ui_set_update(files_update);
}

// ================================================================ PRINTING

static lv_obj_t *pImg, *pName, *pPct, *pBar, *pState, *pTimes, *pInfo, *pTemps, *pBtnPause, *pBtnCancel, *pBtnTune, *pBtnZ;

static void pause_cb(lv_event_t*) {
    if (!strcmp(g.pstate, "printing")) mr_post("/printer/print/pause");
    else if (!strcmp(g.pstate, "paused")) mr_post("/printer/print/resume");
    else ui_goto(S_HOME);
}
static void cancel_yes(void*) { mr_post("/printer/print/cancel"); }
static void cancel_cb(lv_event_t*) {
    if (is_printing()) ui_confirm("Cancel print", "Are you sure you want to cancel the current print?", cancel_yes);
    else if (g.file[0]) mr_print_file(g.file);  // "Reprint" when the job has finished
}
static void tune_cb(lv_event_t*) { ui_goto(S_FANS); }
static void z_cb(lv_event_t*) { ui_goto(S_ZOFFSET); }

static void printing_update() {
    char buf[200], a[24], b[24];

    MrThumb nt;
    if (mr_take_thumb(THUMB_PRINT, &nt)) {
        thumb_apply(printThumb, nt, pImg, 126);
        strlcpy(printThumbFile, g.file, sizeof(printThumbFile));
    }
    set_text(pName, g.file[0] ? basename_of(g.file) : "No active print");

    snprintf(buf, sizeof(buf), "%.0f%%", g.progress * 100);
    set_text(pPct, buf);
    lv_bar_set_value(pBar, (int)(g.progress * 1000), LV_ANIM_OFF);
    set_text(pState, g.message[0] && is_printing() ? g.message : g.pstate);
    set_text_color(pState, state_color(g.pstate));
    set_bg_color(pBar, state_color(g.pstate), LV_PART_INDICATOR);

    float remain = g.progress > 0.005f ? g.printDur / g.progress - g.printDur : 0;
    fmt_dur(a, sizeof(a), g.printDur);
    fmt_dur(b, sizeof(b), remain);
    snprintf(buf, sizeof(buf), "Elapsed %s\nLeft %s", a, g.progress > 0.005f ? b : "-");
    set_text(pTimes, buf);

    char layer[24] = "-";
    if (g.layers > 0) snprintf(layer, sizeof(layer), "%d / %d", g.layer, g.layers);
    snprintf(buf, sizeof(buf), "Layer %s\nZ %.2f mm\n%.2f m", layer, g.pos[2], g.filamentUsed / 1000.0f);
    set_text(pInfo, buf);

    float fan = 0;
    for (int i = 0; i < g.nFans; i++)
        if (g.fans[i].kind == FAN_PART) fan = g.fans[i].speed;
    snprintf(buf, sizeof(buf),
             "Hotend %.0f/%.0f" DEG "   Bed %.0f/%.0f" DEG "\n"
             "Spd %.0f%%  Flow %.0f%%  Fan %.0f%%  Z %+.3f",
             g.ext.temp, g.ext.target, g.bed.temp, g.bed.target,
             g.speedFactor * 100, g.extrudeFactor * 100, fan * 100, g.zOffset);
    set_text(pTemps, buf);

    bool printing = !strcmp(g.pstate, "printing");
    bool paused = !strcmp(g.pstate, "paused");
    ui_btn_text(pBtnPause, printing ? LV_SYMBOL_PAUSE "\nPause" : paused ? LV_SYMBOL_PLAY "\nResume" : LV_SYMBOL_HOME "\nHome");
    set_bg_color(pBtnPause, printing ? COL_ORANGE : paused ? COL_GREEN : COL_BTN, 0);
    ui_btn_text(pBtnCancel, is_printing() ? LV_SYMBOL_STOP "\nCancel" : LV_SYMBOL_REFRESH "\nReprint");
    if (!is_printing() && !g.file[0]) lv_obj_add_state(pBtnCancel, LV_STATE_DISABLED);
    else lv_obj_clear_state(pBtnCancel, LV_STATE_DISABLED);
}

void scr_printing(lv_obj_t* p) {
    lv_obj_t* l = ui_card(p, 6, 6, 146, 268);
    lv_obj_t* box = lv_obj_create(l);
    lv_obj_set_size(box, 130, 130);
    set_bg_color(box, COL_BG, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* ph = ui_label(box, ICON_CUBE, &icons_28, COL_DIM);
    lv_obj_center(ph);
    pImg = lv_img_create(box);
    lv_obj_center(pImg);
    if (!strcmp(printThumbFile, g.file)) thumb_show(printThumb, pImg, 126);

    pName = ui_label(l, "", &lv_font_montserrat_12);
    lv_label_set_long_mode(pName, LV_LABEL_LONG_DOT);
    lv_obj_set_size(pName, 130, 46);
    lv_obj_set_pos(pName, 0, 136);
    pTimes = ui_label(l, "", &lv_font_montserrat_14, COL_DIM);
    lv_obj_set_pos(pTimes, 0, 214);

    lv_obj_t* r = ui_card(p, 158, 6, 316, 118);
    pPct = ui_label(r, "", &lv_font_montserrat_28);
    pState = ui_label(r, "", &lv_font_montserrat_14);
    lv_label_set_long_mode(pState, LV_LABEL_LONG_DOT);
    lv_obj_set_width(pState, 190);
    lv_obj_set_pos(pState, 0, 38);
    pInfo = ui_label(r, "", &lv_font_montserrat_12, COL_TEXT);
    lv_obj_set_style_text_align(pInfo, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(pInfo, LV_ALIGN_TOP_RIGHT, 0, 0);
    pBar = lv_bar_create(r);
    lv_obj_set_size(pBar, 300, 16);
    lv_obj_align(pBar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_bar_set_range(pBar, 0, 1000);

    lv_obj_t* t = ui_card(p, 158, 130, 316, 66);
    pTemps = ui_label(t, "", &lv_font_montserrat_14);
    lv_obj_set_style_text_line_space(pTemps, 6, 0);

    lv_obj_t* row = ui_row(p, 316, 72);
    lv_obj_set_pos(row, 158, 202);
    lv_obj_set_style_pad_column(row, 4, 0);
    pBtnPause = ui_btn(row, "", 76, 72, pause_cb);
    pBtnCancel = ui_btn(row, "", 76, 72, cancel_cb, nullptr, COL_RED);
    pBtnTune = ui_btn(row, LV_SYMBOL_LOOP "\nTune", 76, 72, tune_cb);
    pBtnZ = ui_btn(row, LV_SYMBOL_UP LV_SYMBOL_DOWN "\nZ offset", 76, 72, z_cb);
    lv_obj_t* btns[] = {pBtnPause, pBtnCancel, pBtnTune, pBtnZ};
    for (lv_obj_t* b : btns) lv_obj_set_style_text_font(b, &lv_font_montserrat_14, 0);

    ui_set_update(printing_update);
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
    lv_obj_t* c = ui_card(p, 6, 6, 226, 268);
    ui_label(c, "Current Z offset", &lv_font_montserrat_14, COL_DIM);
    zVal = ui_label(c, "", &lv_font_montserrat_28);
    lv_obj_set_pos(zVal, 0, 24);
    lv_obj_t* hint = ui_label(c, "Positive values raise the\nnozzle. Applied immediately,\nalso during a print.",
                              &lv_font_montserrat_12, COL_DIM);
    lv_obj_set_pos(hint, 0, 70);
    lv_obj_t* r = ui_row(c, 210, 46);
    lv_obj_align(r, LV_ALIGN_BOTTOM_MID, 0, 0);
    ui_btn(r, "Reset", 102, 44, zreset_cb);
    zSave = ui_btn(r, LV_SYMBOL_SAVE " Save", 102, 44, zsave_cb, nullptr, COL_GREEN);

    lv_obj_t* s = ui_card(p, 238, 6, 236, 268);
    ui_btn(s, LV_SYMBOL_UP "  Raise nozzle", 220, 74, zadj_cb, (void*)(intptr_t)1);
    lv_obj_t* lower = ui_btn(s, LV_SYMBOL_DOWN "  Lower nozzle", 220, 74, zadj_cb, (void*)(intptr_t)-1);
    lv_obj_set_pos(lower, 0, 80);
    lv_obj_t* l = ui_label(s, "Step (mm)", &lv_font_montserrat_12, COL_DIM);
    lv_obj_set_pos(l, 0, 162);
    lv_obj_t* sr = ui_row(s, 220, 70);
    lv_obj_align(sr, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_pad_column(sr, 3, 0);
    static const char* names[] = {"0.005", "0.01", "0.025", "0.05", "0.1"};
    for (int i = 0; i < 5; i++) {
        zStepBtns[i] = ui_btn(sr, names[i], 41, 64, zstep_cb, (void*)(intptr_t)i);
        lv_obj_set_style_text_font(zStepBtns[i], &lv_font_montserrat_12, 0);
        ui_select(zStepBtns[i], i == zStep);
    }
    ui_set_update(zoffset_update);
}
