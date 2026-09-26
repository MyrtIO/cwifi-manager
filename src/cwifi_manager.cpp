#include "cwifi_manager.h"
#include <Arduino.h>
#include <WiFi.h>
#include <string.h>
#include <atomic>

static uint8_t last_reported_status = WL_IDLE_STATUS;
static const cwifi_runtime_config_t *cfg;
static bool connected = false;
static bool provisioning_enabled = false;
static uint32_t last_attempt = 0;
static uint32_t offline_since = 0;
static uint32_t last_recovery = 0;
static bool attempting = false;
static cwifi_diagnostics_t diagnostics = {};
static std::atomic<bool> disconnected(false);
#if defined(ESP32)
static portMUX_TYPE event_mux = portMUX_INITIALIZER_UNLOCKED;

static void on_wifi_event(WiFiEvent_t event, WiFiEventInfo_t info) {
	if (event != ARDUINO_EVENT_WIFI_STA_DISCONNECTED ||
	    info.wifi_sta_disconnected.reason == WIFI_REASON_ASSOC_LEAVE) {
		return;
	}
	portENTER_CRITICAL(&event_mux);
	diagnostics.disconnect_count++;
	diagnostics.last_disconnect_ms = millis();
	diagnostics.last_disconnect_reason = info.wifi_sta_disconnected.reason;
	diagnostics.last_disconnect_rssi = info.wifi_sta_disconnected.rssi;
	portEXIT_CRITICAL(&event_mux);
	disconnected.store(true);
}
#endif

static bool wifi_sta_ready(void) {
#if defined(ESP32)
	// Arduino 2.x can leave WL_CONNECTED stale after AUTH_EXPIRE.
	const int ready = STA_CONNECTED_BIT | STA_HAS_IP_BIT;
	return (WiFi.getStatusBits() & ready) == ready;
#else
	return WiFi.status() == WL_CONNECTED;
#endif
}

static const char *wifi_status_string(uint8_t status) {
	switch (status) {
	case WL_CONNECTED:
		return "connected";
	case WL_NO_SSID_AVAIL:
		return "ssid unavailable";
	case WL_CONNECT_FAILED:
		return "connect failed";
	case WL_CONNECTION_LOST:
		return "connection lost";
	case WL_DISCONNECTED:
		return "disconnected";
	case WL_IDLE_STATUS:
		return "idle";
	case WL_SCAN_COMPLETED:
		return "scan completed";
	default:
		return "unknown";
	}
}

static bool wifi_has_sta_config(void) {
	return cfg != NULL && cfg->ssid != NULL && cfg->ssid[0] != '\0';
}

static bool wifi_ap_should_be_enabled(void) {
	return provisioning_enabled || !wifi_has_sta_config();
}

static size_t copy_ip_string(IPAddress ip, char *buf, size_t buf_size) {
	if (buf == NULL || buf_size == 0) {
		return 0;
	}

	String ip_string = ip.toString();
	size_t len = ip_string.length();
	size_t copy_len = (len < (buf_size - 1)) ? len : (buf_size - 1);
	memcpy(buf, ip_string.c_str(), copy_len);
	buf[copy_len] = '\0';
	return copy_len;
}

static void wifi_begin_sta(void) {
	if (!wifi_has_sta_config()) {
		connected = false;
		return;
	}

	last_attempt = millis();
	diagnostics.reconnect_count++;
	attempting = WiFi.begin(cfg->ssid, cfg->password) != WL_CONNECT_FAILED;
}

static void wifi_apply_mode(void) {
	bool sta_enabled = wifi_has_sta_config();
	bool ap_enabled = wifi_ap_should_be_enabled();

	connected = false;
	attempting = false;
	disconnected.store(false);
	last_recovery = millis();
	WiFi.setAutoReconnect(false);
	WiFi.mode(WIFI_OFF);
	if (cfg != NULL && cfg->hostname != NULL && cfg->hostname[0] != '\0') {
		WiFi.setHostname(cfg->hostname);
	}

	if (sta_enabled && ap_enabled) {
		WiFi.mode(WIFI_AP_STA);
	} else if (sta_enabled) {
		WiFi.mode(WIFI_STA);
	} else {
		WiFi.mode(WIFI_AP);
	}

	if (ap_enabled) {
		WiFi.softAP(cfg->ap_ssid);
	}

	if (sta_enabled) {
		if (cfg->disable_sleep) {
			WiFi.setSleep(false);
		}
		wifi_begin_sta();
	} else {
		WiFi.disconnect();
		connected = false;
	}
}

void cwifi_init(const cwifi_runtime_config_t *c, bool enabled) {
#if defined(ESP32)
	static bool events_registered = false;
	if (!events_registered) {
		WiFi.onEvent(on_wifi_event);
		events_registered = true;
	}
#endif
	cfg = c;
	provisioning_enabled = enabled;
	offline_since = millis();
	wifi_apply_mode();
}

