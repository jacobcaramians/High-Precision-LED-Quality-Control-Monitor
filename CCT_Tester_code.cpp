/*
 * 3-SENSOR CCT & LUX MONITOR with LIVE LVGL CHART
 * Port: IIC (Pins 19 and 20)
 */

#include <Arduino.h>
#include <Wire.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include <DFRobot_ColorTemperature.h>

using namespace esp_panel::board;

// ─── Hardware Config ──────────────────────────────────────────────────────────
#define SENSOR_SDA 19
#define SENSOR_SCL 20
#define MUX_ADDR   0x70  
#define NUM_SENSORS 3    

DFRobot_ColorTemperature sensor(&Wire1);

// ─── Display ──────────────────────────────────────────────────────────────────
#define SCREEN_W 800
#define SCREEN_H 480

static lv_disp_draw_buf_t draw_buf;
static lv_color_t         lvgl_buf[SCREEN_W * SCREEN_H / 10];
static Board              *panel      = nullptr;
static bool               panel_ready = false;

// ─── UI labels & Chart ────────────────────────────────────────────────────────
static lv_obj_t *lbl_lux_val     = nullptr;
static lv_obj_t *lbl_lux_indiv   = nullptr;
static lv_obj_t *lbl_cct_val     = nullptr;
static lv_obj_t *lbl_cct_indiv   = nullptr; 
static lv_obj_t *lbl_std_cct_val = nullptr; 

static lv_obj_t *chart           = nullptr;
static lv_chart_series_t *ser_lux = nullptr;
static lv_chart_series_t *ser_cct = nullptr;

// Pop-up Alert Modal
static lv_obj_t *popup_bg        = nullptr;
static lv_obj_t *lbl_popup_msg   = nullptr;

static unsigned long last_read_ms = 0;
#define READ_INTERVAL_MS 250

// ─── Individual Sensor Calibration Tables (Piecewise LUT) ─────────────────────
const int NUM_CAL_POINTS = 6;

// Format: {Raw Sensor Reading, True Sphere Value}
float cal_table_s1[NUM_CAL_POINTS][2] = {
    {2557.0, 2132.0}, 
    {2930.0, 2719.0}, 
    {3181.0, 3073.0}, 
    {3384.0, 3384.0}, 
    {3649.0, 3875.0}, 
    {4986.0, 5345.0}
};

float cal_table_s2[NUM_CAL_POINTS][2] = {
    {2547.0, 2132.0}, 
    {2914.0, 2719.0}, 
    {3176.0, 3073.0}, 
    {3367.0, 3384.0}, 
    {3628.0, 3875.0}, 
    {5010.0, 5345.0}
};

float cal_table_s3[NUM_CAL_POINTS][2] = {
    {2619.0, 2158.0}, 
    {2919.0, 2719.0}, 
    {3193.0, 3073.0}, 
    {3388.0, 3384.0}, 
    {3658.0, 3875.0}, 
    {4954.0, 5345.0}
};

// ─── Universal Piecewise Interpolator ─────────────────────────────────────────
float calibrate_piecewise(float raw, float table[][2], int points) {
    // 1. Lower out of bounds: apply flat offset of the lowest point
    if (raw <= table[0][0]) {
        return raw + (table[0][1] - table[0][0]);
    }
    
    // 2. Upper out of bounds: apply flat offset of the highest point
    if (raw >= table[points-1][0]) {
        return raw + (table[points-1][1] - table[points-1][0]);
    }

    // 3. Find the exact segment and interpolate
    for (int i = 0; i < points - 1; i++) {
        if (raw >= table[i][0] && raw <= table[i+1][0]) {
            float x0 = table[i][0];
            float y0 = table[i][1];
            float x1 = table[i+1][0];
            float y1 = table[i+1][1];
            
            return y0 + (raw - x0) * ((y1 - y0) / (x1 - x0));
        }
    }
    return raw; 
}


// ─── LVGL flush ───────────────────────────────────────────────────────────────
void my_disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    if (panel_ready && panel && panel->getLCD()) {
        panel->getLCD()->drawBitmap(
            area->x1, area->y1,
            area->x2 - area->x1 + 1,
            area->y2 - area->y1 + 1,
            (const uint8_t *)color_p
        );
    }
    lv_disp_flush_ready(drv);
}

