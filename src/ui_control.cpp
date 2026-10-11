// Temperature, move, extrude, fans/speed, macros and power-device screens.
#include "ui.h"

static lv_obj_t* make_scroll_col(lv_obj_t* p, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h) {
    lv_obj_t* c = lv_obj_create(p);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_set_style_pad_row(c, 5, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(c, LV_DIR_VER);
    return c;
}

// ================================================================ TEMPERATURE

struct Preset { const char* name; int ext, bed; };
static const Preset presets[] = {{"PLA", 200, 60}, {"PETG", 240, 80}, {"ABS", 250, 100}, {"TPU", 225, 50}};

// heaters shown on the temperature screen: 0 = extruder, 1 = bed, 2.. = settable extras
static int tSel = 0;
static float tPending = -1;
static uint32_t tPendingAt = 0;
static lv_obj_t* tBtns[2 + MR_MAX_EXTRA];
static int tMap[2 + MR_MAX_EXTRA];  // button index -> heater id (-1 ext, -2 bed, >=0 extra)
static int tCount;
static lv_obj_t *tName, *tTarget, *tCur, *tSensors;

static MrTemp* heater_by_id(int id) {
    if (id == -1) return &g.ext;
    if (id == -2) return &g.bed;
    return id < g.nExtra ? &g.extra[id] : nullptr;
}

static void set_heater(const MrTemp& h, float t) {
    if (t < 0) t = 0;
    if (!strncmp(h.obj, "temperature_fan ", 16))
        mr_gcodef("SET_TEMPERATURE_FAN_TARGET TEMPERATURE_FAN=%s TARGET=%.0f", h.label, t);
    else if (!strncmp(h.obj, "heater_generic ", 15))
        mr_gcodef("SET_HEATER_TEMPERATURE HEATER=%s TARGET=%.0f", h.label, t);
    else
        mr_gcodef("SET_HEATER_TEMPERATURE HEATER=%s TARGET=%.0f", h.obj, t);
}

static void temp_sel_cb(lv_event_t* e) {
    tSel = (int)(intptr_t)lv_event_get_user_data(e);
    tPending = -1;
    for (int i = 0; i < tCount; i++) ui_select(tBtns[i], i == tSel);
}

static void temp_apply(float t) {
    MrTemp* h = heater_by_id(tMap[tSel]);
    if (!h) return;
    if (t < 0) t = 0;
    tPending = t;
    tPendingAt = millis();
    set_heater(*h, t);
}

static void temp_delta_cb(lv_event_t* e) {
    MrTemp* h = heater_by_id(tMap[tSel]);
    if (!h) return;
    float base = tPending >= 0 ? tPending : h->target;
    temp_apply(base + (int)(intptr_t)lv_event_get_user_data(e));
}

static void temp_off_cb(lv_event_t*) { temp_apply(0); }
static void temp_num_done(float v) { temp_apply(v); }
static void temp_num_cb(lv_event_t*) {
    MrTemp* h = heater_by_id(tMap[tSel]);
    if (h) ui_numpad(h->label, h->target, temp_num_done);
}

static void preset_cb(lv_event_t* e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0) {  // cooldown
        mr_gcode("TURN_OFF_HEATERS");
        return;
    }
    set_heater(g.ext, presets[i].ext);
    if (g.hasBed) set_heater(g.bed, presets[i].bed);
}

static void temp_update() {
    char buf[96];
    for (int i = 0; i < tCount; i++) {
        MrTemp* h = heater_by_id(tMap[i]);
        if (!h) continue;
        snprintf(buf, sizeof(buf), "%s\n%.1f / %.0f" DEG, h->label, h->temp, h->target);
        ui_btn_text(tBtns[i], buf);
    }
    MrTemp* h = heater_by_id(tMap[tSel]);
    if (h) {
        if (tPending >= 0 && millis() - tPendingAt > 2500) tPending = -1;
        float tgt = tPending >= 0 ? tPending : h->target;
        set_text(tName, h->label);
        snprintf(buf, sizeof(buf), "%.0f" DEG, tgt);
        ui_btn_text(tTarget, buf);
        snprintf(buf, sizeof(buf), "Current %.1f" DEG "   Power %.0f%%", h->temp, h->power * 100);
        set_text(tCur, buf);
    }
    String s;
    for (int i = 0; i < g.nExtra; i++) {
        if (g.extra[i].settable) continue;
        snprintf(buf, sizeof(buf), "%s: %.1f" DEG "\n", g.extra[i].label, g.extra[i].temp);
        s += buf;
    }
    set_text(tSensors, s.c_str());
}

