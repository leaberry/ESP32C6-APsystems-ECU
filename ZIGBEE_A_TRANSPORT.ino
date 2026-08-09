/*
 * ESP32-C6 APS transport compatibility layer.
 *
 * The application still constructs the exact ZNP AF_DATA_REQUEST(_EXT) byte
 * strings from the original project. sendZB() decodes only that envelope and
 * submits the unchanged APsystems ASDU to Espressif's integrated Zigbee stack.
 * Incoming ASDUs are rendered in the old AF_INCOMING_MSG hex layout so all
 * existing pairing, YC600/QS1/DS3 decoding, MQTT and UI code remains unchanged.
 */

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

static QueueHandle_t apsRxQueue = nullptr;
static volatile bool apsTxConfirmed = false;
static volatile uint8_t apsTxStatus = 0xff;

struct ApsRxFrame {
  uint16_t cluster;
  uint16_t source;
  uint8_t sourceEp;
  uint8_t destEp;
  uint8_t lqi;
  uint16_t len;
  uint8_t data[300];
};

static uint8_t hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return 0;
}

static uint8_t hexByte(const char *p) { return (hexNibble(p[0]) << 4) | hexNibble(p[1]); }
static uint16_t hexLe16(const char *p) { return (uint16_t)hexByte(p) | ((uint16_t)hexByte(p + 2) << 8); }

static void appendHex(char *out, size_t cap, const uint8_t *data, size_t len) {
  static const char digits[] = "0123456789ABCDEF";
  size_t used = strlen(out);
  for (size_t i = 0; i < len && used + 2 < cap; ++i) {
    out[used++] = digits[data[i] >> 4];
    out[used++] = digits[data[i] & 15];
  }
  out[used] = 0;
}

bool apsDataIndication(esp_zb_apsde_data_ind_t ind) {
  char trace[144];
  snprintf(trace, sizeof(trace),
           "APS RX status=0x%02X src=0x%04X ep=%u->%u profile=0x%04X cluster=0x%04X len=%u lqi=%d",
           ind.status, ind.src_short_addr, ind.src_endpoint, ind.dst_endpoint,
           ind.profile_id, ind.cluster_id, (unsigned)ind.asdu_length,
           ind.lqi);
  diagnosticsAppend(String(trace));
  if (ind.status != 0 || ind.profile_id != 0x0F05 ||
      ind.dst_endpoint != 0x14 || !apsRxQueue) return false;
  ApsRxFrame f = {};
  f.cluster = ind.cluster_id;
  f.source = ind.src_short_addr;
  f.sourceEp = ind.src_endpoint;
  f.destEp = ind.dst_endpoint;
  f.lqi = ind.lqi;
  f.len = (uint16_t)min((uint32_t)sizeof(f.data), ind.asdu_length);
  memcpy(f.data, ind.asdu, f.len);
  xQueueSend(apsRxQueue, &f, 0);
  return true;
}

void apsDataConfirm(esp_zb_apsde_data_confirm_t confirm) {
  apsTxStatus = confirm.status;
  apsTxConfirmed = true;
  char trace[128];
  snprintf(trace, sizeof(trace),
           "APS TX confirm status=0x%02X dst=0x%04X ep=%u->%u len=%u tx_time=%d",
           confirm.status, confirm.dst_addr.addr_short, confirm.src_endpoint,
           confirm.dst_endpoint, (unsigned)confirm.asdu_length, confirm.tx_time);
  diagnosticsAppend(String(trace));
}

static bool submitAps(uint8_t mode, uint16_t destination, uint8_t dstEp,
                      uint8_t srcEp, uint16_t cluster, const uint8_t *asdu,
                      uint16_t asduLen, uint8_t radius, uint8_t options) {
  esp_zb_apsde_data_req_t req = {};
  req.dst_addr_mode = mode;
  req.dst_addr.addr_short = destination;
  req.dst_endpoint = dstEp;
  req.src_endpoint = srcEp;
  req.profile_id = 0x0F05;
  req.cluster_id = cluster;
  req.radius = radius;
  req.tx_options = options;
  req.asdu_length = asduLen;
  req.asdu = const_cast<uint8_t *>(asdu);
  apsTxConfirmed = false;
  esp_zb_lock_acquire(portMAX_DELAY);
  esp_err_t err = esp_zb_aps_data_request(&req);
  esp_zb_lock_release();
  char trace[128];
  snprintf(trace, sizeof(trace),
           "APS TX request %s mode=0x%02X dst=0x%04X ep=%u->%u cluster=0x%04X len=%u opts=0x%02X",
           esp_err_to_name(err), mode, destination, srcEp, dstEp, cluster,
           (unsigned)asduLen, options);
  diagnosticsAppend(String(trace));
  return err == ESP_OK;
}

static void submitApplicationAps(uint16_t dst, uint8_t dstEp, uint8_t srcEp,
                                 uint16_t cluster, const uint8_t *payload,
                                 uint16_t len, uint8_t radius, uint8_t opts) {
  uint8_t encrypted[300];
  size_t encryptedLen = 0;
  int which = apsFindInverter(dst, nullptr);
  bool broadcast = dst == 0xFFFF;
  bool useEncryption = broadcast ? apsAllInvertersEncrypted() : apsInverterUsesEncryption(which);
  if (useEncryption && apsEncryptOutgoing(which, payload, len, encrypted,
                                           sizeof(encrypted), &encryptedLen, broadcast)) {
    submitAps(ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT, dst, dstEp, srcEp, cluster,
              encrypted, encryptedLen, radius, opts);
  } else {
    submitAps(ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT, dst, dstEp, srcEp, cluster,
              payload, len, radius, opts);
  }
}