// ─── MUX Control Function ─────────────────────────────────────────────────────
void mux_select(uint8_t channel) {
    if (channel > 7) return;
    Wire1.beginTransmission(MUX_ADDR);
    Wire1.write(1 << channel);
    Wire1.endTransmission();
}

// ─── Math: Round to Closest Standard CCT ──────────────────────────────────────
uint16_t get_closest_std_cct(uint16_t raw_cct) {
    if (raw_cct == 0 || raw_cct == 65535) return 0;
    
    const uint16_t std_ccts[] = {2700, 3000, 3500, 4000, 5000, 5500, 6000, 6500};
    uint16_t closest = std_ccts[0];
    int min_diff = abs(raw_cct - std_ccts[0]);
    
    for (int i = 1; i < 8; i++) {
        int diff = abs(raw_cct - std_ccts[i]);
        if (diff < min_diff) {
            min_diff = diff;
            closest = std_ccts[i];
        }
    }
    return closest;
}

// ─── Build UI ─────────────────────────────────────────────────────────────────
void build_screen() {
    lv_obj_t *scr = lv_scr_act();
    // New Dark Charcoal Background
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x181818), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Multi-Sensor Monitor & Live Trends");
    lv_obj_set_style_text_color(title, lv_color_hex(0xA8A8A8), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0); 
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    // 1. Lux Card (Left) - SCALED UP
    lv_obj_t *card_lux = lv_obj_create(scr);
    lv_obj_set_size(card_lux, 245, 160); 
    lv_obj_align(card_lux, LV_ALIGN_TOP_LEFT, 20, 40);
    lv_obj_set_style_bg_color(card_lux, lv_color_hex(0x0C1222), 0);
    lv_obj_set_style_border_color(card_lux, lv_color_hex(0x081C36), 0);
    lv_obj_set_style_border_width(card_lux, 2, 0);
    lv_obj_clear_flag(card_lux, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_lux_title = lv_label_create(card_lux);
    lv_label_set_text(lbl_lux_title, "Avg Illuminance");
    lv_obj_set_style_text_color(lbl_lux_title, lv_color_hex(0x666D84), 0);
    lv_obj_set_style_text_font(lbl_lux_title, &lv_font_montserrat_16, 0); 
    lv_obj_align(lbl_lux_title, LV_ALIGN_TOP_MID, 0, 5);

    lbl_lux_val = lv_label_create(card_lux);
    lv_label_set_text(lbl_lux_val, "--- lux");
    lv_obj_set_style_text_color(lbl_lux_val, lv_color_hex(0x64FFDA), 0);
    lv_obj_set_style_text_font(lbl_lux_val, &lv_font_montserrat_28, 0); 
    lv_obj_align(lbl_lux_val, LV_ALIGN_CENTER, 0, -5);

    lbl_lux_indiv = lv_label_create(card_lux);
    lv_label_set_text(lbl_lux_indiv, "S1: --  S2: --  S3: --");
    lv_obj_set_style_text_color(lbl_lux_indiv, lv_color_hex(0x788085), 0);
    lv_obj_set_style_text_font(lbl_lux_indiv, &lv_font_montserrat_14, 0); 
    lv_obj_align(lbl_lux_indiv, LV_ALIGN_BOTTOM_MID, 0, 5);

    // 2. Raw CCT Card (Center) - SCALED UP
    lv_obj_t *card_cct = lv_obj_create(scr);
    lv_obj_set_size(card_cct, 245, 160);
    lv_obj_align(card_cct, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_bg_color(card_cct, lv_color_hex(0x0C1222), 0);
    lv_obj_set_style_border_color(card_cct, lv_color_hex(0x081C36), 0);
    lv_obj_set_style_border_width(card_cct, 2, 0);
    lv_obj_clear_flag(card_cct, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_cct_title = lv_label_create(card_cct);
    lv_label_set_text(lbl_cct_title, "Avg Corrected Temp");
    lv_obj_set_style_text_color(lbl_cct_title, lv_color_hex(0x666D84), 0);
    lv_obj_set_style_text_font(lbl_cct_title, &lv_font_montserrat_16, 0); 
    lv_obj_align(lbl_cct_title, LV_ALIGN_TOP_MID, 0, 5);

    lbl_cct_val = lv_label_create(card_cct);
    lv_label_set_text(lbl_cct_val, "---- K");
    lv_obj_set_style_text_color(lbl_cct_val, lv_color_hex(0xFFD700), 0);
    lv_obj_set_style_text_font(lbl_cct_val, &lv_font_montserrat_28, 0); 
    lv_obj_align(lbl_cct_val, LV_ALIGN_CENTER, 0, -5);

    lbl_cct_indiv = lv_label_create(card_cct);
    lv_label_set_text(lbl_cct_indiv, "S1: --  S2: --  S3: --");
    lv_obj_set_style_text_color(lbl_cct_indiv, lv_color_hex(0x788085), 0);
    lv_obj_set_style_text_font(lbl_cct_indiv, &lv_font_montserrat_14, 0); 
    lv_obj_align(lbl_cct_indiv, LV_ALIGN_BOTTOM_MID, 0, 5);

    // 3. Closest CCT rounder Card (Right) - SCALED UP
    lv_obj_t *card_std = lv_obj_create(scr);
    lv_obj_set_size(card_std, 245, 160);
    lv_obj_align(card_std, LV_ALIGN_TOP_RIGHT, -20, 40);
    lv_obj_set_style_bg_color(card_std, lv_color_hex(0x0C1222), 0);
    lv_obj_set_style_border_color(card_std, lv_color_hex(0x081C36), 0);
    lv_obj_set_style_border_width(card_std, 2, 0);
    lv_obj_clear_flag(card_std, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_std_title = lv_label_create(card_std);
    lv_label_set_text(lbl_std_title, "Closest Production CCT");
    lv_obj_set_style_text_color(lbl_std_title, lv_color_hex(0x666D84), 0);
    lv_obj_set_style_text_font(lbl_std_title, &lv_font_montserrat_16, 0); 
    lv_obj_align(lbl_std_title, LV_ALIGN_TOP_MID, 0, 5);

    lbl_std_cct_val = lv_label_create(card_std);
    lv_label_set_text(lbl_std_cct_val, "---- K");
    lv_obj_set_style_text_color(lbl_std_cct_val, lv_color_hex(0xFFA500), 0);
    lv_obj_set_style_text_font(lbl_std_cct_val, &lv_font_montserrat_28, 0); 
    lv_obj_align(lbl_std_cct_val, LV_ALIGN_CENTER, 0, -5);

    // 4. Live Trend Chart - EXPANDED TO FILL BOTTOM
    chart = lv_chart_create(scr);
    lv_obj_set_size(chart, 760, 240); // Much taller now
    lv_obj_align(chart, LV_ALIGN_BOTTOM_MID, 0, -15);
    lv_obj_set_style_bg_color(chart, lv_color_hex(0x0C1222), 0);
    lv_obj_set_style_border_color(chart, lv_color_hex(0x081C36), 0);
    
    // Hide the points on the line to make it smooth
    lv_obj_set_style_size(chart, 0, LV_PART_INDICATOR);
    
    // Configure Chart settings
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, 300); // Show 300 seconds of history
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);

    // Primary Axis (Left) for Lux
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 10000);
    // Secondary Axis (Right) for CCT
    lv_chart_set_range(chart, LV_CHART_AXIS_SECONDARY_Y, 2000, 8000);

    // Add the two lines
    ser_lux = lv_chart_add_series(chart, lv_color_hex(0x64FFDA), LV_CHART_AXIS_PRIMARY_Y);
    ser_cct = lv_chart_add_series(chart, lv_color_hex(0xFFD700), LV_CHART_AXIS_SECONDARY_Y);

    // 5. NEW POP-UP ALERT MODAL (Hidden by default)
    popup_bg = lv_obj_create(scr);
    lv_obj_set_size(popup_bg, 550, 200);
    lv_obj_center(popup_bg); // Placed dead center over the chart
    lv_obj_set_style_bg_color(popup_bg, lv_color_hex(0x0C1222), 0);
    lv_obj_set_style_border_color(popup_bg, lv_color_hex(0xC03939), 0);
    lv_obj_set_style_border_width(popup_bg, 4, 0); // Thick emphasis border
    lv_obj_set_style_radius(popup_bg, 15, 0); // Rounded corners
    lv_obj_add_flag(popup_bg, LV_OBJ_FLAG_HIDDEN); // Invisible until needed

    lbl_popup_msg = lv_label_create(popup_bg);
    lv_obj_set_style_text_font(lbl_popup_msg, &lv_font_montserrat_20, 0); 
    lv_label_set_long_mode(lbl_popup_msg, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl_popup_msg, 500);
    lv_label_set_text(lbl_popup_msg, "");
    lv_obj_center(lbl_popup_msg);
}

