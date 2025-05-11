#include "network.h"

#include <string.h>

#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/inet.h"
#include "mdns.h"

static const char *TAG = "network";

static esp_eth_mac_t *s_mac = NULL;
static esp_eth_phy_t *s_phy = NULL;
static esp_eth_handle_t s_eth_handle = NULL;
static esp_netif_t *s_eth_netif = NULL;
static esp_eth_netif_glue_handle_t s_eth_netif_glue = NULL;

static esp_err_t set_dns_server(esp_netif_t *netif, esp_netif_dns_info_t *dns_info, esp_netif_dns_type_t type) {
	if (netif && dns_info && dns_info->ip.u_addr.ip4.addr != 0) {
		return esp_netif_set_dns_info(netif, type, dns_info);
	}
	return ESP_OK;
}

/**
 * @brief Ethernet event handler
 */
static void eth_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
	uint8_t mac_addr[6] = {0};
	esp_eth_handle_t eth_handle_event = *(esp_eth_handle_t *)event_data;

	switch (event_id) {
		case ETHERNET_EVENT_CONNECTED:
			esp_eth_ioctl(eth_handle_event, ETH_CMD_G_MAC_ADDR, mac_addr);
			ESP_LOGI(TAG, "Ethernet Link Up");
			ESP_LOGI(TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x", mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4],
							 mac_addr[5]);
			break;
		case ETHERNET_EVENT_DISCONNECTED:
			ESP_LOGI(TAG, "Ethernet Link Down");
			break;
		case ETHERNET_EVENT_START:
			ESP_LOGI(TAG, "Ethernet Started");
			break;
		case ETHERNET_EVENT_STOP:
			ESP_LOGI(TAG, "Ethernet Stopped");
			break;
		default:
			break;
	}
}

/**
 * @brief IP_EVENT_ETH_GOT_IP event handler
 */
static void got_ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
	ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
	const esp_netif_ip_info_t *ip_info = &event->ip_info;

	// Get DNS server information
	esp_netif_dns_info_t dns_main, dns_backup;
	esp_err_t ret = esp_netif_get_dns_info(s_eth_netif, ESP_NETIF_DNS_MAIN, &dns_main);
	if (ret != ESP_OK) {
		ESP_LOGW(TAG, "Failed to get main DNS server: %s", esp_err_to_name(ret));
		dns_main.ip.u_addr.ip4.addr = IPADDR_ANY;
	}
	ret = esp_netif_get_dns_info(s_eth_netif, ESP_NETIF_DNS_BACKUP, &dns_backup);
	if (ret != ESP_OK) {
		ESP_LOGW(TAG, "Failed to get backup DNS server: %s", esp_err_to_name(ret));
		dns_backup.ip.u_addr.ip4.addr = IPADDR_ANY;
	}

	ESP_LOGI(TAG, "Ethernet Got IP Address");
	ESP_LOGI(TAG, "~~~~~~~~~~~");
	ESP_LOGI(TAG, "ETHIP: %s", ip4addr_ntoa((ip4_addr_t *)&ip_info->ip));
	ESP_LOGI(TAG, "ETHMASK: %s", ip4addr_ntoa((ip4_addr_t *)&ip_info->netmask));
	ESP_LOGI(TAG, "ETHGW: %s", ip4addr_ntoa((ip4_addr_t *)&ip_info->gw));
	ESP_LOGI(TAG, "DNS(MAIN): %s", ip4addr_ntoa((ip4_addr_t *)&dns_main.ip.u_addr.ip4));
	ESP_LOGI(TAG, "DNS(BACKUP): %s", ip4addr_ntoa((ip4_addr_t *)&dns_backup.ip.u_addr.ip4));
	ESP_LOGI(TAG, "~~~~~~~~~~~");
}

