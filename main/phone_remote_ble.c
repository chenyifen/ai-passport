#include "phone_remote_ble.h"
#include <stdatomic.h>
#include <string.h>
#include "esp_random.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"

/* ESP-IDF's NimBLE example declares this initializer separately from the header. */
void ble_store_config_init(void);

static const uint8_t report_map[] = {
    /* Report 1: mouse buttons + relative X/Y/wheel. No keyboard/track keys. */
    0x05,0x01,0x09,0x02,0xa1,0x01,0x85,0x01,0x09,0x01,0xa1,0x00,
    0x05,0x09,0x19,0x01,0x29,0x03,0x15,0x00,0x25,0x01,0x95,0x03,
    0x75,0x01,0x81,0x02,0x95,0x01,0x75,0x05,0x81,0x03,
    0x05,0x01,0x09,0x30,0x09,0x31,0x09,0x38,0x15,0x81,0x25,0x7f,
    0x75,0x08,0x95,0x03,0x81,0x06,0xc0,0xc0,
    /* Report 2: two independent Consumer Control volume bits. */
    0x05,0x0c,0x09,0x01,0xa1,0x01,0x85,0x02,0x15,0x00,0x25,0x01,
    0x09,0xe9,0x09,0xea,0x75,0x01,0x95,0x02,0x81,0x02,
    0x75,0x06,0x95,0x01,0x81,0x03,0xc0
};
static uint16_t mouse_handle, volume_handle;
static nvs_handle_t peer_store;
static atomic_uint connection = ATOMIC_VAR_INIT(BLE_HS_CONN_HANDLE_NONE);
static atomic_uint generation;
static atomic_bool synced, secure, mouse_sub, volume_sub, forget_pending;
static atomic_int passkey = ATOMIC_VAR_INIT(-1);
static uint8_t own_addr_type;

static int device_info(uint16_t conn,uint16_t attr,struct ble_gatt_access_ctxt *ctx,void *arg) {
    (void)conn;(void)attr;(void)arg;
    /* USB vendor/product identify the Espressif interface used by this board. */
    const uint8_t pnp[]={2,0x3a,0x30,0x01,0x10,0x00,0x01};
    return os_mbuf_append(ctx->om,pnp,sizeof(pnp)) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
}

static int access_value(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctx, void *arg) {
    (void)conn; (void)attr;
    const uintptr_t kind = (uintptr_t)arg;
    const uint8_t info[] = {0x11,0x01,0x00,0x02};
    const uint8_t mouse_ref[] = {1,1}, volume_ref[] = {2,1};
    const uint8_t zero[4] = {0}, protocol = 1;
    const void *data = zero; size_t len = 1;
    if (ctx->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint8_t value;
        if (OS_MBUF_PKTLEN(ctx->om) != 1 || os_mbuf_copydata(ctx->om,0,1,&value))
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        return value <= 1 ? 0 : BLE_ATT_ERR_UNLIKELY;
    }
    if (kind == 1) { data=info; len=sizeof(info); }
    if (kind == 2) { data=report_map; len=sizeof(report_map); }
    if (kind == 3) data=&protocol;
    if (kind == 4) len=4;
    if (kind == 6) { data=mouse_ref; len=2; }
    if (kind == 7) { data=volume_ref; len=2; }
    return os_mbuf_append(ctx->om,data,len) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}