void scr_temp(lv_obj_t* p) {
    const bool P = PORTRAIT;  // portrait: heater buttons in a 2-column strip on top, controls below
    lv_obj_t* list = make_scroll_col(p, 6, 6, P ? CONTENT_W - 12 : 160, P ? 106 : 268);
    if (P) {
        lv_obj_set_flex_flow(list, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_style_pad_column(list, 4, 0);
    }
    tCount = 0;
    tMap[tCount++] = -1;
    if (g.hasBed) tMap[tCount++] = -2;
    for (int i = 0; i < g.nExtra && tCount < 2 + MR_MAX_EXTRA; i++)
        if (g.extra[i].settable) tMap[tCount++] = i;
    if (tSel >= tCount) tSel = 0;
    for (int i = 0; i < tCount; i++) {
        tBtns[i] = ui_btn(list, "", P ? (CONTENT_W - 16) / 2 : 156, 48, temp_sel_cb, (void*)(intptr_t)i);
        lv_obj_set_style_text_font(tBtns[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_align(lv_obj_get_child(tBtns[i], 0), LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_align(lv_obj_get_child(tBtns[i], 0), LV_ALIGN_LEFT_MID, 6, 0);
        ui_select(tBtns[i], i == tSel);
    }
    tSensors = ui_label(list, "", &lv_font_montserrat_12, COL_DIM);

    lv_obj_t* c = P ? ui_card(p, 6, 118, CONTENT_W - 12, CONTENT_H - 124) : ui_card(p, 172, 6, 302, 268);
    tName = ui_label(c, "", &lv_font_montserrat_14, COL_DIM);
    tTarget = ui_btn(c, "", 120, 50, temp_num_cb, nullptr, COL_BG);
    lv_obj_set_style_text_font(tTarget, &lv_font_montserrat_28, 0);
    lv_obj_set_pos(tTarget, 0, 20);
    tCur = ui_label(c, "", &lv_font_montserrat_12, COL_DIM);
    lv_obj_set_pos(tCur, 130, 24);
    lv_obj_t* hint = ui_label(c, "tap the target to type", &lv_font_montserrat_12, COL_DIM);
    lv_obj_set_pos(hint, 130, 46);

    lv_obj_t* r = ui_row(c, 286, 46);
    lv_obj_set_pos(r, 0, 78);
    lv_obj_set_style_pad_column(r, 4, 0);
    static const int deltas[] = {-10, -5, -1, 1, 5, 10};
    for (int d : deltas) {
        char t[8];
        snprintf(t, sizeof(t), "%+d", d);
        lv_obj_t* b = ui_btn(r, t, 44, 44, temp_delta_cb, (void*)(intptr_t)d);
        lv_obj_set_style_text_font(b, &lv_font_montserrat_14, 0);
    }

    lv_obj_t* off = ui_btn(c, "Off", 80, 36, temp_off_cb, nullptr, COL_RED);
    lv_obj_set_style_text_font(off, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(off, 0, 130);

    lv_obj_t* pl = ui_label(c, "Preheat (hotend / bed)", &lv_font_montserrat_12, COL_DIM);
    lv_obj_set_pos(pl, 0, 174);
    lv_obj_t* r3 = ui_row(c, 286, 56);
    lv_obj_align(r3, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_pad_column(r3, 4, 0);
    for (int i = 0; i < 4; i++) {
        char t[32];
        snprintf(t, sizeof(t), "%s\n%d/%d", presets[i].name, presets[i].ext, presets[i].bed);
        lv_obj_t* b = ui_btn(r3, t, 54, 54, preset_cb, (void*)(intptr_t)i, COL_ORANGE);
        lv_obj_set_style_text_font(b, &lv_font_montserrat_12, 0);
    }
    lv_obj_t* cd = ui_btn(r3, "Cool\ndown", 54, 54, preset_cb, (void*)(intptr_t)-1, COL_ACCENT);
    lv_obj_set_style_text_font(cd, &lv_font_montserrat_12, 0);

    ui_set_update(temp_update);
}

// ================================================================ MOVE

static const float mDist[] = {0.1f, 1, 10, 50, 100};
static int mDistSel = 2;
static lv_obj_t* mDistBtns[5];
static lv_obj_t* mPos;

enum { J_XP, J_XN, J_YP, J_YN, J_ZP, J_ZN };

static void jog_cb(lv_event_t* e) {
    int j = (int)(intptr_t)lv_event_get_user_data(e);
    float d = mDist[mDistSel];
    char axis = j <= J_XN ? 'X' : j <= J_YN ? 'Y' : 'Z';
    float sign = (j == J_XP || j == J_YP || j == J_ZP) ? 1 : -1;
    int feed = axis == 'Z' ? 600 : 6000;
    mr_gcodef("G91\nG1 %c%.2f F%d%s", axis, sign * d, feed, g.absolute ? "\nG90" : "");
}

static void dist_cb(lv_event_t* e) {
    mDistSel = (int)(intptr_t)lv_event_get_user_data(e);
    for (int i = 0; i < 5; i++) ui_select(mDistBtns[i], i == mDistSel);
}

static void gcode_btn_cb(lv_event_t* e) { mr_gcode((const char*)lv_event_get_user_data(e)); }
static void goto_cb(lv_event_t* e) { ui_goto((Scr)(intptr_t)lv_event_get_user_data(e)); }

static void move_update() {
    char buf[96];
    snprintf(buf, sizeof(buf), "X %.1f  Y %.1f  Z %.2f\nHomed: %s", g.pos[0], g.pos[1], g.pos[2],
             g.homed[0] ? g.homed : "none");
    set_text(mPos, buf);
}

void scr_move(lv_obj_t* p) {
    const bool P = PORTRAIT;  // portrait: bigger jog pad on top, distances + actions below
    const lv_coord_t padW = P ? CONTENT_W - 12 : 268;
    lv_obj_t* c = ui_card(p, 6, 6, padW, P ? 250 : 268);
    const int B = P ? 72 : 62, G = 6;
    struct J { const char* t; int x, y; int j; };
    const J pad[] = {
        {LV_SYMBOL_UP "\nY+", 1, 0, J_YP}, {LV_SYMBOL_LEFT " X-", 0, 1, J_XN},
        {"X+ " LV_SYMBOL_RIGHT, 2, 1, J_XP}, {LV_SYMBOL_DOWN "\nY-", 1, 2, J_YN},
    };
    for (auto& k : pad) {
        lv_obj_t* b = ui_btn(c, k.t, B, B, jog_cb, (void*)(intptr_t)k.j);
        lv_obj_set_style_text_font(b, &lv_font_montserrat_14, 0);
        lv_obj_set_pos(b, k.x * (B + G), 4 + k.y * (B + G));
    }
    lv_obj_t* hxy = ui_btn(c, LV_SYMBOL_HOME "\nXY", B, B, gcode_btn_cb, (void*)"G28 X Y", COL_ACCENT);
    lv_obj_set_style_text_font(hxy, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(hxy, B + G, 4 + B + G);

    const lv_coord_t zx = 3 * (B + G), zw = padW - 16 - zx;
    lv_obj_t* zp = ui_btn(c, LV_SYMBOL_UP "\nZ+", zw, B, jog_cb, (void*)(intptr_t)J_ZP);
    lv_obj_set_pos(zp, zx, 4);
    lv_obj_t* hz = ui_btn(c, LV_SYMBOL_HOME "\nZ", zw, B, gcode_btn_cb, (void*)"G28 Z", COL_ACCENT);
    lv_obj_set_pos(hz, zx, 4 + B + G);
    lv_obj_t* zn = ui_btn(c, LV_SYMBOL_DOWN "\nZ-", zw, B, jog_cb, (void*)(intptr_t)J_ZN);
    lv_obj_set_pos(zn, zx, 4 + 2 * (B + G));
    lv_obj_t* zb[] = {zp, hz, zn};
    for (lv_obj_t* b : zb) lv_obj_set_style_text_font(b, &lv_font_montserrat_14, 0);

    const lv_coord_t rw = P ? CONTENT_W - 12 : 194, iw = rw - 16;
    lv_obj_t* r = P ? ui_card(p, 6, 262, rw, CONTENT_H - 268) : ui_card(p, 280, 6, rw, 268);
    mPos = ui_label(r, "", &lv_font_montserrat_12);
    lv_obj_t* dl = ui_label(r, "Distance (mm)", &lv_font_montserrat_12, COL_DIM);
    if (P) lv_obj_align(dl, LV_ALIGN_TOP_RIGHT, 0, 0);
    else lv_obj_set_pos(dl, 0, 36);
    lv_obj_t* dr = ui_row(r, iw, 34);
    lv_obj_set_pos(dr, 0, P ? 34 : 52);
    lv_obj_set_style_pad_column(dr, 3, 0);
    static const char* dn[] = {"0.1", "1", "10", "50", "100"};
    for (int i = 0; i < 5; i++) {
        mDistBtns[i] = ui_btn(dr, dn[i], (iw - 12) / 5, 34, dist_cb, (void*)(intptr_t)i);
        lv_obj_set_style_text_font(mDistBtns[i], &lv_font_montserrat_12, 0);
        ui_select(mDistBtns[i], i == mDistSel);
    }

    lv_obj_t* col = P ? make_scroll_col(r, 0, 74, iw, CONTENT_H - 268 - 16 - 74) : make_scroll_col(r, 0, 94, iw, 158);
    if (P) {
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_style_pad_column(col, 4, 0);
    }
    auto act = [&](const char* t, lv_event_cb_t cb, void* ud, lv_color_t colr) {
        lv_obj_t* b = ui_btn(col, t, P ? (iw - 4) / 2 : iw, 36, cb, ud, colr);
        lv_obj_set_style_text_font(b, &lv_font_montserrat_14, 0);
    };
    act(LV_SYMBOL_HOME " Home All", gcode_btn_cb, (void*)"G28", COL_ACCENT);
    if (g.hasQgl) act("Quad Gantry Level", gcode_btn_cb, (void*)"QUAD_GANTRY_LEVEL", COL_BTN);
    if (g.hasZTilt) act("Z Tilt Adjust", gcode_btn_cb, (void*)"Z_TILT_ADJUST", COL_BTN);
    if (g.hasBedMesh) act("Bed Mesh Calibrate", gcode_btn_cb, (void*)"BED_MESH_CALIBRATE", COL_BTN);
    act(LV_SYMBOL_UP LV_SYMBOL_DOWN " Z Offset", goto_cb, (void*)(intptr_t)S_ZOFFSET, COL_BTN);
    act(LV_SYMBOL_CLOSE " Disable Motors", gcode_btn_cb, (void*)"M84", COL_BTN);

    ui_set_update(move_update);
}

// ================================================================ EXTRUDE

static const int eLen[] = {5, 10, 25, 50, 100};
static const int eSpd[] = {1, 2, 5, 10};
static int eLenSel = 1, eSpdSel = 2;
static lv_obj_t* eLenBtns[5];
static lv_obj_t* eSpdBtns[4];
static lv_obj_t *eTemp, *eWarn, *eBtnE, *eBtnR;

static void elen_cb(lv_event_t* e) {
    eLenSel = (int)(intptr_t)lv_event_get_user_data(e);
    for (int i = 0; i < 5; i++) ui_select(eLenBtns[i], i == eLenSel);
}
static void espd_cb(lv_event_t* e) {
    eSpdSel = (int)(intptr_t)lv_event_get_user_data(e);
    for (int i = 0; i < 4; i++) ui_select(eSpdBtns[i], i == eSpdSel);
}
static void extrude_cb(lv_event_t* e) {
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    mr_gcodef("M83\nG1 E%d F%d", dir * eLen[eLenSel], eSpd[eSpdSel] * 60);
}
static void eheat_cb(lv_event_t* e) { set_heater(g.ext, (int)(intptr_t)lv_event_get_user_data(e)); }

static bool has_macro(const char* n) {
    for (int i = 0; i < g.nMacros; i++)
        if (!strcasecmp(g.macros[i], n)) return true;
    return false;
}

static void extrude_update() {
    char buf[64];
    snprintf(buf, sizeof(buf), LV_SYMBOL_TINT " Extruder  %.1f / %.0f" DEG, g.ext.temp, g.ext.target);
    set_text(eTemp, buf);
    if (g.canExtrude) {
        lv_obj_add_flag(eWarn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_state(eBtnE, LV_STATE_DISABLED);
        lv_obj_clear_state(eBtnR, LV_STATE_DISABLED);
    } else {
        lv_obj_clear_flag(eWarn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_state(eBtnE, LV_STATE_DISABLED);
        lv_obj_add_state(eBtnR, LV_STATE_DISABLED);
    }
}

void scr_extrude(lv_obj_t* p) {
    const bool P = PORTRAIT;
    const lv_coord_t fw = CONTENT_W - 12, fiw = fw - 16;
    lv_obj_t* c = ui_card(p, 6, 6, fw, P ? 92 : 74);
    eTemp = ui_label(c, "", &lv_font_montserrat_16);
    eWarn = ui_label(c, LV_SYMBOL_WARNING " Too cold to extrude", &lv_font_montserrat_12, COL_ORANGE);
    if (P) lv_obj_set_pos(eWarn, 0, 22);
    else lv_obj_align(eWarn, LV_ALIGN_TOP_RIGHT, 0, 2);
    lv_obj_t* hr = ui_row(c, fiw, 32);
    lv_obj_align(hr, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    static const int heats[] = {200, 220, 240, 250};
    for (int t : heats) {
        char b[16];
        snprintf(b, sizeof(b), "%d" DEG, t);
        lv_obj_t* bt = ui_btn(hr, b, (fiw - 24) / 5, 32, eheat_cb, (void*)(intptr_t)t, COL_ORANGE);
        lv_obj_set_style_text_font(bt, &lv_font_montserrat_14, 0);
    }
    lv_obj_t* off = ui_btn(hr, "Off", (fiw - 24) / 5, 32, eheat_cb, (void*)(intptr_t)0, COL_ACCENT);
    lv_obj_set_style_text_font(off, &lv_font_montserrat_14, 0);

    const lv_coord_t sw = P ? fw : 236, siw = sw - 16;
    lv_obj_t* s = P ? ui_card(p, 6, 104, sw, 150) : ui_card(p, 6, 86, sw, 188);
    ui_label(s, "Length (mm)", &lv_font_montserrat_12, COL_DIM);
    lv_obj_t* lr = ui_row(s, siw, 42);
    lv_obj_set_pos(lr, 0, 18);
    lv_obj_set_style_pad_column(lr, 4, 0);
    for (int i = 0; i < 5; i++) {
        char b[8];
        snprintf(b, sizeof(b), "%d", eLen[i]);
        eLenBtns[i] = ui_btn(lr, b, (siw - 16) / 5, 42, elen_cb, (void*)(intptr_t)i);
        lv_obj_set_style_text_font(eLenBtns[i], &lv_font_montserrat_14, 0);
        ui_select(eLenBtns[i], i == eLenSel);
    }
    lv_obj_t* sl = ui_label(s, "Speed (mm/s)", &lv_font_montserrat_12, COL_DIM);
    lv_obj_set_pos(sl, 0, P ? 66 : 76);
    lv_obj_t* sr = ui_row(s, siw, 42);
    lv_obj_set_pos(sr, 0, P ? 84 : 94);
    lv_obj_set_style_pad_column(sr, 4, 0);
    for (int i = 0; i < 4; i++) {
        char b[8];
        snprintf(b, sizeof(b), "%d", eSpd[i]);
        eSpdBtns[i] = ui_btn(sr, b, (siw - 12) / 4, 42, espd_cb, (void*)(intptr_t)i);
        lv_obj_set_style_text_font(eSpdBtns[i], &lv_font_montserrat_14, 0);
        ui_select(eSpdBtns[i], i == eSpdSel);
    }

    const lv_coord_t aw = P ? fw : 226, bw = (aw - 16 - 6) / 2;
    lv_obj_t* a = P ? ui_card(p, 6, 260, aw, CONTENT_H - 266) : ui_card(p, 248, 86, aw, 188);
    eBtnE = ui_btn(a, LV_SYMBOL_DOWN "\nExtrude", bw, P ? 76 : 82, extrude_cb, (void*)(intptr_t)1, COL_GREEN);
    eBtnR = ui_btn(a, LV_SYMBOL_UP "\nRetract", bw, P ? 76 : 82, extrude_cb, (void*)(intptr_t)-1);
    lv_obj_set_pos(eBtnR, bw + 6, 0);
    lv_obj_set_style_text_font(eBtnE, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_font(eBtnR, &lv_font_montserrat_14, 0);
    if (has_macro("LOAD_FILAMENT")) {
        lv_obj_t* b = ui_btn(a, "Load\nfilament", bw, P ? 70 : 80, gcode_btn_cb, (void*)"LOAD_FILAMENT");
        lv_obj_set_style_text_font(b, &lv_font_montserrat_14, 0);
        lv_obj_set_pos(b, 0, P ? 82 : 92);
    }
    if (has_macro("UNLOAD_FILAMENT")) {
        lv_obj_t* b = ui_btn(a, "Unload\nfilament", bw, P ? 70 : 80, gcode_btn_cb, (void*)"UNLOAD_FILAMENT");
        lv_obj_set_style_text_font(b, &lv_font_montserrat_14, 0);
        lv_obj_set_pos(b, bw + 6, P ? 82 : 92);
    }
    ui_set_update(extrude_update);
}

// ================================================================ FANS & SPEED

struct SliderRow {
    lv_obj_t* slider;
    lv_obj_t* val;
    uint32_t touchedAt;
};
static SliderRow fRows[MR_MAX_FANS + 2];
static int fCount;
static int fFanIdx[MR_MAX_FANS + 2];  // >=0 fan index, -1 speed factor, -2 flow

static void slider_changed(lv_event_t* e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    int v = lv_slider_get_value(fRows[i].slider);
    char b[12];
    snprintf(b, sizeof(b), "%d%%", v);
    set_text(fRows[i].val, b);
    fRows[i].touchedAt = millis();
}

static void slider_released(lv_event_t* e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    int v = lv_slider_get_value(fRows[i].slider);
    fRows[i].touchedAt = millis();
    int idx = fFanIdx[i];
    if (idx >= g.nFans) return;
    if (idx == -1) mr_gcodef("M220 S%d", v);
    else if (idx == -2) mr_gcodef("M221 S%d", v);
    else if (g.fans[idx].kind == FAN_PART) mr_gcodef("M106 S%d", (int)(v * 255 / 100));
    else if (g.fans[idx].kind == FAN_GENERIC) mr_gcodef("SET_FAN_SPEED FAN=%s SPEED=%.2f", g.fans[idx].label, v / 100.0f);
}

static void add_slider_row(lv_obj_t* col, const char* name, int min, int max, int idx, bool readonly) {
    int i = fCount++;
    fFanIdx[i] = idx;
    fRows[i].touchedAt = 0;
    lv_obj_t* r = ui_row(col, CONTENT_W - 24, 46);
    set_bg_color(r, COL_CARD, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(r, 8, 0);
    lv_obj_set_style_pad_hor(r, 10, 0);
    lv_obj_set_style_pad_column(r, 14, 0);
    lv_obj_t* l = ui_label(r, name, &lv_font_montserrat_14);
    lv_obj_set_width(l, PORTRAIT ? 90 : 130);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_t* s = lv_slider_create(r);
    lv_obj_set_width(s, PORTRAIT ? 140 : 220);
    lv_obj_set_height(s, 12);
    lv_obj_set_ext_click_area(s, 18);
    lv_slider_set_range(s, min, max);
    if (readonly) lv_obj_add_state(s, LV_STATE_DISABLED);
    lv_obj_add_event_cb(s, slider_changed, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)i);
    lv_obj_add_event_cb(s, slider_released, LV_EVENT_RELEASED, (void*)(intptr_t)i);
    fRows[i].slider = s;
    fRows[i].val = ui_label(r, "", &lv_font_montserrat_14);
}

static void fans_update() {
    for (int i = 0; i < fCount; i++) {
        SliderRow& r = fRows[i];
        if (lv_obj_has_state(r.slider, LV_STATE_PRESSED) || millis() - r.touchedAt < 3000) continue;
        int idx = fFanIdx[i];
        if (idx >= g.nFans) continue;
        int v = idx == -1 ? (int)lroundf(g.speedFactor * 100) : idx == -2 ? (int)lroundf(g.extrudeFactor * 100)
                                                                          : (int)lroundf(g.fans[idx].speed * 100);
        lv_slider_set_value(r.slider, v, LV_ANIM_OFF);
        char b[12];
        snprintf(b, sizeof(b), "%d%%", v);
        set_text(r.val, b);
    }
}

void scr_fans(lv_obj_t* p) {
    lv_obj_t* col = make_scroll_col(p, 6, 6, CONTENT_W - 12, CONTENT_H - 12);
    fCount = 0;
    add_slider_row(col, "Speed factor", 10, 300, -1, false);
    add_slider_row(col, "Flow", 50, 150, -2, false);
    for (int i = 0; i < g.nFans; i++) add_slider_row(col, g.fans[i].label, 0, 100, i, g.fans[i].kind == FAN_READONLY);
    ui_set_update(fans_update);
}

// ================================================================ MACROS

static char macroPending[40];

static void macro_yes(void*) { mr_gcode(macroPending); }

static void macro_cb(lv_event_t* e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i >= g.nMacros) return;
    strlcpy(macroPending, g.macros[i], sizeof(macroPending));
    char msg[80];
    snprintf(msg, sizeof(msg), "Run %s?", macroPending);
    ui_confirm("Macro", msg, macro_yes);
}

void scr_macros(lv_obj_t* p) {
    lv_obj_t* c = lv_obj_create(p);
    lv_obj_set_pos(c, 0, 0);
    lv_obj_set_size(c, CONTENT_W, CONTENT_H);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 6, 0);
    lv_obj_set_style_pad_row(c, 5, 0);
    lv_obj_set_style_pad_column(c, 5, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_scroll_dir(c, LV_DIR_VER);
    if (!g.nMacros) ui_label(c, g.ready ? "No macros found" : "Printer not ready", &lv_font_montserrat_16, COL_DIM);
    for (int i = 0; i < g.nMacros; i++) {
        lv_obj_t* b = ui_btn(c, g.macros[i], PORTRAIT ? 150 : 152, 50, macro_cb, (void*)(intptr_t)i);
        lv_obj_t* l = lv_obj_get_child(b, 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(l, 144);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    }
}

// ================================================================ POWER DEVICES

static lv_obj_t* pwSw[MR_MAX_POWER];
static lv_obj_t* pwCol;
static int pwCount;
static char pwPending[32];

static void power_yes(void*) { mr_power_toggle(pwPending); }

static void power_confirm(int i) {
    if (i < 0 || i >= g.nPower) return;
    strlcpy(pwPending, g.power[i].name, sizeof(pwPending));
    char msg[80];
    snprintf(msg, sizeof(msg), "Turn %s %s?", pwPending, g.power[i].on ? "OFF" : "ON");
    ui_confirm("Power device", msg, power_yes);
}

static void power_cb(lv_event_t* e) { power_confirm((int)(intptr_t)lv_event_get_user_data(e)); }

// Home's Power tile: with a single device, toggle it (after confirming) instead of opening the list.
void ui_power_open() {
    if (g.nPower == 1) power_confirm(0);
    else ui_goto(S_POWER);
}

static void power_build() {
    lv_obj_clean(pwCol);
    pwCount = g.nPower;
    if (!pwCount) {
        ui_label(pwCol, g.online ? "No power devices configured in Moonraker\n([power ...] sections in moonraker.conf)"
                                 : "Moonraker is not reachable", &lv_font_montserrat_14, COL_DIM);
        return;
    }
    for (int i = 0; i < pwCount; i++) {
        lv_obj_t* b = ui_btn(pwCol, "", CONTENT_W - 24, 50, power_cb, (void*)(intptr_t)i, COL_CARD);
        lv_obj_t* l = lv_obj_get_child(b, 0);
        set_text(l, g.power[i].name);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 12, 0);
        pwSw[i] = lv_switch_create(b);
        lv_obj_set_size(pwSw[i], 60, 30);
        lv_obj_align(pwSw[i], LV_ALIGN_RIGHT_MID, -12, 0);
        lv_obj_clear_flag(pwSw[i], LV_OBJ_FLAG_CLICKABLE);
    }
}

static void power_update() {
    if (g.nPower != pwCount) power_build();  // devices arrive after the screen was opened
    for (int i = 0; i < pwCount && i < g.nPower; i++) {
        if (g.power[i].on) lv_obj_add_state(pwSw[i], LV_STATE_CHECKED);
        else lv_obj_clear_state(pwSw[i], LV_STATE_CHECKED);
    }
}

void scr_power(lv_obj_t* p) {
    pwCol = make_scroll_col(p, 6, 6, CONTENT_W - 12, CONTENT_H - 12);
    power_build();
    ui_set_update(power_update);
}
