/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 Scott Shawcroft for Adafruit Industries
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "shared-bindings/wifi/Radio.h"

#include "py/runtime.h"

#include "esp_log.h"
static const char *TAG = "cp wifi";

#include "esp-idf/components/esp_wifi/include/esp_wifi.h"

#include "esp-idf/components/heap/include/esp_heap_caps.h"

wifi_radio_obj_t common_hal_wifi_radio_obj;

static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data) {
    ESP_LOGI(TAG, "event %x", event_id);
    wifi_radio_obj_t* radio = arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        xEventGroupSetBits(radio->event_group_handle, WIFI_SCAN_DONE_BIT);
    }
    //     esp_wifi_connect();
    // if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    //     esp_wifi_connect();
    // } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    //     if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
    //         esp_wifi_connect();
    //         s_retry_num++;
    //         ESP_LOGI(TAG, "retry to connect to the AP");
    //     } else {
    //         xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    //     }
    //     ESP_LOGI(TAG,"connect to the AP fail");
    // } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    //     ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
    //     ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
    //     s_retry_num = 0;
    //     xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    // }
}

static bool wifi_inited;

void common_hal_wifi_init(void) {
    ESP_EARLY_LOGI(TAG, "init");
    heap_caps_print_heap_info(MALLOC_CAP_8BIT);
    wifi_inited = true;
	common_hal_wifi_radio_obj.base.type = &wifi_radio_type;

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_EARLY_LOGI(TAG, "create event loop");
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_EARLY_LOGI(TAG, "create wifi sta");
    wifi_radio_obj_t* self = &common_hal_wifi_radio_obj;
    self->netif = esp_netif_create_default_wifi_sta();

    self->event_group_handle = xEventGroupCreateStatic(&self->event_group);
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        self,
                                                        self->handler_instance_all_wifi));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        self,
                                                        self->handler_instance_got_ip));


    ESP_EARLY_LOGI(TAG, "wifi init");
	wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
	esp_err_t result = esp_wifi_init(&config);
	if (result == ESP_ERR_NO_MEM) {
		mp_raise_msg(&mp_type_MemoryError, translate("Failed to allocate Wifi memory"));
	} else if (result != ESP_OK) {
		// handle this
	}
    ESP_EARLY_LOGI(TAG, "enable radio");
    common_hal_wifi_radio_set_enabled(self, true);
}

void wifi_reset(void) {
    ESP_LOGI(TAG, "reset");
    if (!wifi_inited) {
        return;
    }
    wifi_radio_obj_t* radio = &common_hal_wifi_radio_obj;
	common_hal_wifi_radio_set_enabled(radio, false);
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT,
                                                          ESP_EVENT_ANY_ID,
                                                          radio->handler_instance_all_wifi));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT,
                                                          IP_EVENT_STA_GOT_IP,
                                                          radio->handler_instance_got_ip));
	ESP_ERROR_CHECK(esp_wifi_deinit());
    esp_netif_destroy(radio->netif);
    radio->netif = NULL;
    ESP_ERROR_CHECK(esp_netif_deinit());
}