#define UUID16(n) BLE_UUID16_DECLARE(n)
static struct ble_gatt_dsc_def mouse_desc[] = {
    {.uuid=UUID16(0x2908),.att_flags=BLE_ATT_F_READ|BLE_ATT_F_READ_ENC|BLE_ATT_F_READ_AUTHEN,.access_cb=access_value,.arg=(void*)6}, {0}
};
static struct ble_gatt_dsc_def volume_desc[] = {
    {.uuid=UUID16(0x2908),.att_flags=BLE_ATT_F_READ|BLE_ATT_F_READ_ENC|BLE_ATT_F_READ_AUTHEN,.access_cb=access_value,.arg=(void*)7}, {0}
};
static const struct ble_gatt_chr_def characteristics[] = {
    {.uuid=UUID16(0x2a4a),.access_cb=access_value,.arg=(void*)1,.flags=BLE_GATT_CHR_F_READ},
    {.uuid=UUID16(0x2a4b),.access_cb=access_value,.arg=(void*)2,.flags=BLE_GATT_CHR_F_READ},
    {.uuid=UUID16(0x2a4c),.access_cb=access_value,
     .flags=BLE_GATT_CHR_F_WRITE_NO_RSP|BLE_GATT_CHR_F_WRITE_ENC},
    {.uuid=UUID16(0x2a4d),.access_cb=access_value,.arg=(void*)4,.val_handle=&mouse_handle,
     .flags=BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_READ_ENC|BLE_GATT_CHR_F_READ_AUTHEN|BLE_GATT_CHR_F_NOTIFY,.descriptors=mouse_desc},
    {.uuid=UUID16(0x2a4d),.access_cb=access_value,.arg=(void*)5,.val_handle=&volume_handle,
     .flags=BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_READ_ENC|BLE_GATT_CHR_F_READ_AUTHEN|BLE_GATT_CHR_F_NOTIFY,.descriptors=volume_desc}, {0}
};
static const struct ble_gatt_svc_def services[] = {
    {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=UUID16(0x1812),.characteristics=characteristics},
    {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=UUID16(0x180a),.characteristics=(struct ble_gatt_chr_def[]){
        {.uuid=UUID16(0x2a50),.access_cb=device_info,.flags=BLE_GATT_CHR_F_READ}, {0}
    }},
    {0}
};

