#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum cwifi_network_mode_t {
    WIFI_NETWORK_OFF,
    WIFI_NETWORK_AP,
    WIFI_NETWORK_STA,
    WIFI_NETWORK_AP_STA,
} cwifi_network_mode_t;

typedef struct cwifi_runtime_config_t {
    const char *ssid;
    const char *password;
    const char *hostname;
    const char *ap_ssid;
    unsigned long reconnect_interval_ms;
} cwifi_runtime_config_t;

void cwifi_init(const cwifi_runtime_config_t *cfg, bool provisioning_enabled);
void cwifi_reconfigure(const cwifi_runtime_config_t *cfg, bool provisioning_enabled);
void cwifi_set_provisioning(bool enabled);
void cwifi_loop(void);
bool cwifi_sta_is_connected(void);
bool cwifi_ap_is_enabled(void);
cwifi_network_mode_t cwifi_network_mode(void);
const char *cwifi_network_mode_string(void);
size_t cwifi_sta_ip_string(char *buf, size_t buf_size);
size_t cwifi_ap_ip_string(char *buf, size_t buf_size);
void cwifi_mac_address(uint8_t mac[6]);

#ifdef __cplusplus
}
#endif
