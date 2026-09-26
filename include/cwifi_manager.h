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
    /* Zero selects 30 s per attempt and 90 s before restarting the radio. */
    unsigned long connect_timeout_ms;
    unsigned long recovery_timeout_ms;
    bool disable_sleep;
} cwifi_runtime_config_t;

typedef struct cwifi_diagnostics_t {
    uint32_t disconnect_count;
    uint32_t reconnect_count;
    uint32_t restart_count;
    uint32_t last_disconnect_ms;
    uint32_t offline_ms;
    uint32_t last_outage_ms;
    int32_t rssi;
    int32_t last_disconnect_rssi;
    uint8_t last_disconnect_reason;
} cwifi_diagnostics_t;

void cwifi_init(const cwifi_runtime_config_t *cfg, bool provisioning_enabled);
void cwifi_reconfigure(const cwifi_runtime_config_t *cfg, bool provisioning_enabled);
void cwifi_set_provisioning(bool enabled);
void cwifi_loop(void);
bool cwifi_sta_is_connected(void);
void cwifi_get_diagnostics(cwifi_diagnostics_t *diagnostics);
bool cwifi_ap_is_enabled(void);
cwifi_network_mode_t cwifi_network_mode(void);
const char *cwifi_network_mode_string(void);
size_t cwifi_sta_ip_string(char *buf, size_t buf_size);
size_t cwifi_ap_ip_string(char *buf, size_t buf_size);
void cwifi_mac_address(uint8_t mac[6]);

#ifdef __cplusplus
}
#endif
