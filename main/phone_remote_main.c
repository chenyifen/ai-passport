#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_battery.h"
#include "phone_remote_ble.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lvgl.h"

typedef struct { bsp_btn_t button; bsp_btn_ev_t event; int64_t at; } input_t;
static QueueHandle_t inputs;
static bool ble_ok, reset_confirm, help;
static int64_t action_at;
static const char *last_action="Ready";
static lv_obj_t *battery,*status,*detail,*hint,*phone;

static void on_key(bsp_btn_t button,bsp_btn_ev_t event,void *arg) {
    (void)arg;input_t input={button,event,esp_timer_get_time()};
    (void)xQueueSend(inputs,&input,0);
}
static lv_obj_t *label(lv_obj_t *parent,int x,int y,int w,const lv_font_t *font,uint32_t color,const char *text) {
    lv_obj_t *o=lv_label_create(parent);lv_obj_set_pos(o,x,y);lv_obj_set_width(o,w);
    lv_obj_set_style_text_font(o,font,0);lv_obj_set_style_text_color(o,lv_color_hex(color),0);
    lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0);lv_label_set_text(o,text);return o;
}
static void ui_create(void) {
    lv_obj_t *screen=lv_obj_create(NULL);lv_obj_remove_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen,lv_color_hex(0x101A29),0);lv_obj_set_style_pad_all(screen,0,0);
    (void)label(screen,10,18,165,&lv_font_montserrat_14,0xB8C8DD,"SWIPE REMOTE");
    battery=label(screen,178,18,48,&lv_font_montserrat_14,0xB8C8DD,"--");
    phone=lv_obj_create(screen);lv_obj_set_pos(phone,86,49);lv_obj_set_size(phone,68,83);
    lv_obj_set_style_bg_color(phone,lv_color_hex(0x192A40),0);lv_obj_set_style_radius(phone,12,0);
    lv_obj_set_style_border_width(phone,2,0);lv_obj_set_style_pad_all(phone,0,0);
    lv_obj_remove_flag(phone,LV_OBJ_FLAG_SCROLLABLE);
    (void)label(phone,0,12,64,&lv_font_montserrat_20,0x81D8C1,"^\nv");
    status=label(screen,10,148,220,&lv_font_montserrat_20,0xF2F6FB,"");
    detail=label(screen,10,185,220,&lv_font_montserrat_14,0x81D8C1,"");
    hint=label(screen,10,247,220,&lv_font_montserrat_14,0x8194AE,"");
    lv_screen_load(screen);
}
static void render(int64_t now) {
    static int soc=-1;static int64_t battery_at;
    if(!battery_at||now-battery_at>30000000){soc=bsp_battery_soc();battery_at=now;}
    if(!bsp_lvgl_lock(300))return;
    if(soc>=0)lv_label_set_text_fmt(battery,"%d%%",soc);else lv_label_set_text(battery,"--");
    bool ready=ble_ok&&remote_ble_ready();int pass=ble_ok?remote_ble_passkey():-1;
    lv_obj_set_style_border_color(phone,lv_color_hex(ready?0x81D8C1:0x8194AE),0);
    if(reset_confirm){
        lv_label_set_text(status,"Forget iPhone?");lv_label_set_text(detail,"UP: yes\nDOWN: cancel");
        lv_label_set_text(hint,"Then forget on iPhone\nand pair again");
    }else if(pass>=0){
        lv_label_set_text_fmt(status,"%06d",pass);lv_label_set_text(detail,"Enter this code on iPhone\nFirst pairing only");
        lv_label_set_text(hint,"Keep this screen open\nPassport Remote");
    }else if(help){
        lv_label_set_text(status,"SETUP GUIDE");lv_label_set_text(detail,"iPhone > AssistiveTouch\nDevices > Bluetooth Devices");
        lv_label_set_text(hint,"Enable AssistiveTouch\nDrag Lock: OFF\nHOLD OK: close guide");
    }else{
        lv_label_set_text(status,ready?"CONNECTED":ble_ok?"PAIR / RECONNECT":"BLUETOOTH ERROR");
        lv_label_set_text(detail,ready?(now-action_at<2500000?last_action:"UP: prev  DOWN: next"):"iPhone > AssistiveTouch\nDevices > Bluetooth Devices");
        lv_label_set_text(hint,"HOLD UP/DOWN: volume +/-\nOK: center | 2x OK: re-pair\nHOLD OK: setup guide");
    }
    bsp_lvgl_unlock();
}
static void handle_input(const input_t *input) {
    if(esp_timer_get_time()-input->at>1200000)return;
    if(reset_confirm){
        if(input->event==BSP_BTN_CLICK&&input->button==BSP_BTN_UP){if(ble_ok)(void)remote_ble_forget();reset_confirm=false;}
        if(input->event==BSP_BTN_CLICK&&input->button==BSP_BTN_DOWN)reset_confirm=false;
        return;
    }
    if(input->button==BSP_BTN_OK&&input->event==BSP_BTN_DOUBLE){reset_confirm=true;return;}
    if(input->button==BSP_BTN_OK&&input->event==BSP_BTN_LONG){help=!help;return;}
    if(help)return;
    remote_action_t action=remote_action(input->button,input->event);if(action==REMOTE_NONE)return;
    esp_err_t result=ble_ok?remote_ble_action(action):ESP_ERR_INVALID_STATE;
    static const char *actions[]={"Ready","Previous video","Next video","Volume +","Volume -","Pointer centered"};
    last_action=result==ESP_OK?actions[action]:"Not sent - check link";action_at=esp_timer_get_time();
}
void app_main(void) {
    ESP_LOGI("phone_remote","Standalone iPhone swipe remote");
    ESP_ERROR_CHECK(nvs_flash_init());
    inputs=xQueueCreate(8,sizeof(input_t));if(!inputs){ESP_LOGE("phone_remote","Queue allocation failed");return;}
    ESP_ERROR_CHECK(bsp_i2c_init());ESP_ERROR_CHECK(bsp_display_init());if(!bsp_lvgl_init())return;
    bsp_display_backlight(65);(void)bsp_battery_init();
    if(!bsp_lvgl_lock(1000))return;
    ui_create();bsp_lvgl_unlock();
    ESP_ERROR_CHECK(bsp_button_init(on_key,NULL));ble_ok=remote_ble_init()==ESP_OK;
    for(;;){input_t input;if(xQueueReceive(inputs,&input,pdMS_TO_TICKS(80))==pdTRUE)handle_input(&input);
        if(ble_ok)remote_ble_poll();
        render(esp_timer_get_time());}
}
