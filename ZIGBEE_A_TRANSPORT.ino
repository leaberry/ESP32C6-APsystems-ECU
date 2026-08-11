/*
 * APsystems native ESP32-C6 802.15.4 transport.
 *
 * The application continues to create the original ZNP AF_DATA_REQUEST hex
 * envelopes. This file decodes those envelopes, emits the equivalent raw
 * MAC/NWK/APS frames, acknowledges APsystems' fragmented APS responses, and
 * places reassembled ASDUs in the legacy decoder queue.
 */

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

extern uint16_t zbOperationalPan;
bool rawRadioSetPan(uint16_t pan);
void radioTraceObserve(const uint8_t *frame, uint8_t captured,
                       uint8_t channel, int8_t rssi, uint8_t lqi);

namespace {
constexpr uint8_t APS_CHANNEL = 16;
constexpr uint8_t RAW_RX_QUEUE_DEPTH = 24;
constexpr uint8_t RAW_MAX_BLOCKS = 8;
constexpr uint8_t RAW_BLOCK_BYTES = 112;
constexpr uint8_t RAW_REASSEMBLY_SLOTS = 4;

struct ApsRxFrame {
  uint16_t cluster;
  uint16_t source;
  uint8_t sourceEp;
  uint8_t destEp;
  int8_t rssi;
  uint8_t lqi;
  uint16_t len;
  uint8_t data[300];
};

struct RawRxFrame {
  uint8_t captured;
  uint8_t channel;
  int8_t rssi;
  uint8_t lqi;
  uint8_t bytes[128];
};

struct RawReassembly {
  bool active;
  uint16_t pan;
  uint16_t macSource;
  uint16_t nwkSource;
  uint16_t cluster;
  uint16_t profile;
  uint8_t destEp;
  uint8_t sourceEp;
  uint8_t counter;
  uint8_t totalBlocks;
  uint8_t receivedMask;
  int8_t rssi;
  uint8_t lqi;
  uint8_t lengths[RAW_MAX_BLOCKS];
  uint8_t blocks[RAW_MAX_BLOCKS][RAW_BLOCK_BYTES];
  uint32_t updatedAt;
};

QueueHandle_t apsRxQueue = nullptr;
QueueHandle_t rawRxQueue = nullptr;
SemaphoreHandle_t rawTxMutex = nullptr;
SemaphoreHandle_t rawTxDone = nullptr;
TaskHandle_t rawWorkerHandle = nullptr;
RawReassembly rawSessions[RAW_REASSEMBLY_SLOTS] = {};
volatile bool rawTxSucceeded = false;
volatile int rawTxFailure = 0;
volatile int apsExpectedWhich = -1;
bool rawRadioStarted = false;
bool rawPromiscuous = false;
uint16_t rawCurrentPan = 0xFFFF;
uint8_t rawMacSequence = 0;
uint8_t rawNwkSequence = 0;
uint8_t rawApsCounter = 0;
uint8_t rawExtendedAddress[8] = {};
uint32_t rawRxDropped = 0;

static uint8_t hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return 0;
}

static uint8_t hexByte(const char *p) {
  return (hexNibble(p[0]) << 4) | hexNibble(p[1]);
}

static uint16_t hexLe16(const char *p) {
  return (uint16_t)hexByte(p) | ((uint16_t)hexByte(p + 2) << 8);
}

