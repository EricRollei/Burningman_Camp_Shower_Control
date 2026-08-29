#pragma once

// ESP-NOW callback-signature shims. The Tough builds on Arduino core 2.0.x
// (IDF 4.4) and the NanoC6 on core 3.x (IDF 5.5); their esp_now callback
// prototypes differ. Both firmwares use the raw esp_now C API through these
// macros so the transport code reads the same on either side.

#include <esp_arduino_version.h>
#include <esp_idf_version.h>
#include <esp_now.h>
#include <esp_wifi.h>

#if ESP_ARDUINO_VERSION_MAJOR >= 3
#define CAMPNET_RECV_CB_PARAMS const esp_now_recv_info_t* info, const uint8_t* data, int length
#else
#define CAMPNET_RECV_CB_PARAMS const uint8_t* mac, const uint8_t* data, int length
#endif

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
#define CAMPNET_SEND_CB_PARAMS const esp_now_send_info_t* txInfo, esp_now_send_status_t status
#else
#define CAMPNET_SEND_CB_PARAMS const uint8_t* mac, esp_now_send_status_t status
#endif

namespace CampNet {
constexpr uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
}
