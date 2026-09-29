#include "PAIRING_SESSION.h"

static PairTransportSession pairTransport;
static portMUX_TYPE pairTransportMux = portMUX_INITIALIZER_UNLOCKED;

void pairTransportBegin(const uint8_t uid[6], uint16_t pan) {
  portENTER_CRITICAL(&pairTransportMux);
  pairTransport.begin(uid, pan);
  portEXIT_CRITICAL(&pairTransportMux);
}
void pairTransportStop() {
  portENTER_CRITICAL(&pairTransportMux);
  pairTransport.active = false;
  portEXIT_CRITICAL(&pairTransportMux);
}
void pairTransportStage(uint16_t pan, uint16_t source, bool discovery) {
  empty_serial2();
  portENTER_CRITICAL(&pairTransportMux);
  pairTransport.stage(pan, source, discovery, millis());
  portEXIT_CRITICAL(&pairTransportMux);
}
void pairTransportObserve(const uint8_t *b, size_t n, uint32_t at) {
  portENTER_CRITICAL(&pairTransportMux);
  pairTransport.observe(b, n, at);
  portEXIT_CRITICAL(&pairTransportMux);
}
bool pairTransportAcceptFrame(const uint8_t *b, size_t n, uint32_t at) {
  portENTER_CRITICAL(&pairTransportMux);
  bool accepted = pairTransport.accepts(b, n, at);
  portEXIT_CRITICAL(&pairTransportMux);
  return accepted;
}
bool pairTransportAsdu(uint16_t pan, uint16_t source, uint16_t cluster,
                       const uint8_t *data, size_t n, uint32_t at) {
  portENTER_CRITICAL(&pairTransportMux);
  bool consumed = pairTransport.asdu(pan, source, cluster, data, n, at);
  portEXIT_CRITICAL(&pairTransportMux);
  if (consumed) pairTraceAsdu(pan, source, cluster, data, n, at);
  return consumed;
}
uint16_t pairTransportFound(bool operating) {
  portENTER_CRITICAL(&pairTransportMux);
  uint16_t result = pairTransport.conflict ? 0 :
      (operating ? pairTransport.operatingSource : pairTransport.found);
  portEXIT_CRITICAL(&pairTransportMux);
  return result;
}
bool pairTransportConflict() {
  portENTER_CRITICAL(&pairTransportMux);
  bool conflict = pairTransport.conflict;
  portEXIT_CRITICAL(&pairTransportMux);
  return conflict;
}
void pairTransportWaitForReply() {
  portENTER_CRITICAL(&pairTransportMux);
  pairTransport.reply = PairPowerReply{};
  pairTransport.since = millis();
  pairTransport.waiting = true;
  portEXIT_CRITICAL(&pairTransportMux);
}
bool pairTransportTakeReply(PairPowerReply &reply) {
  portENTER_CRITICAL(&pairTransportMux);
  reply = pairTransport.reply;
  pairTransport.waiting = false;
  bool ok = !pairTransport.conflict && reply.length;
  portEXIT_CRITICAL(&pairTransportMux);
  return ok;
}