static uint16_t readLe16(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void putLe16(uint8_t *out, size_t &pos, uint16_t value) {
  out[pos++] = value & 0xFF;
  out[pos++] = value >> 8;
}

static void appendHex(char *out, size_t cap, const uint8_t *data, size_t len) {
  static const char digits[] = "0123456789ABCDEF";
  size_t used = strlen(out);
  for (size_t i = 0; i < len && used + 2 < cap; ++i) {
    out[used++] = digits[data[i] >> 4];
    out[used++] = digits[data[i] & 15];
  }
  out[used] = 0;
}

static bool serialBytesToText(const uint8_t uid[6], char serial[13]) {
  static const char digits[] = "0123456789ABCDEF";
  if (!uid || !serial) return false;
  for (uint8_t i = 0; i < 6; ++i) {
    serial[i * 2] = digits[uid[i] >> 4];
    serial[i * 2 + 1] = digits[uid[i] & 0x0F];
  }
  serial[12] = 0;
  return true;
}

static bool radioPreference(const char *serial, uint32_t *value, bool write) {
  if (!serial || strlen(serial) != 12) return false;
  Preferences radioPrefs;
  if (!radioPrefs.begin("apsradio", !write)) return false;
  if (write) radioPrefs.putUInt(serial, *value);
  else *value = radioPrefs.getUInt(serial, 0);
  radioPrefs.end();
  return write || *value != 0;
}

static bool radioTransmit(const uint8_t *frame, size_t bytes, bool cca,
                          const char *reason) {
  if (!rawRadioStarted || !frame || bytes < 4 || bytes > 126 ||
      !rawTxMutex || !rawTxDone) return false;
  if (xSemaphoreTake(rawTxMutex, pdMS_TO_TICKS(500)) != pdTRUE) {
    diagnosticsAppend(String("802.15.4 TX busy: ") + reason);
    return false;
  }

  static uint8_t txFrame[128];
  memset(txFrame, 0, sizeof(txFrame));
  txFrame[0] = (uint8_t)(bytes + 2);  // PHY length includes hardware FCS.
  memcpy(txFrame + 1, frame, bytes);
  esp_err_t start = ESP_OK;
  bool completed = false;
  bool ok = false;
  uint8_t attempts = 0;
  // Wi-Fi and 802.15.4 share the C6 RF path. A coexistence or CCA rejection
  // is transient, so retry locally instead of dropping an APS command/ACK.
  for (attempts = 1; attempts <= 12 && !ok; ++attempts) {
    while (xSemaphoreTake(rawTxDone, 0) == pdTRUE) {}
    rawTxSucceeded = false;
    rawTxFailure = 0;
    start = esp_ieee802154_transmit(txFrame, cca);
    completed = start == ESP_OK &&
                xSemaphoreTake(rawTxDone, pdMS_TO_TICKS(160)) == pdTRUE;
    ok = completed && rawTxSucceeded;
    if (ok) break;
    bool transient = start == ESP_OK && completed &&
        (rawTxFailure == ESP_IEEE802154_TX_ERR_CCA_BUSY ||
         rawTxFailure == ESP_IEEE802154_TX_ERR_COEXIST);
    if (!transient) break;
    vTaskDelay(pdMS_TO_TICKS(3 + attempts * 2));
  }
  if (!ok) {
    char line[128];
    snprintf(line, sizeof(line), "802.15.4 TX %s failed start=%s done=%u reason=%d",
             reason ? reason : "frame", esp_err_to_name(start), completed,
             rawTxFailure);
    diagnosticsAppend(String(line));
  } else if (attempts > 1) {
    char line[112];
    snprintf(line, sizeof(line), "802.15.4 TX %s succeeded after %u attempts",
             reason ? reason : "frame", attempts);
    diagnosticsAppend(String(line));
  }
  xSemaphoreGive(rawTxMutex);
  return ok;
}

static bool sendApsAck(uint16_t pan, uint16_t macDestination,
                       uint16_t nwkDestination, uint8_t originalDestEp,
                       uint8_t originalSourceEp, uint16_t cluster,
                       uint16_t profile, uint8_t apsCounter,
                       uint8_t fragmentation, uint8_t blockNumber) {
  uint8_t frame[64] = {};
  size_t p = 0;
  frame[p++] = 0x61;  // Data, PAN compressed, MAC ACK requested.
  frame[p++] = 0x88;  // Short destination and short source.
  frame[p++] = ++rawMacSequence;
  putLe16(frame, p, pan);
  putLe16(frame, p, macDestination);
  putLe16(frame, p, 0x0000);

  putLe16(frame, p, 0x0008);  // NWK data, Zigbee protocol version 2.
  putLe16(frame, p, nwkDestination);
  putLe16(frame, p, 0x0000);
  frame[p++] = 0x0F;
  frame[p++] = ++rawNwkSequence;

  frame[p++] = 0x82;  // APS data ACK, extended header present.
  frame[p++] = originalSourceEp;
  putLe16(frame, p, cluster);
  putLe16(frame, p, profile);
  frame[p++] = originalDestEp;
  frame[p++] = apsCounter;
  frame[p++] = fragmentation;
  frame[p++] = blockNumber;
  frame[p++] = 0xFF;  // Window size one: received block plus unused bits.

  rawRadioSetPan(pan);
  bool ok = radioTransmit(frame, p, true, "APS fragment ACK");
  char line[144];
  snprintf(line, sizeof(line),
           "APS fragment ACK pan=0x%04X dst=0x%04X ctr=%u block=%u result=%s",
           pan, nwkDestination, apsCounter, blockNumber, ok ? "OK" : "FAILED");
  diagnosticsAppend(String(line));
  return ok;
}

static RawReassembly *sessionFor(uint16_t pan, uint16_t source,
                                 uint8_t counter, uint16_t cluster,
                                 bool create) {
  RawReassembly *oldest = &rawSessions[0];
  for (RawReassembly &s : rawSessions) {
    if (s.active && s.pan == pan && s.nwkSource == source &&
        s.counter == counter && s.cluster == cluster) return &s;
    if (!s.active) oldest = &s;
    else if (s.updatedAt < oldest->updatedAt) oldest = &s;
  }
  if (!create) return nullptr;
  memset(oldest, 0, sizeof(*oldest));
  oldest->active = true;
  oldest->pan = pan;
  oldest->nwkSource = source;
  oldest->counter = counter;
  oldest->cluster = cluster;
  oldest->updatedAt = millis();
  return oldest;
}

static void deliverAsdu(RawReassembly *session) {
  if (!session || !apsRxQueue) return;
  ApsRxFrame complete = {};
  complete.cluster = session->cluster;
  complete.source = session->nwkSource;
  complete.sourceEp = session->sourceEp;
  complete.destEp = session->destEp;
  complete.rssi = session->rssi;
  complete.lqi = session->lqi;
  for (uint8_t block = 0; block < session->totalBlocks; ++block) {
    size_t room = sizeof(complete.data) - complete.len;
    size_t take = min(room, (size_t)session->lengths[block]);
    memcpy(complete.data + complete.len, session->blocks[block], take);
    complete.len += take;
  }
  if (complete.len >= 6) {
    char serial[13];
    serialBytesToText(complete.data, serial);
    uint32_t peer = ((uint32_t)session->pan << 16) | session->nwkSource;
    radioPreference(serial, &peer, true);
    char line[160];
    snprintf(line, sizeof(line),
             "APS reassembled serial=%s pan=0x%04X src=0x%04X blocks=%u len=%u",
             serial, session->pan, session->nwkSource, session->totalBlocks,
             complete.len);
    diagnosticsAppend(String(line));
  }
  xQueueSend(apsRxQueue, &complete, 0);
  session->active = false;
}

static void processApsFrame(const RawRxFrame &rx) {
  const uint8_t *b = rx.bytes;
  if (rx.captured < 29) return;
  uint8_t phyLength = b[0];
  size_t end = phyLength > 2 ? min((size_t)rx.captured, (size_t)phyLength - 1U) : 0;
  if (end < 28) return;

  // Current APsystems frames use compressed short/short MAC addressing.
  uint16_t macFcf = readLe16(b + 1);
  if ((macFcf & 0xCC00) != 0x8800 || !(macFcf & 0x0040)) return;
  uint16_t pan = readLe16(b + 4);
  uint16_t macDestination = readLe16(b + 6);
  uint16_t macSource = readLe16(b + 8);
  if (macDestination != 0x0000 && macDestination != 0xFFFF) return;

  size_t p = 10;
  uint16_t nwkFcf = readLe16(b + p);
  if ((nwkFcf & 0x0003) != 0 || ((nwkFcf >> 2) & 0x0F) != 2) return;
  uint16_t nwkDestination = readLe16(b + p + 2);
  uint16_t nwkSource = readLe16(b + p + 4);
  p += 8;
  if (nwkFcf & 0x0800) p += 8;
  if (nwkFcf & 0x1000) p += 8;
  if (nwkFcf & 0x0100) p += 1;
  if (nwkFcf & 0x0400) {
    if (p + 2 > end) return;
    p += 2 + (size_t)b[p] * 2;
  }
  if (nwkDestination != 0x0000 || p + 8 > end) return;

  uint8_t apsFcf = b[p++];
  if ((apsFcf & 0x03) != 0) return;  // APS data only.
  uint8_t delivery = (apsFcf >> 2) & 0x03;
  if (delivery != 0) return;         // Inverter replies are unicast.
  uint8_t destEp = b[p++];
  uint16_t cluster = readLe16(b + p); p += 2;
  uint16_t profile = readLe16(b + p); p += 2;
  uint8_t sourceEp = b[p++];
  uint8_t counter = b[p++];
  if (profile != 0x0F05 || destEp != 0x14) return;

  uint8_t fragmentation = 0;
  uint8_t blockField = 0;
  if (apsFcf & 0x80) {
    if (p + 2 > end) return;
    fragmentation = b[p++] & 0x03;
    if (fragmentation) blockField = b[p++];
  }
  size_t payloadLength = end > p ? end - p : 0;

  if (!fragmentation) {
    if (!payloadLength || payloadLength > 300 || !apsRxQueue) return;
    ApsRxFrame frame = {};
    frame.cluster = cluster;
    frame.source = nwkSource;
    frame.sourceEp = sourceEp;
    frame.destEp = destEp;
    frame.rssi = rx.rssi;
    frame.lqi = rx.lqi;
    frame.len = payloadLength;
    memcpy(frame.data, b + p, payloadLength);
    xQueueSend(apsRxQueue, &frame, 0);
    return;
  }

  uint8_t blockNumber = fragmentation == 1 ? 0 : blockField;
  uint8_t totalBlocks = fragmentation == 1 ? blockField : 0;
  if (blockNumber >= RAW_MAX_BLOCKS || payloadLength > RAW_BLOCK_BYTES) return;
  RawReassembly *session = sessionFor(pan, nwkSource, counter, cluster,
                                      fragmentation == 1);
  if (!session) return;
  if (fragmentation == 1) {
    if (!totalBlocks || totalBlocks > RAW_MAX_BLOCKS) {
      session->active = false;
      return;
    }
    session->totalBlocks = totalBlocks;
    session->macSource = macSource;
    session->profile = profile;
    session->destEp = destEp;
    session->sourceEp = sourceEp;
  }
  if (!session->totalBlocks || blockNumber >= session->totalBlocks) return;
  if (!(session->receivedMask & (1U << blockNumber))) {
    // Preserve the weakest fragment's radio metadata. A reassembled APS
    // response is only as reliable as its weakest received fragment.
    if (!session->receivedMask) {
      session->rssi = rx.rssi;
      session->lqi = rx.lqi;
    } else {
      session->rssi = min(session->rssi, rx.rssi);
      session->lqi = min(session->lqi, rx.lqi);
    }
    memcpy(session->blocks[blockNumber], b + p, payloadLength);
    session->lengths[blockNumber] = payloadLength;
    session->receivedMask |= 1U << blockNumber;
  }
  session->updatedAt = millis();

  sendApsAck(pan, macSource, nwkSource, destEp, sourceEp, cluster, profile,
             counter, fragmentation, blockNumber);
  uint8_t wantedMask = (1U << session->totalBlocks) - 1U;
  if ((session->receivedMask & wantedMask) == wantedMask) deliverAsdu(session);
}

static void rawWorker(void *) {
  RawRxFrame frame;
  for (;;) {
    if (xQueueReceive(rawRxQueue, &frame, portMAX_DELAY) == pdTRUE) {
      radioTraceObserve(frame.bytes, frame.captured, frame.channel,
                        frame.rssi, frame.lqi);
      processApsFrame(frame);
    }
  }
}

static bool submitRawAps(uint16_t requestedDestination, uint8_t dstEp,
                         uint8_t srcEp, uint16_t cluster,
                         const uint8_t *asdu, uint16_t asduLength,
                         uint8_t radius, uint8_t options) {
  int which = requestedDestination == 0xFFFF
                  ? -1 : apsFindInverter(requestedDestination, nullptr);
  apsExpectedWhich = which;

  uint16_t pan = rawCurrentPan;
  if (which >= 0) {
    uint32_t peer = 0;
    if (radioPreference(Inv_Prop[which].invSerial, &peer, false)) {
      pan = peer >> 16;
    } else {
      char line[128];
      snprintf(line, sizeof(line),
               "no learned radio PAN for inverter %d serial=%s", which,
               Inv_Prop[which].invSerial);
      diagnosticsAppend(String(line));
    }
  }
  rawRadioSetPan(pan);

  uint8_t encrypted[300];
  size_t encryptedLength = 0;
  bool useEncryption = which >= 0 ? apsInverterUsesEncryption(which)
                                  : apsAllInvertersEncrypted();
  if (useEncryption && apsEncryptOutgoing(which, asdu, asduLength, encrypted,
                                           sizeof(encrypted), &encryptedLength,
                                           which < 0)) {
    asdu = encrypted;
    asduLength = encryptedLength;
  }

  uint8_t frame[128] = {};
  size_t p = 0;
  frame[p++] = 0x41;  // MAC data, PAN compressed, no ACK for broadcast.
  frame[p++] = 0x88;
  frame[p++] = ++rawMacSequence;
  putLe16(frame, p, pan);
  putLe16(frame, p, 0xFFFF);
  putLe16(frame, p, 0x0000);

  putLe16(frame, p, 0x1008);  // NWK data with source IEEE address.
  putLe16(frame, p, 0xFFFF);
  putLe16(frame, p, 0x0000);
  frame[p++] = radius ? radius : 0x0F;
  frame[p++] = ++rawNwkSequence;
  memcpy(frame + p, rawExtendedAddress, sizeof(rawExtendedAddress));
  p += sizeof(rawExtendedAddress);

  frame[p++] = 0x08;  // APS broadcast delivery.
  frame[p++] = dstEp;
  putLe16(frame, p, cluster);
  putLe16(frame, p, 0x0F05);
  frame[p++] = srcEp;
  frame[p++] = ++rawApsCounter;
  if (p + asduLength > sizeof(frame)) return false;
  memcpy(frame + p, asdu, asduLength);
  p += asduLength;

  bool ok = radioTransmit(frame, p, true, "APsystems request");
  char line[176];
  snprintf(line, sizeof(line),
           "APS raw TX pan=0x%04X logical_dst=0x%04X target=%d cluster=0x%04X len=%u opts=0x%02X result=%s",
           pan, requestedDestination, which, cluster, asduLength, options,
           ok ? "OK" : "FAILED");
  diagnosticsAppend(String(line));
  return ok;
}
}  // namespace

