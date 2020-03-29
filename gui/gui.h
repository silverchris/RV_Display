#ifndef GUI_H
#define GUI_H

#include "lvgl/lvgl.h"

#ifdef __cplusplus

#include "ha/ha.hpp"

class lvgl_light {
public:
    lvgl_light(lv_obj_t *parent, ha_entity *entity);

    lv_obj_t *label;
    lv_obj_t *icon;
    lv_obj_t *btn;
    ha_entity *entity_ptr;

    void callback(lv_obj_t *obj, lv_event_t event);

    void refresh();
};

extern "C" {
#endif

void gui_init();

void main_tab(lv_obj_t *parent);

void light_tab(lv_obj_t *parent);

void update_tanks(int16_t water, int16_t grey, int16_t black, int16_t lpg);

void update_battery(int16_t battery);

extern lv_style_t style_tv_btn_rel;
extern lv_style_t style_tv_btn_pr;
extern lv_style_t style_symbol;
extern lv_style_t style_temperature;
extern lv_style_t style_label_small;
extern lv_style_t style_preload;


void style_setup(void);

#ifdef __cplusplus
}
#endif


#endif