esp_err_t network_init(network_config_t *config) {
	esp_err_t ret = ESP_OK;

	if (config == NULL) {
		ESP_LOGE(TAG, "Network configuration cannot be NULL");
		return ESP_ERR_INVALID_ARG;
	}

	ESP_LOGI(TAG, "Initializing TCP/IP adapter...");
	ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_init());

	// 2. Create default event loop
	ESP_LOGI(TAG, "Creating default event loop...");
	ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_loop_create_default());

	// 3. Configure Ethernet MAC and PHY
	ESP_LOGI(TAG, "Initializing Ethernet MAC and PHY...");
	eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
	eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();

	// Update PHY configuration
	phy_config.phy_addr = NETWORK_ETH_PHY_ADDR;
	phy_config.reset_gpio_num = NETWORK_ETH_PHY_RST_GPIO;

	// Update MAC configuration
	eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
	esp32_emac_config.smi_gpio.mdc_num = NETWORK_ETH_MDC_GPIO;
	esp32_emac_config.smi_gpio.mdio_num = NETWORK_ETH_MDIO_GPIO;

	// Create MAC instance
	s_mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config);
	if (!s_mac) {
		ESP_LOGE(TAG, "Failed to create ESP32 Ethernet MAC instance");
		ret = ESP_FAIL;
		goto err_mac_phy;
	}

	// Create PHY instance
	s_phy = esp_eth_phy_new_ip101(&phy_config);
	if (!s_phy) {
		ESP_LOGE(TAG, "Failed to create IP101 PHY instance");
		ret = ESP_FAIL;
		goto err_mac_phy;
	}

	// 4. Install Ethernet driver
	ESP_LOGI(TAG, "Installing Ethernet driver...");
	esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(s_mac, s_phy);
	ret = esp_eth_driver_install(&eth_config, &s_eth_handle);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Ethernet driver install failed: %s", esp_err_to_name(ret));
		goto err_driver_install;
	}

	// 5. Create Ethernet network interface
	ESP_LOGI(TAG, "Creating Ethernet network interface...");
	esp_netif_config_t cfg_netif = ESP_NETIF_DEFAULT_ETH();
	s_eth_netif = esp_netif_new(&cfg_netif);
	if (!s_eth_netif) {
		ESP_LOGE(TAG, "Failed to create Ethernet network interface");
		ret = ESP_FAIL;
		goto err_netif_new;
	}

	if (!config->dhcp_enabled) {
		ESP_LOGI(TAG, "Setting static IP configuration");
		ret = esp_netif_dhcpc_stop(s_eth_netif);
		if (ret != ESP_OK && ret != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
			ESP_LOGE(TAG, "Failed to stop DHCP client: %s", esp_err_to_name(ret));
			goto err_static_ip;
		}
		ret = esp_netif_set_ip_info(s_eth_netif, &config->static_ip_info);
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "Failed to set static IP info: %s", esp_err_to_name(ret));
			goto err_static_ip;
		}
		ret = set_dns_server(s_eth_netif, &config->dns_main, ESP_NETIF_DNS_MAIN);
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "Failed to set main DNS server: %s", esp_err_to_name(ret));
			goto err_static_ip;
		}
		ret = set_dns_server(s_eth_netif, &config->dns_backup, ESP_NETIF_DNS_BACKUP);
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "Failed to set backup DNS server: %s", esp_err_to_name(ret));
			goto err_static_ip;
		}
	} else {
		ESP_LOGI(TAG, "Using DHCP");
	}

	ESP_LOGI(TAG, "Attaching Ethernet driver to TCP/IP stack...");
	s_eth_netif_glue = esp_eth_new_netif_glue(s_eth_handle);
	if (!s_eth_netif_glue) {
		ESP_LOGE(TAG, "Failed to create Ethernet netif glue");
		ret = ESP_FAIL;
		goto err_netif_glue;
	}
	ret = esp_netif_attach(s_eth_netif, s_eth_netif_glue);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to attach Ethernet to netif: %s", esp_err_to_name(ret));
		goto err_netif_attach;
	}

	// 7. Register event handlers
	ESP_LOGI(TAG, "Registering event handlers...");
	ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
	ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));

	// 8. Start Ethernet driver
	ESP_LOGI(TAG, "Starting Ethernet driver...");
	ret = esp_eth_start(s_eth_handle);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Ethernet driver start failed: %s", esp_err_to_name(ret));
		goto err_eth_start;
	}

	ESP_LOGI(TAG, "Network initialization successful.");

	// If mDNS is enabled, initialize mDNS
	if (config->mdns_enabled) {
		ESP_LOGI(TAG, "Initializing mDNS...");
		esp_err_t mdns_ret = mdns_init();
		if (mdns_ret != ESP_OK) {
			ESP_LOGW(TAG, "mDNS Init failed: %s", esp_err_to_name(mdns_ret));
		} else {
			mdns_ret = mdns_hostname_set(config->mdns_hostname);
			if (mdns_ret != ESP_OK) {
				ESP_LOGW(TAG, "mDNS set hostname failed: %s", esp_err_to_name(mdns_ret));
			}
			if (config->mdns_instance_name) {
				mdns_ret = mdns_instance_name_set(config->mdns_instance_name);
				if (mdns_ret != ESP_OK) {
					ESP_LOGW(TAG, "mDNS set instance name failed: %s", esp_err_to_name(mdns_ret));
				}
			}
		}
	}

	return ESP_OK;

