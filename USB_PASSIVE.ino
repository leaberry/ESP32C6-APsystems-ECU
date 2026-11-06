#if APS_USB_PASSIVE
#include <esp_bt.h>
#include "hal/ieee802154_ll.h"

namespace usbListener {
constexpr uint32_t durationMs = 900000;
constexpr uint8_t channel = 16;
QueueHandle_t queue = nullptr;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
volatile bool receiving = false;
volatile uint32_t received = 0, dropped = 0, invalid = 0, transmitted = 0;
bool ready = false, session = false, statusPending = true, headerPending = false;
bool columnsPending = false;
uint32_t started = 0, ended = 0, written = 0, progressAt = 0, idleAt = 0;
uint32_t lastUsbProgress = 0;
const char *reason = "idle";
const char *antenna = "saved";
const char *fault = "none";
AntennaSettings antennaSettings;
char output[640] = {}, command[48] = {};
size_t outputLength = 0, outputSent = 0, commandLength = 0;
bool commandOverflow = false;

bool otherRadiosOff() {
  wifi_mode_t mode;
  return esp_wifi_get_mode(&mode) == ESP_ERR_WIFI_NOT_INIT &&
         esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE;
}

bool ackOff() {
  return !ieee802154_ll_get_tx_auto_ack() &&
         !ieee802154_ll_get_rx_auto_ack() &&
         !ieee802154_ll_get_tx_enhance_ack();
}

void stop(const char *why) {
  if (!session || ended) return;
  portENTER_CRITICAL(&mux);
  receiving = false;
  portEXIT_CRITICAL(&mux);
  if (esp_ieee802154_sleep() != ESP_OK) {
    ready = false;
    fault = "sleep-failed";
    esp_ieee802154_disable();
  }
  ended = millis();
  reason = why;
}

void applyAntenna() {
  if (!antennaSettings.mode) return;
  if (antennaSettings.enablePin >= 0) {
    pinMode(antennaSettings.enablePin, OUTPUT);
    digitalWrite(antennaSettings.enablePin, antennaSettings.enableHigh ? HIGH : LOW);
    delay(100);
  }
  pinMode(antennaSettings.selectPin, OUTPUT);
  digitalWrite(antennaSettings.selectPin,
               antennaSelectLevel(antennaSettings) ? HIGH : LOW);
}

void start() {
  if (!ready || session || !otherRadiosOff()) { statusPending = true; return; }
  xQueueReset(queue);
  received = dropped = invalid = transmitted = 0;
  started = millis(); ended = written = 0; progressAt = started;
  lastUsbProgress = started;
  reason = "recording";
  session = true;
  headerPending = columnsPending = true;
  bool ok = esp_ieee802154_set_promiscuous(true) == ESP_OK &&
            esp_ieee802154_set_coordinator(false) == ESP_OK &&
            esp_ieee802154_set_channel(channel) == ESP_OK &&
            esp_ieee802154_set_rx_when_idle(true) == ESP_OK &&
            esp_ieee802154_receive() == ESP_OK;
  // Read hardware ACK bits after receive applies the driver's pending settings.
  ok = ok && esp_ieee802154_get_promiscuous() &&
       esp_ieee802154_get_channel() == channel && ackOff();
  if (!ok) { ready = false; fault = "radio-setup"; stop("radio-setup-failed"); }
  else {
    portENTER_CRITICAL(&mux);
    receiving = true;
    portEXIT_CRITICAL(&mux);
  }
}

void processCommand() {
  if (!strcmp(command, "STOP")) stop("user-stop");
  else if (!strcmp(command, "START")) start();
  else if (!strcmp(command, "STATUS")) statusPending = true;
  else if (!session && !strcmp(command, "ANTENNA XIAO")) {
    antennaSettings = AntennaSettings(); antennaSettings.mode = 2;
    antennaPreset(antennaSettings); antenna = "xiao-external";
    applyAntenna(); statusPending = true;
  } else if (!session && !strcmp(command, "ANTENNA SAVED")) {
    if (antennaLoad(antennaSettings)) { antenna = "saved"; applyAntenna(); }
    else { ready = false; fault = "antenna-settings"; }
    statusPending = true;
  } else { statusPending = true; }
}

void status() {
  snprintf(output, sizeof(output),
      "# aps-usb-ready-v1 firmware=%s ready=%u state=%s channel=%u "
      "wifi=%s bluetooth=%s antenna=%s antenna_mode=%d select_pin=%d "
      "enable_pin=%d select_level=%d enable_level=%d fault=%s\n",
      VERSION, ready && otherRadiosOff(), session ? (ended ? "draining" : "recording") : "idle",
      channel, otherRadiosOff() ? "off" : "unexpected", otherRadiosOff() ? "off" : "unexpected",
      antenna, antennaSettings.mode, antennaSettings.selectPin, antennaSettings.enablePin,
      antennaSettings.mode ? digitalRead(antennaSettings.selectPin) : -1,
      antennaSettings.mode && antennaSettings.enablePin >= 0 ? digitalRead(antennaSettings.enablePin) : -1,
      fault);
}
} // namespace usbListener