extern "C" void IRAM_ATTR esp_ieee802154_receive_done(
    uint8_t *frame, esp_ieee802154_frame_info_t *info) {
  BaseType_t wake = pdFALSE;
  if (rawRxQueue && frame && info) {
    RawRxFrame copy = {};
    uint8_t count = min((uint16_t)sizeof(copy.bytes),
                        (uint16_t)(frame[0] + 1U));
    copy.captured = count;
    copy.channel = info->channel;
    copy.rssi = info->rssi;
    copy.lqi = info->lqi;
    for (uint8_t i = 0; i < count; ++i) copy.bytes[i] = frame[i];
    if (xQueueSendFromISR(rawRxQueue, &copy, &wake) != pdTRUE) ++rawRxDropped;
  }
  esp_ieee802154_receive_handle_done(frame);
  if (wake) portYIELD_FROM_ISR();
}

extern "C" void IRAM_ATTR esp_ieee802154_transmit_done(
    const uint8_t *frame, const uint8_t *ack,
    esp_ieee802154_frame_info_t *ackInfo) {
  (void)frame;
  (void)ackInfo;
  if (ack) esp_ieee802154_receive_handle_done(ack);
  rawTxSucceeded = true;
  BaseType_t wake = pdFALSE;
  if (rawTxDone) xSemaphoreGiveFromISR(rawTxDone, &wake);
  if (wake) portYIELD_FROM_ISR();
}

