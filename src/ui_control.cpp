// Temperature, move, extrude, fans/speed, macros and power-device screens.
#include "ui.h"

static lv_obj_t* make_scroll_col(lv_obj_t* p, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h) {
    lv_obj_t* c = lv_obj_create(p);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_set_style_pad_row(c, 8, 0);
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
    lv_obj_t* list = make_scroll_col(p, 8, 8, 280, 408);
    tCount = 0;
    tMap[tCount++] = -1;
    if (g.hasBed) tMap[tCount++] = -2;
    for (int i = 0; i < g.nExtra && tCount < 2 + MR_MAX_EXTRA; i++)
        if (g.extra[i].settable) tMap[tCount++] = i;
    if (tSel >= tCount) tSel = 0;
    for (int i = 0; i < tCount; i++) {
        tBtns[i] = ui_btn(list, "", 270, 76, temp_sel_cb, (void*)(intptr_t)i);
        lv_obj_set_style_text_align(lv_obj_get_child(tBtns[i], 0), LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_align(lv_obj_get_child(tBtns[i], 0), LV_ALIGN_LEFT_MID, 8, 0);
        ui_select(tBtns[i], i == tSel);
    }
    tSensors = ui_label(list, "", &lv_font_montserrat_16, COL_DIM);

    lv_obj_t* c = ui_card(p, 296, 8, 496, 408);
    tName = ui_label(c, "", &lv_font_montserrat_20, COL_DIM);
    tTarget = ui_btn(c, "", 200, 76, temp_num_cb, nullptr, COL_BG);
    lv_obj_set_style_text_font(tTarget, &lv_font_montserrat_48, 0);
    lv_obj_set_pos(tTarget, 0, 30);
    lv_obj_t* hint = ui_label(c, "tap to type", &lv_font_montserrat_14, COL_DIM);
    lv_obj_set_pos(hint, 60, 110);
    tCur = ui_label(c, "", &lv_font_montserrat_16, COL_DIM);
    lv_obj_set_pos(tCur, 214, 56);

    lv_obj_t* r = ui_row(c, 476, 70);
    lv_obj_set_pos(r, 0, 136);
    static const int deltas[] = {-10, -5, -1, 1, 5, 10};
    for (int d : deltas) {
        char t[8];
        snprintf(t, sizeof(t), "%+d", d);
        ui_btn(r, t, 72, 66, temp_delta_cb, (void*)(intptr_t)d);
    }

    lv_obj_t* r2 = ui_row(c, 476, 70);
    lv_obj_set_pos(r2, 0, 216);
    ui_btn(r2, "Off", 110, 66, temp_off_cb, nullptr, COL_RED);

    lv_obj_t* pl = ui_label(c, "Preheat (extruder / bed)", &lv_font_montserrat_16, COL_DIM);
    lv_obj_set_pos(pl, 0, 296);
    lv_obj_t* r3 = ui_row(c, 476, 72);
    lv_obj_align(r3, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_pad_column(r3, 6, 0);
    for (int i = 0; i < 4; i++) {
        char t[32];
        snprintf(t, sizeof(t), "%s\n%d/%d", presets[i].name, presets[i].ext, presets[i].bed);
        lv_obj_t* b = ui_btn(r3, t, 88, 68, preset_cb, (void*)(intptr_t)i, COL_ORANGE);
        lv_obj_set_style_text_font(b, &lv_font_montserrat_16, 0);
    }
    lv_obj_t* cd = ui_btn(r3, "Cool\ndown", 88, 68, preset_cb, (void*)(intptr_t)-1, COL_ACCENT);
    lv_obj_set_style_text_font(cd, &lv_font_montserrat_16, 0);

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
    snprintf(buf, sizeof(buf), "X %.2f   Y %.2f   Z %.2f\nHomed: %s", g.pos[0], g.pos[1], g.pos[2],
             g.homed[0] ? g.homed : "none");
    set_text(mPos, buf);
}

void scr_move(lv_obj_t* p) {
    lv_obj_t* c = ui_card(p, 8, 8, 470, 408);
    const int B = 108, G = 10;
    struct J { const char* t; int x, y; int j; };
    const J pad[] = {
        {LV_SYMBOL_UP "\nY+", 1, 0, J_YP}, {LV_SYMBOL_LEFT " X-", 0, 1, J_XN},
        {"X+ " LV_SYMBOL_RIGHT, 2, 1, J_XP}, {LV_SYMBOL_DOWN "\nY-", 1, 2, J_YN},
    };
    for (auto& k : pad) {
        lv_obj_t* b = ui_btn(c, k.t, B, B, jog_cb, (void*)(intptr_t)k.j);
        lv_obj_set_pos(b, k.x * (B + G), 20 + k.y * (B + G));
    }
    lv_obj_t* hxy = ui_btn(c, LV_SYMBOL_HOME "\nXY", B, B, gcode_btn_cb, (void*)"G28 X Y", COL_ACCENT);
    lv_obj_set_pos(hxy, B + G, 20 + B + G);

    lv_obj_t* zp = ui_btn(c, LV_SYMBOL_UP "\nZ+", 90, B, jog_cb, (void*)(intptr_t)J_ZP);
    lv_obj_set_pos(zp, 3 * (B + G) + 4, 20);
    lv_obj_t* hz = ui_btn(c, LV_SYMBOL_HOME "\nZ", 90, B, gcode_btn_cb, (void*)"G28 Z", COL_ACCENT);
    lv_obj_set_pos(hz, 3 * (B + G) + 4, 20 + B + G);
    lv_obj_t* zn = ui_btn(c, LV_SYMBOL_DOWN "\nZ-", 90, B, jog_cb, (void*)(intptr_t)J_ZN);
    lv_obj_set_pos(zn, 3 * (B + G) + 4, 20 + 2 * (B + G));

    lv_obj_t* r = ui_card(p, 486, 8, 306, 408);
    mPos = ui_label(r, "", &lv_font_montserrat_16);
    lv_obj_t* dl = ui_label(r, "Distance (mm)", &lv_font_montserrat_14, COL_DIM);
    lv_obj_set_pos(dl, 0, 48);
    lv_obj_t* dr = ui_row(r, 286, 56);
    lv_obj_set_pos(dr, 0, 68);
    lv_obj_set_style_pad_column(dr, 4, 0);
    static const char* dn[] = {"0.1", "1", "10", "50", "100"};
    for (int i = 0; i < 5; i++) {
        mDistBtns[i] = ui_btn(dr, dn[i], 54, 52, dist_cb, (void*)(intptr_t)i);
        lv_obj_set_style_text_font(mDistBtns[i], &lv_font_montserrat_16, 0);
        ui_select(mDistBtns[i], i == mDistSel);
    }

    lv_obj_t* col = make_scroll_col(r, 0, 134, 286, 254);
    lv_obj_set_style_pad_row(col, 6, 0);
    ui_btn(col, LV_SYMBOL_HOME " Home All", 286, 54, gcode_btn_cb, (void*)"G28", COL_ACCENT);
    if (g.hasQgl) ui_btn(col, "Quad Gantry Level", 286, 54, gcode_btn_cb, (void*)"QUAD_GANTRY_LEVEL");
    if (g.hasZTilt) ui_btn(col, "Z Tilt Adjust", 286, 54, gcode_btn_cb, (void*)"Z_TILT_ADJUST");
    if (g.hasBedMesh) ui_btn(col, "Bed Mesh Calibrate", 286, 54, gcode_btn_cb, (void*)"BED_MESH_CALIBRATE");
    ui_btn(col, LV_SYMBOL_UP LV_SYMBOL_DOWN " Z Offset", 286, 54, goto_cb, (void*)(intptr_t)S_ZOFFSET);
    ui_btn(col, LV_SYMBOL_CLOSE " Disable Motors", 286, 54, gcode_btn_cb, (void*)"M84");

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
    lv_obj_t* c = ui_card(p, 8, 8, 784, 120);
    eTemp = ui_label(c, "", &lv_font_montserrat_24);
    eWarn = ui_label(c, LV_SYMBOL_WARNING " Extruder below min. extrude temperature", &lv_font_montserrat_16, COL_ORANGE);
    lv_obj_align(eWarn, LV_ALIGN_TOP_RIGHT, 0, 4);
    lv_obj_t* hr = ui_row(c, 764, 60);
    lv_obj_align(hr, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    static const int heats[] = {200, 220, 240, 250};
    for (int t : heats) {
        char b[16];
        snprintf(b, sizeof(b), "%d" DEG, t);
        ui_btn(hr, b, 110, 56, eheat_cb, (void*)(intptr_t)t, COL_ORANGE);
    }
    ui_btn(hr, "Off", 110, 56, eheat_cb, (void*)(intptr_t)0, COL_ACCENT);

    lv_obj_t* s = ui_card(p, 8, 136, 400, 280);
    ui_label(s, "Length (mm)", &lv_font_montserrat_16, COL_DIM);
    lv_obj_t* lr = ui_row(s, 380, 64);
    lv_obj_set_pos(lr, 0, 24);
    for (int i = 0; i < 5; i++) {
        char b[8];
        snprintf(b, sizeof(b), "%d", eLen[i]);
        eLenBtns[i] = ui_btn(lr, b, 68, 60, elen_cb, (void*)(intptr_t)i);
        ui_select(eLenBtns[i], i == eLenSel);
    }
    lv_obj_t* sl = ui_label(s, "Speed (mm/s)", &lv_font_montserrat_16, COL_DIM);
    lv_obj_set_pos(sl, 0, 100);
    lv_obj_t* sr = ui_row(s, 380, 64);
    lv_obj_set_pos(sr, 0, 124);
    for (int i = 0; i < 4; i++) {
        char b[8];
        snprintf(b, sizeof(b), "%d", eSpd[i]);
        eSpdBtns[i] = ui_btn(sr, b, 86, 60, espd_cb, (void*)(intptr_t)i);
        ui_select(eSpdBtns[i], i == eSpdSel);
    }

    lv_obj_t* a = ui_card(p, 416, 136, 376, 280);
    eBtnE = ui_btn(a, LV_SYMBOL_DOWN " Extrude", 172, 120, extrude_cb, (void*)(intptr_t)1, COL_GREEN);
    eBtnR = ui_btn(a, LV_SYMBOL_UP " Retract", 172, 120, extrude_cb, (void*)(intptr_t)-1);
    lv_obj_set_pos(eBtnR, 184, 0);
    bool load = has_macro("LOAD_FILAMENT"), unload = has_macro("UNLOAD_FILAMENT");
    if (load) {
        lv_obj_t* b = ui_btn(a, "Load\nfilament", 172, 120, gcode_btn_cb, (void*)"LOAD_FILAMENT");
        lv_obj_set_pos(b, 0, 136);
    }
    if (unload) {
        lv_obj_t* b = ui_btn(a, "Unload\nfilament", 172, 120, gcode_btn_cb, (void*)"UNLOAD_FILAMENT");
        lv_obj_set_pos(b, 184, 136);
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
    lv_obj_t* r = ui_row(col, 770, 64);
    set_bg_color(r, COL_CARD, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(r, 10, 0);
    lv_obj_set_style_pad_hor(r, 14, 0);
    lv_obj_set_style_pad_column(r, 24, 0);
    lv_obj_t* l = ui_label(r, name, &lv_font_montserrat_20);
    lv_obj_set_width(l, 200);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_t* s = lv_slider_create(r);
    lv_obj_set_width(s, 400);
    lv_obj_set_height(s, 14);
    lv_obj_set_ext_click_area(s, 24);
    lv_slider_set_range(s, min, max);
    if (readonly) lv_obj_add_state(s, LV_STATE_DISABLED);
    lv_obj_add_event_cb(s, slider_changed, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)i);
    lv_obj_add_event_cb(s, slider_released, LV_EVENT_RELEASED, (void*)(intptr_t)i);
    fRows[i].slider = s;
    fRows[i].val = ui_label(r, "", &lv_font_montserrat_20);
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
    lv_obj_t* col = make_scroll_col(p, 8, 8, 784, 408);
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
    lv_obj_set_size(c, 800, CONTENT_H);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 8, 0);
    lv_obj_set_style_pad_row(c, 10, 0);
    lv_obj_set_style_pad_column(c, 10, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_scroll_dir(c, LV_DIR_VER);
    if (!g.nMacros) ui_label(c, g.ready ? "No macros found" : "Printer not ready", &lv_font_montserrat_20, COL_DIM);
    for (int i = 0; i < g.nMacros; i++) {
        lv_obj_t* b = ui_btn(c, g.macros[i], 252, 76, macro_cb, (void*)(intptr_t)i);
        lv_obj_t* l = lv_obj_get_child(b, 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(l, 236);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    }
}

// ================================================================ POWER DEVICES

static lv_obj_t* pwSw[MR_MAX_POWER];
static int pwCount;
static char pwPending[32];

static void power_yes(void*) { mr_power_toggle(pwPending); }

static void power_cb(lv_event_t* e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i >= g.nPower) return;
    strlcpy(pwPending, g.power[i].name, sizeof(pwPending));
    char msg[80];
    snprintf(msg, sizeof(msg), "Turn %s %s?", pwPending, g.power[i].on ? "OFF" : "ON");
    ui_confirm("Power device", msg, power_yes);
}

static void power_update() {
    for (int i = 0; i < pwCount && i < g.nPower; i++) {
        if (g.power[i].on) lv_obj_add_state(pwSw[i], LV_STATE_CHECKED);
        else lv_obj_clear_state(pwSw[i], LV_STATE_CHECKED);
    }
}

void scr_power(lv_obj_t* p) {
    lv_obj_t* col = make_scroll_col(p, 8, 8, 784, 408);
    pwCount = g.nPower;
    if (!pwCount) ui_label(col, "No power devices configured in Moonraker\n([power ...] sections in moonraker.conf)",
                           &lv_font_montserrat_20, COL_DIM);
    for (int i = 0; i < pwCount; i++) {
        lv_obj_t* b = ui_btn(col, "", 770, 72, power_cb, (void*)(intptr_t)i, COL_CARD);
        lv_obj_t* l = lv_obj_get_child(b, 0);
        set_text(l, g.power[i].name);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 12, 0);
        pwSw[i] = lv_switch_create(b);
        lv_obj_set_size(pwSw[i], 80, 40);
        lv_obj_align(pwSw[i], LV_ALIGN_RIGHT_MID, -12, 0);
        lv_obj_clear_flag(pwSw[i], LV_OBJ_FLAG_CLICKABLE);
    }
    ui_set_update(power_update);
}
