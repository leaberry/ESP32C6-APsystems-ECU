#include "ENCRYPTED_PROBE.h"
#include <memory>

extern uint16_t zbOperationalPan;
bool rawRadioSetPromiscuous(bool enabled);
bool apsRadioLoadPeer(const char *serial, uint16_t *pan, uint16_t *source);

static ProbeCapture encryptedCapture;
static portMUX_TYPE encryptedProbeMux = portMUX_INITIALIZER_UNLOCKED;
static uint16_t encryptedProbeReaders = 0;

bool encryptedProbeBusy() {
  portENTER_CRITICAL(&encryptedProbeMux);
  bool busy=encryptedCapture.active || encryptedProbeReaders;
  portEXIT_CRITICAL(&encryptedProbeMux);
  return busy;
}

void encryptedProbeObserve(const uint8_t *b, size_t n, uint32_t at, int8_t rssi, uint8_t lqi) {
  portENTER_CRITICAL(&encryptedProbeMux);
  encryptedCapture.observe(b,n,at,rssi,lqi);
  portEXIT_CRITICAL(&encryptedProbeMux);
}

bool encryptedProbeAcceptFrame(const uint8_t *b, size_t n, uint32_t at) {
  portENTER_CRITICAL(&encryptedProbeMux);
  bool ok=encryptedCapture.accepts(b,n) &&
    (int32_t)(at-encryptedCapture.started-encryptedCapture.phases[encryptedCapture.phase].started)>=0;
  portEXIT_CRITICAL(&encryptedProbeMux);
  return ok;
}

bool encryptedProbeAsdu(uint16_t pan, uint16_t source, uint16_t cluster,
                        const uint8_t *data, size_t n, uint32_t at) {
  portENTER_CRITICAL(&encryptedProbeMux);
  bool consumed=encryptedCapture.asdu(pan,source,cluster,data,n,at);
  portEXIT_CRITICAL(&encryptedProbeMux);
  return consumed;
}

static void encryptedProbeStage(uint8_t index, uint16_t pan, uint16_t source, uint8_t mode) {
  empty_serial2();
  portENTER_CRITICAL(&encryptedProbeMux);
  encryptedCapture.stage(index,pan,source,mode,millis());
  portEXIT_CRITICAL(&encryptedProbeMux);
}

// Every encrypted variant starts from the same fixed read-only DC request.
bool encryptedProbePayload(int which, uint8_t mode, uint8_t *out, size_t cap, size_t *len) {
  uint8_t ecu[6], plain[19], cipher[48]; size_t plainLen=0, cipherLen=0;
  char serial[13]; ECU_REVERSE().toCharArray(serial,sizeof(serial));
  if ((mode>5 && mode!=7) || !apsSerialToBcd(serial,ecu) || !probePlainInfo(ecu,plain,sizeof(plain),&plainLen)) return false;
  // Exact read-only BB request used by normal polling; no caller-selected opcode.
  if (mode==7) { plain[9]=0xBB; plain[16]=0xC1; }
  if (mode==0 || mode==7) { if(cap<plainLen) return false; memcpy(out,plain,plainLen); *len=plainLen; return true; }
  if (!apsEncryptOutgoing(which,plain,plainLen,cipher,sizeof(cipher),&cipherLen,mode>=4)) return false;
  bool marker=mode>=2, prefix=mode!=3 && mode!=5;
  size_t needed=(prefix?6:0)+(marker?1:0)+cipherLen-6;
  if(cap<needed) return false;
  size_t p=0;
  if(prefix) {memcpy(out,cipher,6);p=6;}
  if(marker) out[p++]=mode>=4?0xA0:0xA1;
  memcpy(out+p,cipher+6,cipherLen-6); *len=needed; return true;
}

static bool encryptedProbeInfoPhase(uint8_t phase, int which, uint16_t pan,
                                    uint16_t source, uint8_t mode, bool broadcast) {
  encryptedProbeStage(phase,pan,source,mode);
  portENTER_CRITICAL(&encryptedProbeMux);
  encryptedCapture.phases[phase].broadcast=broadcast;
  portEXIT_CRITICAL(&encryptedProbeMux);
  if(!apsUseSpecificPan(pan,"read-only encrypted probe")) return false;
  for(uint8_t repeat=0;repeat<2;++repeat) {
    uint8_t payload[64]; size_t len=0; int error=-1;
    if(!encryptedProbePayload(which,mode,payload,sizeof(payload),&len)) return false;
    uint32_t at=millis();
    bool ok=apsSendDiagnosticInfo(pan,source,payload,len,broadcast,&error);
    portENTER_CRITICAL(&encryptedProbeMux);
    auto &p=encryptedCapture.phases[phase]; p.sent=repeat+1;
    p.txOk[repeat]=ok; p.txError[repeat]=error;
    p.txMs[repeat]=at-encryptedCapture.started;
    p.txLength[repeat]=len; memcpy(p.tx[repeat],payload,len);
    portEXIT_CRITICAL(&encryptedProbeMux);
    // A missing MAC ACK is evidence, not grounds for skipping all other layouts.
    delay(mode==7 ? 6000 : 3500);
  }
  return true;
}