extern "C" void IRAM_ATTR esp_ieee802154_transmit_failed(
    const uint8_t *frame, esp_ieee802154_tx_error_t error) {
  (void)frame;
  rawTxSucceeded = false;
  rawTxFailure = (int)error;
  BaseType_t wake = pdFALSE;
  if (rawTxDone) xSemaphoreGiveFromISR(rawTxDone, &wake);
  if (wake) portYIELD_FROM_ISR();
}

bool apsRadioRememberPeer(const char *serial, uint16_t pan, uint16_t source) {
  uint32_t peer = ((uint32_t)pan << 16) | source;
  return pan != 0 && pan != 0xFFFF && radioPreference(serial, &peer, true);
}

bool apsRadioLoadPeer(const char *serial, uint16_t *pan, uint16_t *source) {
  uint32_t peer = 0;
  if (!radioPreference(serial, &peer, false)) return false;
  if (pan) *pan = peer >> 16;
  if (source) *source = peer & 0xFFFF;
  return true;
}

bool rawRadioSetPan(uint16_t pan) {
  esp_err_t a = esp_ieee802154_set_panid(pan);
  esp_err_t b = esp_ieee802154_set_short_address(0x0000);
  if (a == ESP_OK && b == ESP_OK) rawCurrentPan = pan;
  return a == ESP_OK && b == ESP_OK;
}