err_eth_start:
	esp_event_handler_unregister(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler);
	esp_event_handler_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler);
err_netif_attach:
	if (s_eth_netif_glue) esp_eth_del_netif_glue(s_eth_netif_glue);
err_netif_glue:
err_static_ip:
	if (s_eth_netif) esp_netif_destroy(s_eth_netif);
err_netif_new:
	if (s_eth_handle) esp_eth_driver_uninstall(s_eth_handle);
err_driver_install:
err_mac_phy:
	if (s_phy) {
		s_phy->del(s_phy);
		s_phy = NULL;
	}
	if (s_mac) {
		s_mac->del(s_mac);
		s_mac = NULL;
	}
	esp_event_loop_delete_default();
	esp_netif_deinit();
	ESP_LOGE(TAG, "Network initialization failed.");
	return ret;
}

esp_err_t network_deinit(void) {
	esp_err_t ret = ESP_OK;
	ESP_LOGI(TAG, "Deinitializing network...");

	if (s_eth_handle) {
		ESP_LOGI(TAG, "Stopping Ethernet driver...");
		ret = esp_eth_stop(s_eth_handle);
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "Failed to stop Ethernet: %s", esp_err_to_name(ret));
		}
	}

	if (s_eth_netif_glue) {
		ESP_LOGI(TAG, "Deleting netif glue...");
		ret = esp_eth_del_netif_glue(s_eth_netif_glue);
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "Failed to delete netif glue: %s", esp_err_to_name(ret));
		}
		s_eth_netif_glue = NULL;
	}

	if (s_eth_netif) {
		ESP_LOGI(TAG, "Destroying Ethernet network interface...");
		esp_netif_destroy(s_eth_netif);
		s_eth_netif = NULL;
	}

	if (s_eth_handle) {
		ESP_LOGI(TAG, "Uninstalling Ethernet driver...");
		ret = esp_eth_driver_uninstall(s_eth_handle);
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "Failed to uninstall Ethernet driver: %s", esp_err_to_name(ret));
		}
		s_eth_handle = NULL;
	}

	if (s_phy) {
		ESP_LOGI(TAG, "Deleting PHY instance...");
		ret = s_phy->del(s_phy);
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "Failed to delete PHY: %s", esp_err_to_name(ret));
		}
		s_phy = NULL;
	}

	if (s_mac) {
		ESP_LOGI(TAG, "Deleting MAC instance...");
		ret = s_mac->del(s_mac);
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "Failed to delete MAC: %s", esp_err_to_name(ret));
		}
		s_mac = NULL;
	}

	ESP_LOGI(TAG, "Unregistering event handlers...");
	esp_event_handler_unregister(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler);
	esp_event_handler_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler);

	ESP_LOGI(TAG, "Deleting default event loop...");
	ret = esp_event_loop_delete_default();
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to delete default event loop: %s", esp_err_to_name(ret));
	}

	ESP_LOGI(TAG, "Deinitializing TCP/IP adapter...");
	ret = esp_netif_deinit();
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to deinitialize TCP/IP adapter: %s", esp_err_to_name(ret));
	}

	ESP_LOGI(TAG, "Network deinitialization finished.");
	return ret;
}