static void advertise(void);
static bool allowed_peer(uint16_t handle) {
    ble_addr_t saved;size_t size=sizeof(saved);
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(handle,&desc)) return false;
    esp_err_t err=nvs_get_blob(peer_store,"phone",&saved,&size);
    if (err==ESP_ERR_NVS_NOT_FOUND) return true;
    return err==ESP_OK&&size==sizeof(saved)&&saved.type==desc.peer_id_addr.type
           &&!memcmp(saved.val,desc.peer_id_addr.val,6);
}
static int gap_event(struct ble_gap_event *ev, void *arg) {
    (void)arg;
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status) { advertise(); break; }
        if (!allowed_peer(ev->connect.conn_handle)) {
            ble_gap_terminate(ev->connect.conn_handle,BLE_ERR_REM_USER_CONN_TERM); break;
        }
        atomic_store(&connection,ev->connect.conn_handle);
        atomic_store(&mouse_sub,false);atomic_store(&volume_sub,false);
        atomic_fetch_add(&generation,1);
        atomic_store(&secure,false);
        int security_result=ble_gap_security_initiate(ev->connect.conn_handle);
        if (security_result!=0&&security_result!=BLE_HS_EALREADY)
            ble_gap_terminate(ev->connect.conn_handle,BLE_ERR_REM_USER_CONN_TERM);
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        atomic_store(&connection,BLE_HS_CONN_HANDLE_NONE);
        atomic_fetch_add(&generation,1);
        atomic_store(&secure,false); atomic_store(&mouse_sub,false); atomic_store(&volume_sub,false);
        atomic_store(&passkey,-1); advertise(); break;
    case BLE_GAP_EVENT_ENC_CHANGE: {
        struct ble_gap_conn_desc d;
        bool ok=ev->enc_change.conn_handle==atomic_load(&connection)
                &&ev->enc_change.status==0 && !ble_gap_conn_find(ev->enc_change.conn_handle,&d)
                && d.sec_state.encrypted && d.sec_state.authenticated && d.sec_state.bonded;
        atomic_store(&secure,ok); atomic_store(&passkey,-1);
        if (ok) {
            const char *key="phone";
            if (nvs_set_blob(peer_store,key,&d.peer_id_addr,sizeof(d.peer_id_addr))!=ESP_OK
                ||nvs_commit(peer_store)!=ESP_OK) {ok=false;atomic_store(&secure,false);}
        }
        if (!ok) ble_gap_terminate(ev->enc_change.conn_handle,BLE_ERR_REM_USER_CONN_TERM);
        break;
    }
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        if (ev->passkey.params.action == BLE_SM_IOACT_DISP) {
            struct ble_sm_io io={.action=BLE_SM_IOACT_DISP,.passkey=esp_random()%1000000};
            atomic_store(&passkey,(int)io.passkey);
            ble_sm_inject_io(ev->passkey.conn_handle,&io);
        }
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (ev->subscribe.attr_handle==mouse_handle) atomic_store(&mouse_sub,ev->subscribe.cur_notify);
        if (ev->subscribe.attr_handle==volume_handle) atomic_store(&volume_sub,ev->subscribe.cur_notify);
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE: advertise(); break;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* Never discard an existing bond in response to a remote request. */
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    default: break;
    }
    return 0;
}
static void advertise(void) {
    if (!atomic_load(&synced)||atomic_load(&forget_pending)
        ||atomic_load(&connection)!=BLE_HS_CONN_HANDLE_NONE||ble_gap_adv_active()) return;
    static const ble_uuid16_t hid_uuid=BLE_UUID16_INIT(0x1812);
    struct ble_hs_adv_fields f={0};
    f.flags=BLE_HS_ADV_F_DISC_GEN|BLE_HS_ADV_F_BREDR_UNSUP;
    f.uuids16=&hid_uuid; f.num_uuids16=1; f.uuids16_is_complete=1;
    f.appearance=0x03c2; f.appearance_is_present=1;
    if (ble_gap_adv_set_fields(&f)) return;
    struct ble_hs_adv_fields scan={0};
    const char *name="Passport Remote";
    scan.name=(const uint8_t*)name;scan.name_len=strlen(name);scan.name_is_complete=1;
    if (ble_gap_adv_rsp_set_fields(&scan)) return;
    struct ble_gap_adv_params p={.conn_mode=BLE_GAP_CONN_MODE_UND,.disc_mode=BLE_GAP_DISC_MODE_GEN,
                                 .itvl_min=48,.itvl_max=80};
    (void)ble_gap_adv_start(own_addr_type,NULL,BLE_HS_FOREVER,&p,gap_event,NULL);
}
static void on_sync(void) {
    if (ble_hs_util_ensure_addr(0)||ble_hs_id_infer_auto(0,&own_addr_type)) return;
    atomic_store(&synced,true); advertise();
}
static void on_reset(int reason) {
    (void)reason; atomic_store(&synced,false); atomic_store(&secure,false);
    atomic_store(&connection,BLE_HS_CONN_HANDLE_NONE); atomic_fetch_add(&generation,1);
    atomic_store(&mouse_sub,false); atomic_store(&volume_sub,false); atomic_store(&passkey,-1);
}
static void host_task(void *arg) { (void)arg; nimble_port_run(); vTaskDelete(NULL); }