bool rawRadioSetPromiscuous(bool enabled) {
  esp_err_t result = esp_ieee802154_set_promiscuous(enabled);
  if (result == ESP_OK) rawPromiscuous = enabled;
  return result == ESP_OK;
}

bool rawRadioStart() {
  if (rawRadioStarted) return true;
  apsRxQueue = xQueueCreate(12, sizeof(ApsRxFrame));
  rawRxQueue = xQueueCreate(RAW_RX_QUEUE_DEPTH, sizeof(RawRxFrame));
  rawTxMutex = xSemaphoreCreateMutex();
  rawTxDone = xSemaphoreCreateBinary();
  if (!apsRxQueue || !rawRxQueue || !rawTxMutex || !rawTxDone) return false;

  esp_err_t enabled = esp_ieee802154_enable();
  if (enabled != ESP_OK) return false;
  esp_err_t coexist = esp_coex_wifi_i154_enable();
  esp_ieee802154_set_channel(APS_CHANNEL);
  esp_ieee802154_set_coordinator(true);
  esp_ieee802154_set_rx_when_idle(true);
  rawRadioSetPan(zbOperationalPan);
  rawRadioSetPromiscuous(false);

  uint8_t ieee[8] = {};
  if (esp_read_mac(ieee, ESP_MAC_IEEE802154) == ESP_OK) {
    for (uint8_t i = 0; i < 8; ++i) rawExtendedAddress[i] = ieee[7 - i];
    esp_ieee802154_set_extended_address(rawExtendedAddress);
  } else {
    esp_ieee802154_get_extended_address(rawExtendedAddress);
  }

  if (xTaskCreate(rawWorker, "aps_raw_radio", 6144, nullptr, 6,
                  &rawWorkerHandle) != pdPASS) return false;
  rawRadioStarted = esp_ieee802154_receive() == ESP_OK;
  char line[160];
  snprintf(line, sizeof(line),
           "native 802.15.4 start channel=%u pan=0x%04X coex=%s result=%s",
           APS_CHANNEL, rawCurrentPan, esp_err_to_name(coexist),
           rawRadioStarted ? "OK" : "FAILED");
  diagnosticsAppend(String(line));
  return rawRadioStarted;
}

