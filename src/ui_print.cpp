// File browser, file details, print status and Z offset screens.
#include "ui.h"
#include "ui_icons.h"
#include "board.h"
#include <time.h>

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
static lv_obj_t* dInfo;
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
