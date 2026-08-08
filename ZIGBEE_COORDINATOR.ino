static volatile bool zbStarted = false;
static volatile bool zbStartFailed = false;
static bool zbTaskCreated = false;

static void startCommissioning(uint8_t mode) {
  esp_zb_bdb_start_top_level_commissioning(mode);
}

extern "C" void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal) {
  uint32_t *raw = signal->p_app_signal;
  esp_zb_app_signal_type_t type = (esp_zb_app_signal_type_t)*raw;
  switch (type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
      startCommissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
      break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
      if (signal->esp_err_status == ESP_OK) {
        if (esp_zb_bdb_is_factory_new()) startCommissioning(ESP_ZB_BDB_MODE_NETWORK_FORMATION);
        else zbStarted = true;
      } else zbStartFailed = true;
      break;
    case ESP_ZB_BDB_SIGNAL_FORMATION:
      if (signal->esp_err_status == ESP_OK) {
        zbStarted = true;
        // Pairing is APsystems' application broadcast, not Zigbee BDB joining,
        // nevertheless allowing joins matches the permissive legacy coordinator.
        esp_zb_bdb_open_network(0xFF);
      } else {
        esp_zb_scheduler_alarm((esp_zb_callback_t)startCommissioning,
                               ESP_ZB_BDB_MODE_NETWORK_FORMATION, 1000);
      }
      break;
    default:
      break;
  }
}

static void zigbeeTask(void *) {
  esp_zb_platform_config_t platform = {};
  platform.radio_config.radio_mode = ZB_RADIO_MODE_NATIVE;
  platform.host_config.host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE;
  ESP_ERROR_CHECK(esp_zb_platform_config(&platform));

  esp_zb_cfg_t cfg = {};
  cfg.esp_zb_role = ESP_ZB_DEVICE_TYPE_COORDINATOR;
  cfg.install_code_policy = false;
  cfg.nwk_cfg.zczr_cfg.max_children = 10;
  esp_zb_init(&cfg);

  uint16_t pan = hexLe16(ECU_ID); // ECU begins D8A3 -> PAN 0xA3D8
  esp_zb_set_pan_id(pan);
  esp_zb_ieee_addr_t extPan = {0xFF, 0xFF, 0, 0, 0, 0, 0, 0};
  String reversed = ECU_REVERSE();
  for (uint8_t i = 0; i < 6; ++i) extPan[i + 2] = hexByte(reversed.c_str() + i * 2);
  esp_zb_set_extended_pan_id(extPan);
  ESP_ERROR_CHECK(esp_zb_set_primary_network_channel_set(1UL << 16));

  // Packet captures from the original firmware show unsecured APsystems APS
  // traffic; do not add Zigbee NWK or APS encryption to these proprietary frames.
  ESP_ERROR_CHECK(esp_zb_secur_network_security_enable(false));

  esp_zb_cluster_list_t *clusters = esp_zb_zcl_cluster_list_create();
  esp_zb_attribute_list_t *basic = esp_zb_basic_cluster_create(nullptr);
  ESP_ERROR_CHECK(esp_zb_cluster_list_add_basic_cluster(clusters, basic, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
  esp_zb_ep_list_t *endpoints = esp_zb_ep_list_create();
  esp_zb_endpoint_config_t ep = {
    .endpoint = 0x14,
    .app_profile_id = 0x0F05,
    .app_device_id = 0x0100,
    .app_device_version = 0,
  };
  ESP_ERROR_CHECK(esp_zb_ep_list_add_ep(endpoints, clusters, ep));
  ESP_ERROR_CHECK(esp_zb_device_register(endpoints));

  apsRxQueue = xQueueCreate(8, sizeof(ApsRxFrame));
  esp_zb_aps_data_indication_handler_register(apsDataIndication);
  esp_zb_aps_data_confirm_handler_register(apsDataConfirm);
  esp_zb_nvram_erase_at_start(true);
  ESP_ERROR_CHECK(esp_zb_start(false));
  esp_zb_stack_main_loop();
}

bool coordinator(bool normal) {
  (void)normal;
  if (!zbTaskCreated) {
    zbTaskCreated = xTaskCreate(zigbeeTask, "aps_zigbee", 8192, nullptr, 5, nullptr) == pdPASS;
    if (!zbTaskCreated) return false;
  }
  unsigned long began = millis();
  while (!zbStarted && !zbStartFailed && millis() - began < 15000) delay(25);
  zigbeeUp = zbStarted ? 1 : 0;
  return zbStarted;
}

void coordinator_init() { coordinator(false); }
void sendNO() {
  char ecuRev[13], cmd[100];
  ECU_REVERSE().toCharArray(ecuRev, sizeof(ecuRev));
  snprintf(cmd, sizeof(cmd), "2401FFFF1414060001000F1E%sFBFB1100000D6030FBD3000000000000000004010281FEFE", ecuRev);
  sendZB(cmd);
}

void ZBhardReset() {
  // Integrated radio cannot be GPIO-reset separately. A whole-chip restart is
  // deliberately not performed here; coordinator() is idempotent.
  consoleOut("integrated Zigbee radio already managed by ESP32-C6");
}