void sendZB(char command[]) {
  size_t chars = strlen(command);
  if (chars < 4 || (chars & 1)) return;

  if (!strncmp(command, "2401", 4) && chars >= 24) {
    const char *p = command + 4;
    uint16_t destination = hexLe16(p); p += 4;
    uint8_t dstEp = hexByte(p); p += 2;
    uint8_t srcEp = hexByte(p); p += 2;
    uint16_t cluster = hexLe16(p); p += 4;
    p += 2;
    uint8_t options = hexByte(p); p += 2;
    uint8_t radius = hexByte(p); p += 2;
    uint8_t length = hexByte(p); p += 2;
    uint8_t payload[300];
    length = min((size_t)length, min(sizeof(payload), strlen(p) / 2));
    for (uint16_t i = 0; i < length; ++i) payload[i] = hexByte(p + 2 * i);
    submitRawAps(destination, dstEp, srcEp, cluster, payload, length,
                 radius, options);
    return;
  }

  if (!strncmp(command, "2402", 4) && chars >= 46) {
    const char *p = command + 4;
    p += 2 + 16;
    uint8_t dstEp = hexByte(p); p += 2;
    p += 4;
    uint8_t srcEp = hexByte(p); p += 2;
    uint16_t cluster = hexLe16(p); p += 4;
    p += 2;
    uint8_t options = hexByte(p); p += 2;
    uint8_t radius = hexByte(p); p += 2;
    uint8_t length = hexByte(p); p += 2;
    uint8_t payload[300];
    length = min((size_t)length, min(sizeof(payload), strlen(p) / 2));
    for (uint16_t i = 0; i < length; ++i) payload[i] = hexByte(p + 2 * i);
    apsExpectedWhich = -1;
    submitRawAps(0xFFFF, dstEp, srcEp, cluster, payload, length,
                 radius, options);
  }
}