static bool encryptedProbeDiscovery(uint8_t phase, int which) {
  encryptedProbeStage(phase,0xFFFF,0,6);
  if(!apsUsePairingPan(true)) return false;
  char cmd[96];
  snprintf(cmd,sizeof(cmd),"24020FFFFFFFFFFFFFFFFF14FFFF140C0201000F0600%s",Inv_Prop[which].invSerial);
  for(uint8_t repeat=0;repeat<2;++repeat) {
    uint32_t at=millis(); bool ok=sendZB(cmd);
    portENTER_CRITICAL(&encryptedProbeMux);
    auto &p=encryptedCapture.phases[phase]; p.sent=repeat+1; p.txOk[repeat]=ok;
    p.txMs[repeat]=at-encryptedCapture.started; p.txError[repeat]=ok?0:-1;
    p.txLength[repeat]=6; memcpy(p.tx[repeat],encryptedCapture.target,6);
    portEXIT_CRITICAL(&encryptedProbeMux);
    if(!ok) return false;
    delay(3500);
  }
  return true;
}

bool encryptedProbeRun(int which) {
  uint8_t uid[6];
  if(which<0 || which>=inverterCount || !flightRecorderEnabled ||
     !apsSerialDefaultsToEncrypted(Inv_Prop[which].invSerial) ||
     !pairSerialBytes(Inv_Prop[which].invSerial,uid) || encryptedProbeBusy()) return false;
  // Reuse the normal operation owner to pause polling and prevent route learning.
  if(!pairReceiveBegin(Inv_Prop[which].invSerial,zbOperationalPan)) return false;
  empty_serial2();
  portENTER_CRITICAL(&encryptedProbeMux);
  encryptedCapture.begin(uid,millis());
  portEXIT_CRITICAL(&encryptedProbeMux);
  bool ok=rawRadioSetPromiscuous(false);
  uint16_t target=0; bool conflict=false;
  // Up to three separately logged discovery windows; never use a cached peer.
  for(uint8_t attempt=0;attempt<3 && ok && !target && !conflict;++attempt) {
    ok=encryptedProbeDiscovery(attempt,which);
    portENTER_CRITICAL(&encryptedProbeMux);
    target=encryptedCapture.discovered; conflict=encryptedCapture.conflict;
    portEXIT_CRITICAL(&encryptedProbeMux);
  }
  ok=ok && pairValidAddress(target) && !conflict;
  int control=-1; uint16_t controlPan=0,controlSource=0;
  for(int i=0;i<inverterCount;++i) {
    if(i!=which && !apsInverterUsesEncryption(i) && strcmp(Inv_Prop[i].invID,"0000") &&
       apsRadioLoadPeer(Inv_Prop[i].invSerial,&controlPan,&controlSource) &&
       controlPan && controlPan!=0xFFFF && pairValidAddress(controlSource)) {control=i;break;}
  }
  if(ok && control>=0) ok=encryptedProbeInfoPhase(3,control,controlPan,controlSource,0,false);
  // Bracket each telemetry experiment with the plaintext DC query already
  // observed working. A missing MAC ACK never suppresses the receive window.
  if(ok) ok=encryptedProbeInfoPhase(4,which,0xFFFF,target,0,false);
  if(ok) ok=encryptedProbeInfoPhase(5,which,0xFFFF,target,7,false);
  if(ok) ok=encryptedProbeInfoPhase(6,which,0xFFFF,target,0,false);
  if(ok) ok=encryptedProbeInfoPhase(7,which,0xFFFF,target,7,true);
  if(ok) ok=encryptedProbeInfoPhase(8,which,0xFFFF,target,0,false);
  if(ok) ok=encryptedProbeInfoPhase(9,which,zbOperationalPan,target,7,false);
  if(ok) ok=encryptedProbeInfoPhase(10,which,0xFFFF,target,0,false);
  if(ok) ok=encryptedProbeDiscovery(11,which);
  if(ok && control>=0) ok=encryptedProbeInfoPhase(12,control,controlPan,controlSource,0,false);
  // Keep the capture/routing guard alive through restoration and queue clearing.
  bool restored=apsUsePairingPan(false) && rawRadioSetPromiscuous(false);
  empty_serial2();
  portENTER_CRITICAL(&encryptedProbeMux);
  encryptedCapture.active=false; encryptedCapture.finished=ok;
  encryptedCapture.restored=restored; encryptedCapture.ended=millis();
  portEXIT_CRITICAL(&encryptedProbeMux);
  pairReceiveStop();
  consoleOut("read-only inverter tests finished; download encrypted test log before restarting");
  return ok && restored;
}

