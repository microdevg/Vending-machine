#include "lvgl.h"

void lv_example(void) {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);

    /* 1. Franja Celeste Superior (320x57 px) */
    lv_obj_t *top_stripe = lv_obj_create(scr);
    lv_obj_set_size(top_stripe, 320, 57);
    lv_obj_align(top_stripe, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(top_stripe, lv_color_hex(0x75AADB), 0); // Celeste oficial
    lv_obj_set_style_bg_opa(top_stripe, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(top_stripe, 0, 0);
    lv_obj_set_style_radius(top_stripe, 0, 0);

    /* 2. Franja Blanca Central (320x58 px) */
    lv_obj_t *mid_stripe = lv_obj_create(scr);
    lv_obj_set_size(mid_stripe, 320, 58);
    lv_obj_align(mid_stripe, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(mid_stripe, lv_color_hex(0xFFFFFF), 0); // Blanco
    lv_obj_set_style_bg_opa(mid_stripe, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(mid_stripe, 0, 0);
    lv_obj_set_style_radius(mid_stripe, 0, 0);

    /* 3. Franja Celeste Inferior (320x57 px) */
    lv_obj_t *bot_stripe = lv_obj_create(scr);
    lv_obj_set_size(bot_stripe, 320, 57);
    lv_obj_align(bot_stripe, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bot_stripe, lv_color_hex(0x75AADB), 0); // Celeste oficial
    lv_obj_set_style_bg_opa(bot_stripe, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bot_stripe, 0, 0);
    lv_obj_set_style_radius(bot_stripe, 0, 0);

    /* 4. Texto "ARGENTINA" en el centro */
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "ARGENTINA");
    lv_obj_set_style_text_color(label, lv_color_hex(0xD4AF37), 0); // Dorado (Sol de Mayo)
    
    // Si tienes habilitadas fuentes más grandes en lv_conf.h puedes usar:
    // lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    
    lv_obj_center(label);
}