char *readZB(char out[]) {
  out[0] = 0;
  if (!apsRxQueue) return out;
  uint32_t started = millis();
  while (millis() - started < 3200) {
    ApsRxFrame f = {};
    uint32_t left = 3200 - (millis() - started);
    if (xQueueReceive(apsRxQueue, &f, pdMS_TO_TICKS(left)) != pdTRUE) break;

    uint8_t decoded[300];
    size_t decodedLen = 0;
    int which = -1;
    if (apsDecryptIncoming(f.source, f.data, f.len, decoded, sizeof(decoded),
                           &decodedLen, &which)) {
      memcpy(f.data, decoded, decodedLen);
      f.len = decodedLen;
    } else if (f.len >= 8 && !(f.data[6] == 0xFB && f.data[7] == 0xFB)) {
      consoleOut(F("encrypted APS frame could not be decrypted"));
      continue;
    }
    if (apsExpectedWhich >= 0 && which != apsExpectedWhich) {
      char line[96];
      snprintf(line, sizeof(line), "ignoring response for inverter %d while waiting for %d",
               which, (int)apsExpectedWhich);
      diagnosticsAppend(String(line));
      continue;
    }
    if (which >= 0 && which < inverterCount) {
      Inv_Data[which].radioRssi = f.rssi;
      Inv_Data[which].radioLqi = f.lqi;
      Inv_Data[which].sigQ = (float)f.lqi * 100.0F / 255.0F;
      Inv_Data[which].radioMetricsValid = true;
    }

    strcpy(out, "FE0164010064FE034480001400D3");
    char header[80];
    uint8_t length = (uint8_t)min((uint16_t)255, f.len);
    snprintf(header, sizeof(header),
             "FE%02X44810000%02X%02X%02X%02X%02X%02X00%02X000000000000%02X",
             (unsigned)(17 + length), f.cluster & 0xff, f.cluster >> 8,
             f.source & 0xff, f.source >> 8, f.sourceEp, f.destEp, f.lqi,
             length);
    strncat(out, header, CC2530_MAX_SERIAL_BUFFER_SIZE - strlen(out) - 1);
    appendHex(out, CC2530_MAX_SERIAL_BUFFER_SIZE, f.data, length);
    readCounter = strlen(out) / 2;
    return out;
  }
  readCounter = 0;
  return out;
}

bool waitSerial2Available() {
  return apsRxQueue && uxQueueMessagesWaiting(apsRxQueue);
}

void empty_serial2() {
  if (apsRxQueue) xQueueReset(apsRxQueue);
}

String checkSumString(char command[]) { (void)command; return String(); }
char *sLen(const char command[]) {
  static char result[3];
  snprintf(result, sizeof(result), "%02X", (unsigned)(strlen(command) / 2));
  return result;
}
int StrToHex(char str[]) { return (int)strtol(str, nullptr, 16); }
String ECU_REVERSE() {
  String id(ECU_ID);
  return id.substring(10,12)+id.substring(8,10)+id.substring(6,8)+
         id.substring(4,6)+id.substring(2,4)+id.substring(0,2);
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
  snprintf(cmd, sizeof(cmd), "2401%s1414060001000F13%sFBFB06C1000000000000A6FEFE",
           Inv_Prop[which].invID, ecuRev);
  sendZB(cmd);
}

void resetValues(bool energy, bool mustSend) {
  for (int z = 0; z < inverterCount; z++) {
    for (int y = 0; y < 4; y++) Inv_Data[z].power[y] = 0.0;
    if (energy) Inv_Data[z].en_total = 0;
    if (mustSend) mqttPoll(z);
  }
}