// Decode only the firmware-info shape; never update inverter settings/telemetry.
bool encryptedProbeDecode(const uint8_t uid[6], const uint8_t *data, size_t n,
                           char *version, size_t cap, uint8_t *model) {
  uint8_t plain[300]; size_t length=0;
  if(n>=6 && !memcmp(data,uid,6) && decodeInverterInfoL2(data+6,n-6,*model,version,cap)) return true;
  if(decodeInverterInfoL2(data,n,*model,version,cap)) return true;
  for(uint8_t skip: {uint8_t(0),uint8_t(6)}) {
    if(n<=skip || (skip && memcmp(data,uid,6))) continue;
    const uint8_t *tail=data+skip; size_t tailLen=n-skip;
    if(apsDecryptTail(uid,tail,tailLen,plain,sizeof(plain),&length) &&
       decodeInverterInfoL2(plain,length,*model,version,cap)) return true;
    if(tailLen>1 && (tail[0]==0xA0 || tail[0]==0xA1) &&
       apsDecryptTail(uid,tail+1,tailLen-1,plain,sizeof(plain),&length) &&
       decodeInverterInfoL2(plain,length,*model,version,cap)) return true;
  }
  return false;
}

String encryptedProbeSummary() {
  char line[200];
  portENTER_CRITICAL(&encryptedProbeMux);
  snprintf(line,sizeof(line),"\nENCRYPTED READ-ONLY TEST: started=%lu active=%u finished=%u restored=%u discovered=%04X conflict=%u\nDownload /diagnostics/encrypted-test for complete packets.\n",
    (unsigned long)encryptedCapture.started,encryptedCapture.active,encryptedCapture.finished,
    encryptedCapture.restored,encryptedCapture.discovered,encryptedCapture.conflict);
  portEXIT_CRITICAL(&encryptedProbeMux);
  return String(line);
}

