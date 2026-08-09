#ifndef LV_CONF_H
#define LV_CONF_H

/* Keep LVGL's renderer monochrome from the beginning; this avoids allocating
 * an RGB framebuffer just to quantize it again for the e-paper panel. */
#define LV_COLOR_DEPTH 1
#define LV_USE_OS LV_OS_NONE
#define LV_MEM_SIZE (48U * 1024U)
#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1

#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_DRAW_SW_I1_LUM_THRESHOLD 128
#define LV_USE_LABEL 1
#define LV_USE_BAR 1
#define LV_USE_ANIM 0
#define LV_USE_SHADOW 0
#define LV_USE_GRADIENT 0

#endif