void usbPassiveSetup() {
  using namespace usbListener;
  Serial.setTxTimeoutMs(0);
  // No filesystem mount, identity generation, Wi-Fi startup, BLE startup or ECU services.
  esp_wifi_stop();
  esp_wifi_deinit();
  if (!otherRadiosOff()) { fault = "other-radio-active"; return; }
  if (!antennaLoad(antennaSettings)) { fault = "antenna-settings"; return; }
  applyAntenna();
  queue = xQueueCreate(256, sizeof(UsbPassivePacket));
  if (!queue) { fault = "queue-allocation"; return; }
  if (esp_ieee802154_enable() != ESP_OK || esp_ieee802154_sleep() != ESP_OK ||
      esp_ieee802154_set_promiscuous(true) != ESP_OK ||
      !esp_ieee802154_get_promiscuous()) {
    esp_ieee802154_disable(); fault = "radio-init"; return;
  }
  ready = true;
}

void IRAM_ATTR usbPassiveReceive(uint8_t *frame, uint8_t ch, int8_t rssi, uint8_t lqi) {
  using namespace usbListener;
  BaseType_t wake = pdFALSE;
  portENTER_CRITICAL_ISR(&mux);
  if (receiving) {
    if (!frame || frame[0] < 3 || frame[0] > 127 || ch != channel) ++invalid;
    else {
      UsbPassivePacket p = {};
      p.sequence = ++received; p.at = millis(); p.channel = ch;
      p.rssi = rssi; p.lqi = lqi; p.length = frame[0] + 1U;
      for (unsigned i = 0; i < p.length; ++i) p.bytes[i] = frame[i];
      if (xQueueSendFromISR(queue, &p, &wake) != pdTRUE) ++dropped;
    }
  }
  portEXIT_CRITICAL_ISR(&mux);
  if (wake) portYIELD_FROM_ISR();
}

void IRAM_ATTR usbPassiveTransmitted() {
  portENTER_CRITICAL_ISR(&usbListener::mux);
  ++usbListener::transmitted;
  portEXIT_CRITICAL_ISR(&usbListener::mux);
}

void usbPassiveLoop() {
  using namespace usbListener;
  // Keep STOP and the timeout responsive even if the computer stops reading USB.
  for (unsigned n = 0; n < 64 && Serial.available(); ++n) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      command[commandLength] = 0;
      if (commandLength && !commandOverflow) processCommand();
      commandLength = 0; commandOverflow = false;
    } else if (commandLength + 1 < sizeof(command)) command[commandLength++] = c;
    else commandOverflow = true;
  }
  uint32_t now = millis();
  if (session && !ended) {
    if (transmitted || !otherRadiosOff() || !ackOff()) {
      ready = false; fault = "radio-state-changed"; stop("radio-state-changed");
    } else if (!Serial) stop("usb-disconnected");
    else if (now - started >= durationMs) stop("time-limit");
    else if (now - lastUsbProgress >= 10000) stop("usb-stalled");
  }
  if (!Serial) { delay(1); return; }
  if (outputSent < outputLength) {
    int space = Serial.availableForWrite();
    if (space > 0) {
      size_t n = min((size_t)space, outputLength - outputSent);
      size_t sent = Serial.write((const uint8_t *)output + outputSent, n);
      outputSent += sent;
      if (sent) lastUsbProgress = now;
    }
    delay(1); return;
  }
  outputLength = outputSent = 0;
  if (headerPending) {
    headerPending = false;
    snprintf(output, sizeof(output),
        "# aps-passive-v1 firmware=%s transport=usb channel=%u started_ms=%lu "
        "duration_ms=%lu wifi=off bluetooth=off promiscuous=%u ack_tx=%u ack_rx=%u "
        "enh_ack=%u antenna=%s antenna_mode=%d select_pin=%d enable_pin=%d "
        "select_level=%d enable_level=%d\n", VERSION, channel,
        (unsigned long)started, (unsigned long)durationMs, esp_ieee802154_get_promiscuous(),
        ieee802154_ll_get_tx_auto_ack(), ieee802154_ll_get_rx_auto_ack(),
        ieee802154_ll_get_tx_enhance_ack(), antenna, antennaSettings.mode,
        antennaSettings.selectPin, antennaSettings.enablePin,
        antennaSettings.mode ? digitalRead(antennaSettings.selectPin) : -1,
        antennaSettings.mode && antennaSettings.enablePin >= 0 ? digitalRead(antennaSettings.enablePin) : -1);
  } else if (columnsPending) {
    columnsPending = false;
    strcpy(output, "uptime_ms,channel,rssi_dbm,lqi,raw_hex,sequence\n");
  } else if (session) {
    UsbPassivePacket p;
    if (xQueueReceive(queue, &p, 0) == pdTRUE) {
      outputLength = usbPassiveFormat(p, output, sizeof(output));
      ++written;
    } else if (ended) {
      snprintf(output, sizeof(output),
          "# end reason=%s elapsed_ms=%lu received=%lu written=%lu queue_drops=%lu "
          "invalid=%lu tx=%lu fault=%s\n", reason, (unsigned long)(ended - started),
          (unsigned long)received, (unsigned long)written, (unsigned long)dropped,
          (unsigned long)invalid, (unsigned long)transmitted, fault);
      session = false; statusPending = false; idleAt = now;
    } else if (now - progressAt >= 5000) {
      progressAt = now;
      snprintf(output, sizeof(output), "# progress elapsed_ms=%lu received=%lu written=%lu queue_drops=%lu\n",
          (unsigned long)(now - started), (unsigned long)received,
          (unsigned long)written, (unsigned long)dropped);
    } else { delay(1); return; }
  } else if (statusPending || now - idleAt >= 2000) {
    statusPending = false; idleAt = now; status();
  } else { delay(1); return; }
  if (!outputLength) outputLength = strlen(output);
}
#endif