// The download reads immutable completed capture storage, one line at a time.
// A reader lease prevents a new probe from clearing it during transmission.
static size_t encryptedProbeLogLine(size_t index, char *line, size_t cap) {
  if(index==0) return snprintf(line,cap,"Plaintext telemetry tests %s; RAM capture, lost on restart. No pairing or settings written.\n",VERSION);
  if(index==1) return snprintf(line,cap,"started=%lu ended=%lu finished=%u restored=%u discovered=%04X conflict=%u\n",
    (unsigned long)encryptedCapture.started,(unsigned long)encryptedCapture.ended,
    encryptedCapture.finished,encryptedCapture.restored,encryptedCapture.discovered,encryptedCapture.conflict);
  if(index==2) return snprintf(line,cap,"Modes: 0=plaintext DC firmware query, 6=discovery, 7=plaintext BB telemetry query. No AES or settings changes. TX error 3 means no MAC ACK, not proven non-delivery.\n");
  if(index==3) return snprintf(line,cap,"Phases: 0-2=bounded discovery attempts, 3=control DC, 4/6/8/10=target DC on FFFF, 5=target BB unicast FFFF, 7=target BB broadcast FFFF, 9=target BB unicast operating PAN, 11=discovery recheck, 12=control DC recheck. Two requests per entered phase. BB waits 6s per request; others 3.5s. Late replies may cross phases.\n");
  if(index==4) return snprintf(line,cap,"Raw retains first three/latest three; ASDU retains first/latest, including unknown telemetry formats. Times are milliseconds from start. TX contains ASDU only; discovery cluster=020C, DC/BB cluster=0006, profile=0F05, endpoints=14. Telemetry is captured only, never published.\n");
  index-=5; size_t phase=index/11, row=index%11;
  if(phase>=PROBE_PHASES) return 0;
  const auto &p=encryptedCapture.phases[phase];
  if(row==0) return snprintf(line,cap,"phase=%u entered=%u mode=%u broadcast=%u pan=%04X source=%04X start=%lu sent=%u rx=%lu related=%lu raw_omitted=%lu asdus=%lu asdu_omitted=%lu\n",
    (unsigned)phase,p.entered,p.mode,p.broadcast,p.pan,p.source,(unsigned long)p.started,p.sent,
    (unsigned long)p.rx,(unsigned long)p.related,(unsigned long)(p.related>6?p.related-6:0),
    (unsigned long)p.asdus,(unsigned long)(p.asdus>2?p.asdus-2:0));
  const uint8_t *data=nullptr; size_t length=0; int used=0;
  if(row<=2) {
    size_t k=row-1;
    if(k>=p.sent) return snprintf(line,cap,"  TX %u not-run\n",(unsigned)k);
    used=snprintf(line,cap,"  TX %u ms=%lu ok=%u error=%d bytes=%u data=",(unsigned)k,
      (unsigned long)p.txMs[k],p.txOk[k],p.txError[k],p.txLength[k]);
    data=p.tx[k]; length=p.txLength[k];
  } else if(row<=8) {
    size_t k=row-3;
    if(k>=p.related) return snprintf(line,cap,"  RX %u empty\n",(unsigned)k);
    const auto &s=p.packets[k];
    used=snprintf(line,cap,"  RX %u ms=%lu pan=%04X source=%04X rssi=%d lqi=%u bytes=%u raw=",(unsigned)k,
      (unsigned long)s.ms,s.pan,s.source,s.rssi,s.lqi,s.length);
    data=s.data; length=s.length;
  } else {
    size_t k=row-9;
    if(k>=p.asdus) return snprintf(line,cap,"  ASDU %u empty\n",(unsigned)k);
    const auto &a=p.answers[k]; char version[40]={}; uint8_t model=0;
    // Control responses use their clear UID; target probes use the requested UID.
    const uint8_t *uid=(phase==3 || phase==12) && a.length>=6 ? a.data : encryptedCapture.target;
    bool decoded=encryptedProbeDecode(uid,a.data,a.length,version,sizeof(version),&model);
    used=snprintf(line,cap,"  ASDU %u ms=%lu cluster=%04X info_decoded=%u model=%02X version=%s bytes=%u data=",(unsigned)k,
      (unsigned long)a.ms,a.cluster,decoded,model,decoded?version:"unknown",a.length);
    data=a.data; length=a.length;
  }
  for(size_t i=0;i<length && used+3<(int)cap;++i) used+=snprintf(line+used,cap-used,"%02X",data[i]);
  if(used+1<(int)cap) line[used++]='\n';
  line[used]=0; return used;
}

struct EncryptedProbeDownload {
  size_t index=0, used=0, offset=0;
  char line[900];
  ~EncryptedProbeDownload() {
    portENTER_CRITICAL(&encryptedProbeMux); --encryptedProbeReaders; portEXIT_CRITICAL(&encryptedProbeMux);
  }
};

void encryptedProbeDownload(AsyncWebServerRequest *request) {
  portENTER_CRITICAL(&encryptedProbeMux);
  bool busy=encryptedCapture.active || pendingPairInverter>=0;
  if(!busy) ++encryptedProbeReaders;
  portEXIT_CRITICAL(&encryptedProbeMux);
  if(busy) {request->send(409,"text/plain","Wait for the test to finish, then download again.");return;}
  auto cursor=std::make_shared<EncryptedProbeDownload>();
  auto *response=request->beginChunkedResponse("text/plain; charset=utf-8",
    [cursor](uint8_t *buffer,size_t room,size_t) -> size_t {
      size_t written=0;
      while(written<room) {
        if(cursor->offset==cursor->used) {
          cursor->used=encryptedProbeLogLine(cursor->index++,cursor->line,sizeof(cursor->line)); cursor->offset=0;
          if(!cursor->used) break;
        }
        size_t take=min(room-written,cursor->used-cursor->offset);
        memcpy(buffer+written,cursor->line+cursor->offset,take); cursor->offset+=take; written+=take;
      }
      return written;
    });
  response->addHeader("Content-Disposition","attachment; filename=aps-ecu-encrypted-test.txt");
  response->addHeader("Cache-Control","no-store"); request->send(response);
}
