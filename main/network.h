#ifndef NETWORK_H_
#define NETWORK_H_

#include "esp_err.h"
#include "esp_netif_types.h"	// For esp_netif_ip_info_t and esp_netif_dns_info_t

// Constants for GPIO pin assignments, etc.
#define NETWORK_ETH_MDC_GPIO 31
#define NETWORK_ETH_MDIO_GPIO 52
#define NETWORK_ETH_PHY_RST_GPIO 51
#define NETWORK_ETH_PHY_ADDR 1

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	bool dhcp_enabled;
	esp_netif_ip_info_t static_ip_info;
	esp_netif_dns_info_t dns_main;
	esp_netif_dns_info_t dns_backup;
	bool mdns_enabled;
	const char *mdns_hostname;
	const char *mdns_instance_name;
} network_config_t;

/**
 * @brief Initialize Ethernet and TCP/IP stack.
 *
 * Initializes only the internal Ethernet interface,
 * registers event handlers, and attaches the Ethernet driver to the TCP/IP stack.
 *
 * @param config Pointer to the network configuration structure.
 * @return
 * - ESP_OK: Success
 * - ESP_ERR_INVALID_ARG: If config is NULL
 * - Others: Error
 */
esp_err_t network_init(network_config_t *config);

/**
 * @brief Deinitialize Ethernet and TCP/IP stack.
 *
 * Releases initialized resources.
 *
 * @return
 * - ESP_OK: Success
 * - Others: Error
 */
esp_err_t network_deinit(void);

/**
 * @brief Creates a network configuration for DHCP.
 *
 * @param config Pointer to the network_config_t structure to be populated.
 * @return
 * - ESP_OK: Success
 * - ESP_ERR_INVALID_ARG: If config is NULL
 */
esp_err_t network_create_dhcp_config(network_config_t *config);

/**
 * @brief Creates a network configuration for static IP.
 *
 * @param config Pointer to the network_config_t structure to be populated.
 * @param ip_addr Static IP address string (e.g., "192.168.1.10").
 * @param netmask Netmask string (e.g., "255.255.255.0").
 * @param gateway Gateway address string (e.g., "192.168.1.1").
 * @param dns_main_server Main DNS server address string (e.g., "8.8.8.8").
 * @param dns_backup_server Backup DNS server address string (e.g., "8.8.4.4"). Can be NULL if not used.
 * @return
 * - ESP_OK: Success
 * - ESP_ERR_INVALID_ARG: If config, ip_addr, netmask, or gateway is NULL.
 */
esp_err_t network_create_static_ip_config(network_config_t *config, const char *ip_addr, const char *netmask, const char *gateway,
																					const char *dns_main_server, const char *dns_backup_server);

/**
 * @brief Configure mDNS for the network.
 *
 * @param config Pointer to the network_config_t structure.
 * @param hostname Hostname for mDNS (e.g., "esp32").
 * @param instance_name Instance name for mDNS (e.g., "ESP32 Device"). Can be NULL.
 * @return
 * - ESP_OK: Success
 * - ESP_ERR_INVALID_ARG: If config is NULL or hostname is NULL.
 */
esp_err_t network_config_mdns(network_config_t *config, const char *hostname, const char *instance_name);

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_H_ */
