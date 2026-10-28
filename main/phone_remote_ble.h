#pragma once
#include "esp_err.h"
#include "phone_remote_model.h"
esp_err_t remote_ble_init(void);
void remote_ble_poll(void);
bool remote_ble_ready(void);
int remote_ble_passkey(void);
esp_err_t remote_ble_action(remote_action_t action);
esp_err_t remote_ble_forget(void);