void cwifi_reconfigure(const cwifi_runtime_config_t *c, bool enabled) {
	cfg = c;
	provisioning_enabled = enabled;
	offline_since = millis();
	wifi_apply_mode();
}

void cwifi_set_provisioning(bool enabled) {
	if (provisioning_enabled == enabled) {
		return;
	}

	provisioning_enabled = enabled;
	offline_since = millis();
	wifi_apply_mode();
}

void cwifi_loop(void) {
	if (!wifi_has_sta_config()) {
		connected = false;
		return;
	}

	uint8_t status = WiFi.status();
	if (status != last_reported_status) {
		last_reported_status = status;
		Serial.printf("[WiFi] %s\n", wifi_status_string(status));
	}

	uint32_t now = millis();
	if (disconnected.exchange(false)) {
		attempting = false;
		cwifi_diagnostics_t snapshot;
		cwifi_get_diagnostics(&snapshot);
		Serial.printf("[WiFi] disconnected reason=%u rssi=%ld\n",
		              snapshot.last_disconnect_reason, (long)snapshot.last_disconnect_rssi);
	}

	if (wifi_sta_ready()) {
		if (!connected) {
			diagnostics.last_outage_ms = now - offline_since;
			Serial.printf("[WiFi] connected after %lu ms\n", (unsigned long)(now - offline_since));
		}
		connected = true;
		attempting = false;
		last_recovery = now;
		return;
	}

	if (connected) {
		connected = false;
		offline_since = now;
	}

	unsigned long recovery_timeout = cfg->recovery_timeout_ms ? cfg->recovery_timeout_ms : 90000;
	if (now - last_recovery >= recovery_timeout) {
		diagnostics.restart_count++;
		Serial.println("[WiFi] restarting radio after connection timeout");
		wifi_apply_mode();
		return;
	}

	unsigned long connect_timeout = cfg->connect_timeout_ms ? cfg->connect_timeout_ms : 30000;
	unsigned long interval = cfg->reconnect_interval_ms ? cfg->reconnect_interval_ms : 5000;
	if (now - last_attempt < interval ||
	    (attempting && now - last_attempt < connect_timeout)) {
		return;
	}

	last_attempt = now;
	diagnostics.reconnect_count++;
	// Reuse the station configuration instead of resetting DHCP with begin().
	attempting = WiFi.reconnect();
	Serial.printf("[WiFi] reconnect %s\n", attempting ? "started" : "failed");
}

bool cwifi_sta_is_connected(void) {
	return wifi_has_sta_config() && wifi_sta_ready();
}

void cwifi_get_diagnostics(cwifi_diagnostics_t *result) {
	if (result == NULL) {
		return;
	}
#if defined(ESP32)
	portENTER_CRITICAL(&event_mux);
#endif
	*result = diagnostics;
#if defined(ESP32)
	portEXIT_CRITICAL(&event_mux);
#endif
	bool ready = cwifi_sta_is_connected();
	result->offline_ms = !ready && wifi_has_sta_config() ? millis() - offline_since : 0;
	result->rssi = ready ? WiFi.RSSI() : 0;
}

bool cwifi_ap_is_enabled(void) {
	return wifi_ap_should_be_enabled();
}

cwifi_network_mode_t cwifi_network_mode(void) {
	WiFiMode_t mode = WiFi.getMode();

	if ((mode & WIFI_AP) && (mode & WIFI_STA)) {
		return WIFI_NETWORK_AP_STA;
	}
	if (mode & WIFI_AP) {
		return WIFI_NETWORK_AP;
	}
	if (mode & WIFI_STA) {
		return WIFI_NETWORK_STA;
	}
	return WIFI_NETWORK_OFF;
}

const char *cwifi_network_mode_string(void) {
	switch (cwifi_network_mode()) {
	case WIFI_NETWORK_AP_STA:
		return "AP + STA";
	case WIFI_NETWORK_AP:
		return "AP";
	case WIFI_NETWORK_STA:
		return "STA";
	default:
		return "off";
	}
}

size_t cwifi_sta_ip_string(char *buf, size_t buf_size) {
	if (!cwifi_sta_is_connected()) {
		if (buf != NULL && buf_size > 0) {
			buf[0] = '\0';
		}
		return 0;
	}
	return copy_ip_string(WiFi.localIP(), buf, buf_size);
}

size_t cwifi_ap_ip_string(char *buf, size_t buf_size) {
	if (!wifi_ap_should_be_enabled()) {
		if (buf != NULL && buf_size > 0) {
			buf[0] = '\0';
		}
		return 0;
	}
	return copy_ip_string(WiFi.softAPIP(), buf, buf_size);
}

void cwifi_mac_address(uint8_t mac[6]) {
	WiFi.macAddress(mac);
}
