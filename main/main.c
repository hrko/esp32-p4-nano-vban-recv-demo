#include <stdio.h>

#include "esp_err.h"
#include "esp_log.h"
#include "network.h"
#include "nvs_flash.h"

static const char *TAG = "vban_demo";

void app_main(void) {
	ESP_ERROR_CHECK(nvs_flash_init());

	esp_err_t ret = ESP_OK;

	network_config_t net_config;
	ret = network_create_dhcp_config(&net_config);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to create DHCP config: %s", esp_err_to_name(ret));
		return;
	}

	ret = network_config_mdns(&net_config, "esp32-p4-nano", NULL);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to configure mDNS: %s", esp_err_to_name(ret));
		return;
	}

	ret = network_init(&net_config);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Network initialization failed: %s", esp_err_to_name(ret));
		return;
	}
}