esp_err_t network_create_dhcp_config(network_config_t *config) {
	if (config == NULL) {
		return ESP_ERR_INVALID_ARG;
	}
	memset(config, 0, sizeof(network_config_t));
	config->dhcp_enabled = true;
	return ESP_OK;
}

esp_err_t network_create_static_ip_config(network_config_t *config, const char *ip_addr, const char *netmask, const char *gateway,
																					const char *dns_main_server, const char *dns_backup_server) {
	if (config == NULL || ip_addr == NULL || netmask == NULL || gateway == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	memset(config, 0, sizeof(network_config_t));
	config->dhcp_enabled = false;

	config->static_ip_info.ip.addr = ipaddr_addr(ip_addr);
	if (config->static_ip_info.ip.addr == IPADDR_NONE) {
		ESP_LOGE(TAG, "Invalid IP address: %s", ip_addr);
		return ESP_ERR_INVALID_ARG;
	}
	config->static_ip_info.netmask.addr = ipaddr_addr(netmask);
	if (config->static_ip_info.netmask.addr == IPADDR_NONE) {
		ESP_LOGE(TAG, "Invalid netmask: %s", netmask);
		return ESP_ERR_INVALID_ARG;
	}
	if (ip4_addr_netmask_valid(config->static_ip_info.netmask.addr) == 0) {
		ESP_LOGE(TAG, "Invalid netmask: %s", netmask);
		return ESP_ERR_INVALID_ARG;
	}
	config->static_ip_info.gw.addr = ipaddr_addr(gateway);
	if (config->static_ip_info.gw.addr == IPADDR_NONE) {
		ESP_LOGE(TAG, "Invalid gateway: %s", gateway);
		return ESP_ERR_INVALID_ARG;
	}

	if (dns_main_server != NULL) {
		config->dns_main.ip.type = IPADDR_TYPE_V4;
		config->dns_main.ip.u_addr.ip4.addr = ipaddr_addr(dns_main_server);
		if (config->dns_main.ip.u_addr.ip4.addr == IPADDR_NONE) {
			ESP_LOGE(TAG, "Invalid DNS server: %s", dns_main_server);
			return ESP_ERR_INVALID_ARG;
		}
	} else {
		// Use gateway address as DNS server if not provided
		ESP_LOGW(TAG, "No DNS server provided, using gateway address as DNS server");
		config->dns_main.ip.u_addr.ip4.addr = config->static_ip_info.gw.addr;
		config->dns_main.ip.type = IPADDR_TYPE_V4;
	}

	if (dns_backup_server != NULL) {
		config->dns_backup.ip.type = IPADDR_TYPE_V4;
		config->dns_backup.ip.u_addr.ip4.addr = ipaddr_addr(dns_backup_server);
		if (config->dns_backup.ip.u_addr.ip4.addr == IPADDR_NONE) {
			ESP_LOGE(TAG, "Invalid backup DNS server: %s", dns_backup_server);
			return ESP_ERR_INVALID_ARG;
		}
	} else {
		config->dns_backup.ip.u_addr.ip4.addr = IPADDR_ANY;
		config->dns_backup.ip.type = IPADDR_TYPE_V4;
	}

	return ESP_OK;
}

esp_err_t network_config_mdns(network_config_t *config, const char *hostname, const char *instance_name) {
	if (config == NULL || hostname == NULL) {
		return ESP_ERR_INVALID_ARG;
	}
	config->mdns_enabled = true;
	config->mdns_hostname = hostname;
	config->mdns_instance_name = instance_name;
	return ESP_OK;
}