void sendZB(char command[]) {
  size_t chars = strlen(command);
  if (chars < 4 || (chars & 1)) return;

  // ZNP AF_DATA_REQUEST: 24 01 + short, endpoints, cluster, transaction,
  // options, radius, length and unchanged APsystems ASDU.
  if (!strncmp(command, "2401", 4) && chars >= 24) {
    const char *p = command + 4;
    uint16_t dst = hexLe16(p); p += 4;
    uint8_t dstEp = hexByte(p); p += 2;
    uint8_t srcEp = hexByte(p); p += 2;
    uint16_t cluster = hexLe16(p); p += 4;
    p += 2; // transaction id is allocated by the Espressif stack
    uint8_t znpOptions = hexByte(p); p += 2;
    uint8_t radius = hexByte(p); p += 2;
    uint8_t len = hexByte(p); p += 2;
    uint8_t payload[300];
    len = min((size_t)len, min(sizeof(payload), strlen(p) / 2));
    for (uint16_t i = 0; i < len; ++i) payload[i] = hexByte(p + 2 * i);
    uint8_t opts = (znpOptions & 0x10) ? ESP_ZB_APSDE_TX_OPT_ACK_TX : 0;
    submitApplicationAps(dst, dstEp, srcEp, cluster, payload, len, radius, opts);
    return;
  }

  // ZNP AF_DATA_REQUEST_EXT. APsystems pairing uses address mode 0x0F and
  // FF:FF...FF, which is the all-devices broadcast (0xFFFF) here.
  if (!strncmp(command, "2402", 4) && chars >= 46) {
    const char *p = command + 4;
    uint8_t znpMode = hexByte(p); p += 2;
    p += 16; // extended destination (all FF in the APS pairing sequence)
    uint8_t dstEp = hexByte(p); p += 2;
    p += 4; // destination PAN (FFFF)
    uint8_t srcEp = hexByte(p); p += 2;
    uint16_t cluster = hexLe16(p); p += 4;
    p += 2; // transaction id
    uint8_t znpOptions = hexByte(p); p += 2;
    uint8_t radius = hexByte(p); p += 2;
    uint8_t len = hexByte(p); p += 2;
    uint8_t payload[300];
    len = min((size_t)len, min(sizeof(payload), strlen(p) / 2));
    for (uint16_t i = 0; i < len; ++i) payload[i] = hexByte(p + 2 * i);
    (void)znpMode;
    uint8_t opts = (znpOptions & 0x10) ? ESP_ZB_APSDE_TX_OPT_ACK_TX : 0;
    submitAps(ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT, 0xFFFF, dstEp, srcEp, cluster, payload, len, radius, opts);
  }
}

char *readZB(char out[]) {
  ApsRxFrame f = {};
  out[0] = 0;
  if (!apsRxQueue || xQueueReceive(apsRxQueue, &f, pdMS_TO_TICKS(2500)) != pdTRUE) {
    readCounter = 0;
    return out;
  }

  uint8_t decoded[300];
  size_t decodedLen = 0;
  int which = -1;
  if (apsDecryptIncoming(f.source, f.data, f.len, decoded, sizeof(decoded), &decodedLen, &which)) {
    memcpy(f.data, decoded, decodedLen);
    f.len = decodedLen;
  } else if (f.len >= 8 && !(f.data[6] == 0xFB && f.data[7] == 0xFB)) {
    consoleOut("encrypted APS frame could not be decrypted");
  }

  // Synthetic ZNP acknowledgements keep the original validation paths intact.
  strcpy(out, "FE0164010064FE034480001400D3");
  char header[80];
  uint8_t length = (uint8_t)min((uint16_t)255, f.len);
  snprintf(header, sizeof(header), "FE%02X44810000%02X%02X%02X%02X%02X%02X00%02X000000000000%02X",
           (unsigned)(17 + length), f.cluster & 0xff, f.cluster >> 8,
           f.source & 0xff, f.source >> 8, f.sourceEp, f.destEp, f.lqi, length);
  strncat(out, header, CC2530_MAX_SERIAL_BUFFER_SIZE - strlen(out) - 1);
  appendHex(out, CC2530_MAX_SERIAL_BUFFER_SIZE, f.data, length);
  readCounter = strlen(out) / 2;
  return out;
}

bool waitSerial2Available() { return apsRxQueue && uxQueueMessagesWaiting(apsRxQueue); }
void empty_serial2() { if (apsRxQueue) xQueueReset(apsRxQueue); }

String checkSumString(char command[]) { (void)command; return String(); }
char *sLen(const char command[]) { static char result[3]; snprintf(result, sizeof(result), "%02X", (unsigned)(strlen(command) / 2)); return result; }
int StrToHex(char str[]) { return (int)strtol(str, nullptr, 16); }
String ECU_REVERSE() {
  String id(ECU_ID);
  return id.substring(10,12)+id.substring(8,10)+id.substring(6,8)+id.substring(4,6)+id.substring(2,4)+id.substring(0,2);
}

char *split(char *str, const char *delim) {
  char *p = strstr(str, delim);
  if (!p) return nullptr;
  *p = 0;
  return p + strlen(delim);
}

void inverterReboot(int which) {
  char ecuRev[13], cmd[90];
  ECU_REVERSE().toCharArray(ecuRev, sizeof(ecuRev));
  snprintf(cmd, sizeof(cmd), "2401%s1414060001000F13%sFBFB06C1000000000000A6FEFE", Inv_Prop[which].invID, ecuRev);
  sendZB(cmd);
}

void resetValues(bool energy, bool mustSend) {
  for (int z = 0; z < inverterCount; z++) {
    for (int y = 0; y < 4; y++) Inv_Data[z].power[y] = 0.0;
    if (energy) Inv_Data[z].en_total = 0;
    if (mustSend) mqttPoll(z);
  }
}