esp_err_t remote_ble_init(void) {
    if (nvs_open("phone_remote",NVS_READWRITE,&peer_store)!=ESP_OK) return ESP_FAIL;
    if (nimble_port_init()!=ESP_OK) return ESP_FAIL;
    ble_svc_gap_init(); ble_svc_gatt_init();
    if (ble_svc_gap_device_name_set("Passport Remote")) return ESP_FAIL;
    ble_hs_cfg.sync_cb=on_sync;ble_hs_cfg.reset_cb=on_reset;
    ble_hs_cfg.sm_io_cap=BLE_SM_IO_CAP_DISP_ONLY;
    ble_hs_cfg.sm_bonding=1;ble_hs_cfg.sm_mitm=1;ble_hs_cfg.sm_sc=1;
    ble_hs_cfg.sm_our_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
    ble_store_config_init();
    if (ble_gatts_count_cfg(services)||ble_gatts_add_svcs(services)) return ESP_FAIL;
    return xTaskCreate(host_task,"remote_ble",4096,NULL,5,NULL)==pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
void remote_ble_poll(void) {
    if (atomic_load(&forget_pending) && atomic_load(&connection)==BLE_HS_CONN_HANDLE_NONE) {
        const char *key="phone";
        ble_addr_t saved;size_t size=sizeof(saved);
        esp_err_t err=nvs_get_blob(peer_store,key,&saved,&size);
        if (err==ESP_ERR_NVS_NOT_FOUND || (err==ESP_OK&&ble_store_util_delete_peer(&saved)==0)) {
            (void)nvs_erase_key(peer_store,key);(void)nvs_commit(peer_store);
            atomic_store(&forget_pending,false);
        }
    }
    advertise();
}
bool remote_ble_ready(void) { return atomic_load(&secure)&&atomic_load(&mouse_sub)&&atomic_load(&volume_sub); }
int remote_ble_passkey(void) { return atomic_load(&passkey); }
static esp_err_t notify(uint16_t handle,const void *data,size_t len,unsigned epoch) {
    if (!remote_ble_ready()||epoch!=atomic_load(&generation)) return ESP_ERR_INVALID_STATE;
    struct os_mbuf *mb=ble_hs_mbuf_from_flat(data,len);
    if (!mb) return ESP_ERR_NO_MEM;
    return ble_gatts_notify_custom(atomic_load(&connection),handle,mb)==0 ? ESP_OK : ESP_FAIL;
}
static esp_err_t release_input(uint16_t handle,const void *data,size_t size,unsigned epoch) {
    esp_err_t err=ESP_FAIL;
    for(int attempt=0;attempt<3;attempt++) {
        err=notify(handle,data,size,epoch);
        if(err==ESP_OK||epoch!=atomic_load(&generation))return err;
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    /* A dropped key-up must never leave the central with a held button. */
    unsigned c=atomic_load(&connection);
    if(epoch==atomic_load(&generation)&&c!=BLE_HS_CONN_HANDLE_NONE)
        (void)ble_gap_terminate(c,BLE_ERR_REM_USER_CONN_TERM);
    return err;
}
esp_err_t remote_ble_action(remote_action_t action) {
    if (!remote_ble_ready()) return ESP_ERR_INVALID_STATE;
    unsigned epoch=atomic_load(&generation); esp_err_t err=ESP_OK;
    if (action==REMOTE_VOLUME_UP||action==REMOTE_VOLUME_DOWN) {
        uint8_t value=action==REMOTE_VOLUME_UP?1:2;
        err=notify(volume_handle,&value,1,epoch);
        vTaskDelay(pdMS_TO_TICKS(45));value=0;
        esp_err_t release=release_input(volume_handle,&value,1,epoch);
        return err!=ESP_OK?err:release;
    }
    remote_mouse_t reports[67];size_t n=remote_gesture(action,reports,67);
    if (!n) return ESP_ERR_INVALID_ARG;
    for (size_t i=0;i<n;i++) {
        err=notify(mouse_handle,&reports[i],sizeof(reports[i]),epoch);
        if (err!=ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(12));
    }
    /* Release even on send failure, but never deliver stale reports to a new link. */
    remote_mouse_t release={0};
    esp_err_t release_err=release_input(mouse_handle,&release,sizeof(release),epoch);
    return err!=ESP_OK?err:release_err;
}
esp_err_t remote_ble_forget(void) {
    atomic_store(&forget_pending,true);atomic_store(&secure,false);
    (void)ble_gap_adv_stop();
    unsigned c=atomic_load(&connection);
    if (c!=BLE_HS_CONN_HANDLE_NONE) (void)ble_gap_terminate(c,BLE_ERR_REM_USER_CONN_TERM);
    remote_ble_poll(); return ESP_OK;
}