// ─── Setup ────────────────────────────────────────────────────────────────────
void setup() {
    panel = new Board();
    panel->init();
    if (panel->begin()) {
        panel_ready = true;
        if (panel->getBacklight()) {
            panel->getBacklight()->setBrightness(100);
        }
    }

    Wire1.begin(SENSOR_SDA, SENSOR_SCL);
    Wire1.setTimeOut(100); 
    delay(100);

    for (int i = 0; i < NUM_SENSORS; i++) {
        mux_select(i); 
        delay(20);     
        sensor.begin(); 
    }

    if (panel_ready) {
        lv_init();
        lv_disp_draw_buf_init(&draw_buf, lvgl_buf, NULL, SCREEN_W * SCREEN_H / 10);

        static lv_disp_drv_t disp_drv;
        lv_disp_drv_init(&disp_drv);
        disp_drv.hor_res  = SCREEN_W;
        disp_drv.ver_res  = SCREEN_H;
        disp_drv.flush_cb = my_disp_flush;
        disp_drv.draw_buf = &draw_buf;
        lv_disp_drv_register(&disp_drv);

        build_screen();
    }
}

// ─── Loop ─────────────────────────────────────────────────────────────────────
void loop() {
    if (panel_ready) {
        static uint32_t last_tick = millis();
        uint32_t now_tick = millis();
        lv_tick_inc(now_tick - last_tick);
        last_tick = now_tick;
        lv_timer_handler();
    }

    unsigned long now = millis();
    if (now - last_read_ms >= READ_INTERVAL_MS) {
        last_read_ms = now;

        float total_lux = 0;
        uint32_t total_cct = 0;
        int valid_count = 0;
        
        bool any_sensor_too_low = false;
        bool any_sensor_too_high = false;
        
        float lux_readings[NUM_SENSORS] = {0};
        uint16_t cct_readings[NUM_SENSORS] = {0};

        for (int i = 0; i < NUM_SENSORS; i++) {
            mux_select(i);
            delay(20); 
            
            float lux = (float)sensor.readLUX();
            uint16_t raw_cct = sensor.readCCT();
            float corrected_cct = 0;
            
            if (raw_cct != 65535 && raw_cct != 0) {
                // Route the raw reading to the correct sensor table
                if (i == 0) corrected_cct = calibrate_piecewise((float)raw_cct, cal_table_s1, NUM_CAL_POINTS);
                else if (i == 1) corrected_cct = calibrate_piecewise((float)raw_cct, cal_table_s2, NUM_CAL_POINTS);
                else if (i == 2) corrected_cct = calibrate_piecewise((float)raw_cct, cal_table_s3, NUM_CAL_POINTS);
                
                total_lux += lux;
                total_cct += (uint32_t)corrected_cct;
                valid_count++;
                
                if (lux < 500.0) any_sensor_too_low = true;
                if (lux > 10000.0) any_sensor_too_high = true;
            } else {
                corrected_cct = raw_cct; // Pass through error codes (0 or 65535) unmodified
            }

            lux_readings[i] = lux;
            cct_readings[i] = (uint16_t)corrected_cct;
        }

        float avg_lux = (valid_count > 0) ? (total_lux / valid_count) : 0;
        uint16_t avg_cct = (valid_count > 0) ? (total_cct / valid_count) : 0;

        if (panel_ready) {
            char buf_lux_indiv[128];
            char buf_cct_indiv[128];
            
            snprintf(buf_lux_indiv, sizeof(buf_lux_indiv), "S1: %.0f  S2: %.0f  S3: %.0f", 
                     lux_readings[0], lux_readings[1], lux_readings[2]);
                     
            snprintf(buf_cct_indiv, sizeof(buf_cct_indiv), "S1: %d  S2: %d  S3: %d", 
                     cct_readings[0], cct_readings[1], cct_readings[2]);

            lv_label_set_text(lbl_lux_indiv, buf_lux_indiv);
            lv_label_set_text(lbl_cct_indiv, buf_cct_indiv);

            if (avg_cct == 0 || valid_count == 0) {
                lv_label_set_text(lbl_lux_val, "---");
                lv_label_set_text(lbl_cct_val, "---");
                lv_label_set_text(lbl_std_cct_val, "---");
            } else {
                char buf_avg_lux[32], buf_avg_cct[32], buf_std[32];
                snprintf(buf_avg_lux, sizeof(buf_avg_lux), "%.1f lux", avg_lux);
                snprintf(buf_avg_cct, sizeof(buf_avg_cct), "%d K", avg_cct);
                snprintf(buf_std, sizeof(buf_std), "%d K", get_closest_std_cct(avg_cct));

                lv_label_set_text(lbl_lux_val,   buf_avg_lux);
                lv_label_set_text(lbl_cct_val,   buf_avg_cct);
                lv_label_set_text(lbl_std_cct_val, buf_std);
            }

            // ── Pop-Up Alert Logic ────────────────────────────────────────────
            bool show_alert = true;

            // 1. Initial Warm-up Reminder
            if (now < 20000) {
                lv_label_set_text(lbl_popup_msg, "INITIALIZING...\n\nPlease allow sensors to warm up for 5 minutes before testing.");
                lv_obj_set_style_text_color(lbl_popup_msg, lv_color_hex(0xC0A100), 0);
                lv_obj_set_style_border_color(popup_bg, lv_color_hex(0xC0A100), 0);
            }
            // 2. Hardware Fault Condition
            else if (avg_cct == 0 || valid_count == 0) {
                lv_label_set_text(lbl_popup_msg, "HARDWARE FAULT!\n\nSensors Unreachable. Check wiring.");
                lv_obj_set_style_text_color(lbl_popup_msg, lv_color_hex(0xC03939), 0);
                lv_obj_set_style_border_color(popup_bg, lv_color_hex(0xC03939), 0);
            } 
            // 3. Operational Warning Faults
            else if (any_sensor_too_low) {
                lv_label_set_text(lbl_popup_msg, "WARNING: TOO DARK\n\nMove light towards the sensor.");
                lv_obj_set_style_text_color(lbl_popup_msg, lv_color_hex(0xC03939), 0);
                lv_obj_set_style_border_color(popup_bg, lv_color_hex(0xC03939), 0);
            } 
            else if (any_sensor_too_high) {
                lv_label_set_text(lbl_popup_msg, "WARNING: TOO BRIGHT\n\nMove light away from the sensor.");
                lv_obj_set_style_text_color(lbl_popup_msg, lv_color_hex(0xC03939), 0);
                lv_obj_set_style_border_color(popup_bg, lv_color_hex(0xC03939), 0);
            } 
            // 4. Normal Operating State (No Alert)
            else {
                show_alert = false;
            }

            // Display or hide the pop-up box
            if (show_alert) {
                lv_obj_clear_flag(popup_bg, LV_OBJ_FLAG_HIDDEN); // Make it visible
                lv_obj_move_foreground(popup_bg); // Ensure it renders on top of the chart
            } else {
                lv_obj_add_flag(popup_bg, LV_OBJ_FLAG_HIDDEN); // Hide it
            }
            
            // Push new data points to the live chart
            lv_chart_set_next_value(chart, ser_lux, (lv_coord_t)avg_lux);
            lv_chart_set_next_value(chart, ser_cct, (lv_coord_t)avg_cct);
        }
    }
    delay(5);
}
