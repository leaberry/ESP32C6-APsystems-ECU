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

// Build only fixed read-only DC/BB application requests.
bool encryptedProbePayload(int which, uint8_t mode, uint8_t *out, size_t cap, size_t *len) {
  uint8_t ecu[6], plain[19], cipher[48]; size_t plainLen=0, cipherLen=0;
  char serial[13]; ECU_REVERSE().toCharArray(serial,sizeof(serial));
  if ((mode>5 && mode!=7 && mode!=11 && mode!=12) || !apsSerialToBcd(serial,ecu) || !probePlainInfo(ecu,plain,sizeof(plain),&plainLen)) return false;
  // Exact read-only BB request used by normal polling; no caller-selected opcode.
  if (mode==7 || mode==11 || mode==12) { plain[9]=0xBB; plain[16]=0xC1; }
  if(mode==11) mode=1; else if(mode==12) mode=2;
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
    uint8_t payload[32]; size_t len=0; int error=-1;
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
    delay((mode==7 || mode==11 || mode==12) ? 6000 : 3500);
  }
  return true;
}

static bool encryptedProbeDiscovery(uint8_t phase, int which, uint16_t pan) {
  encryptedProbeStage(phase,pan,0,6);
  if(!apsUseSpecificPan(pan,"pairing investigation discovery")) return false;
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

// Existing raw pairing payloads; 020D with the operating PAN is an explicit
// hypothesis for the original ECU's directed PAN-set operation, not a proven
// translation of its modem opcode. No arbitrary opcodes/channels are accepted.
static bool probeAssignment(uint8_t phase, int which, uint16_t source,
                            uint8_t mode, uint8_t repeats, bool settle) {
  if((mode<8 || (mode>10 && mode!=13)) || repeats<1 || repeats>2) return false;
  uint8_t ecu[6], uid[6], payload[17]; char serial[13], hex[35]={}, cmd[120];
  ECU_REVERSE().toCharArray(serial,sizeof(serial));
  if(!apsSerialToBcd(serial,ecu) || !pairSerialBytes(Inv_Prop[which].invSerial,uid)) return false;
  size_t n=mode==10?6:17;
  if(mode==10) memcpy(payload,ecu,6);
  else {
    uint16_t requested=mode==13?0xFFFF:zbOperationalPan;
    memcpy(payload,uid,6); payload[6]=requested>>8; payload[7]=requested;
    payload[8]=0x10; payload[9]=payload[10]=0xFF; memcpy(payload+11,ecu,6);
  }
  for(size_t i=0;i<n;++i) snprintf(hex+2*i,sizeof(hex)-2*i,"%02X",payload[i]);
  const char *cluster=mode==8?"0F01":(mode==9 || mode==13)?"0D02":"0101";
  const unsigned sequence=mode==8?2:(mode==9 || mode==13)?0:3;
  snprintf(cmd,sizeof(cmd),"24020FFFFFFFFFFFFFFFFF14FFFF14%s%02X000F%02X00%s",cluster,sequence,(unsigned)n,hex);
  encryptedProbeStage(phase,0xFFFF,source,mode);
  portENTER_CRITICAL(&encryptedProbeMux);
  encryptedCapture.phases[phase].broadcast=true;
  portEXIT_CRITICAL(&encryptedProbeMux);
  if(!apsUseSpecificPan(0xFFFF,"pairing investigation command")) return false;
  for(uint8_t i=0;i<repeats;++i) {
    uint32_t at=millis(); bool sent=sendZB(cmd);
    portENTER_CRITICAL(&encryptedProbeMux);
    auto &p=encryptedCapture.phases[phase]; p.sent=i+1; p.txOk[i]=sent;
    p.txError[i]=sent?0:-1; p.txMs[i]=at-encryptedCapture.started;
    p.txLength[i]=n; memcpy(p.tx[i],payload,n);
    portEXIT_CRITICAL(&encryptedProbeMux);
    if(!sent) return false;
    delay(mode==10?1000:5000);
  }
  if(settle) delay(10000);
  return true;
}

static uint16_t probeFound(uint8_t phase) {
  portENTER_CRITICAL(&encryptedProbeMux);
  uint16_t found=encryptedCapture.conflict?0:encryptedCapture.phases[phase].foundSource;
  portEXIT_CRITICAL(&encryptedProbeMux);
  return found;
}
static bool probeOperatingSeen() {
  portENTER_CRITICAL(&encryptedProbeMux);
  bool seen=encryptedCapture.operatingSource!=0 || encryptedCapture.conflict;
  portEXIT_CRITICAL(&encryptedProbeMux);
  return seen;
}

bool encryptedProbeRun(int which) {
  uint8_t uid[6];
  if(which<0 || which>=inverterCount || !flightRecorderEnabled ||
     !apsSerialDefaultsToEncrypted(Inv_Prop[which].invSerial) ||
     strcmp(Inv_Prop[which].invID,"0000") ||
     !pairSerialBytes(Inv_Prop[which].invSerial,uid) || encryptedProbeBusy()) return false;
  // Reuse the normal operation owner to pause polling and prevent route learning.
  if(!pairReceiveBegin(Inv_Prop[which].invSerial,zbOperationalPan)) return false;
  empty_serial2();
  portENTER_CRITICAL(&encryptedProbeMux);
  encryptedCapture.begin(uid,millis());
  portEXIT_CRITICAL(&encryptedProbeMux);
  bool ok=rawRadioSetPromiscuous(false);
  uint16_t target=0; bool conflict=false;
  int control=-1; uint16_t controlPan=0,controlSource=0;
  for(int i=0;i<inverterCount;++i) {
    if(i!=which && !apsInverterUsesEncryption(i) && strcmp(Inv_Prop[i].invID,"0000") &&
       apsRadioLoadPeer(Inv_Prop[i].invSerial,&controlPan,&controlSource) &&
       controlPan && controlPan!=0xFFFF && pairValidAddress(controlSource)) {control=i;break;}
  }
  if(ok && control>=0) ok=encryptedProbeInfoPhase(3,control,controlPan,controlSource,0,false);
  // Establish radio evidence even when the target is absent. Never gate the
  // working-inverter control or operating-network discovery on FFFF replies.
  if(ok) ok=encryptedProbeDiscovery(0,which,0xFFFF);
  if(ok && !probeOperatingSeen()) ok=encryptedProbeDiscovery(9,which,zbOperationalPan);
  target=probeFound(0);
  if(ok && !target && !probeOperatingSeen()) {
    // Normal pairing's first serial-addressed command requests FFFF. It needs
    // no guessed short address and is separately captured from discovery.
    ok=probeAssignment(1,which,0,13,1,false);
    if(ok) ok=encryptedProbeDiscovery(2,which,0xFFFF);
    target=probeFound(2);
    // A device may already be on the operating network despite a missed query.
    if(ok && !target && !probeOperatingSeen()) ok=encryptedProbeDiscovery(37,which,zbOperationalPan);
  }
  portENTER_CRITICAL(&encryptedProbeMux);
  conflict=encryptedCapture.conflict;
  bool operatingInitially=encryptedCapture.operatingSource!=0;
  portEXIT_CRITICAL(&encryptedProbeMux);
  ok=ok && !conflict && (pairValidAddress(target) || operatingInitially);
  // Do not send discovery-network requests to an already-operating inverter.
  if(ok && !operatingInitially) {
    ok=encryptedProbeInfoPhase(4,which,0xFFFF,target,0,false);
    if(ok) ok=encryptedProbeInfoPhase(5,which,0xFFFF,target,7,false);
    if(ok) ok=encryptedProbeInfoPhase(6,which,0xFFFF,target,0,false);
    if(ok) ok=encryptedProbeInfoPhase(7,which,0xFFFF,target,7,true);
    if(ok) ok=encryptedProbeInfoPhase(8,which,0xFFFF,target,0,false);
  }
  if(ok && !probeOperatingSeen()) {
    ok=probeAssignment(10,which,target,8,1,false); // established prepare candidate
    if(ok) ok=encryptedProbeDiscovery(11,which,0xFFFF);
    if(probeFound(11)) target=probeFound(11);
    if(ok) ok=encryptedProbeDiscovery(12,which,zbOperationalPan);
    if(ok && !probeOperatingSeen()) {
      if(probeFound(11)) ok=encryptedProbeInfoPhase(13,which,0xFFFF,target,0,false);
      // First commit plus two repeats; preserve the ten-second settle window.
      if(ok) ok=probeAssignment(14,which,target,10,1,false);
      if(ok) ok=probeAssignment(15,which,target,10,2,true);
      if(ok) ok=encryptedProbeDiscovery(16,which,zbOperationalPan);
    }
    if(ok && !probeOperatingSeen()) {
      ok=encryptedProbeDiscovery(17,which,0xFFFF);
      target=probeFound(17);
      // A silent target cannot authorize another speculative assignment.
      if(!target) ok=false;
      if(ok) ok=encryptedProbeInfoPhase(18,which,0xFFFF,target,0,false);
      if(ok) ok=encryptedProbeInfoPhase(19,which,0xFFFF,target,7,false);
      if(ok) ok=probeAssignment(20,which,target,9,1,false);
      if(ok) ok=encryptedProbeDiscovery(21,which,zbOperationalPan);
      if(ok && !probeOperatingSeen()) {
        ok=encryptedProbeDiscovery(22,which,0xFFFF);
        target=probeFound(22);
        if(!target) ok=false;
        if(ok) ok=encryptedProbeInfoPhase(23,which,0xFFFF,target,0,false);
        if(ok) ok=encryptedProbeInfoPhase(24,which,0xFFFF,target,7,false);
        if(ok) ok=probeAssignment(25,which,target,10,1,false);
        if(ok) ok=probeAssignment(26,which,target,10,2,true);
        if(ok) ok=encryptedProbeDiscovery(27,which,zbOperationalPan);
        if(ok && !probeOperatingSeen()) {
          ok=encryptedProbeDiscovery(28,which,0xFFFF);
          target=probeFound(28);
          if(!target) ok=false;
          if(ok) ok=encryptedProbeInfoPhase(29,which,0xFFFF,target,0,false);
          if(ok) ok=encryptedProbeInfoPhase(30,which,0xFFFF,target,7,false);
        }
      }
    }
  }
  portENTER_CRITICAL(&encryptedProbeMux);
  uint16_t operating=encryptedCapture.operatingSource;
  conflict=encryptedCapture.conflict;
  portEXIT_CRITICAL(&encryptedProbeMux);
  ok=ok && !conflict;
  // Network migration is not proof of usable encrypted telemetry. Once seen,
  // stop all writes and test plaintext plus the native/A1 encrypted candidates.
  if(ok && operating) {
    ok=encryptedProbeInfoPhase(31,which,zbOperationalPan,operating,0,false);
    if(ok) ok=encryptedProbeInfoPhase(32,which,zbOperationalPan,operating,7,false);
    if(ok) ok=encryptedProbeInfoPhase(34,which,zbOperationalPan,operating,1,false);
    if(ok) ok=encryptedProbeInfoPhase(35,which,zbOperationalPan,operating,11,false);
    if(ok) ok=encryptedProbeInfoPhase(36,which,zbOperationalPan,operating,12,false);
  }
  // Run the final control even after a missing target, conflicting replies or
  // command error. Keep the earlier failure visible in the final status.
  bool finalControl=true;
  if(control>=0) finalControl=encryptedProbeInfoPhase(33,control,controlPan,controlSource,0,false);
  ok=ok && finalControl;
  // Keep the capture/routing guard alive through restoration and queue clearing.
  bool restored=apsUsePairingPan(false) && rawRadioSetPromiscuous(false);
  empty_serial2();
  portENTER_CRITICAL(&encryptedProbeMux);
  encryptedCapture.active=false; encryptedCapture.finished=ok;
  encryptedCapture.restored=restored; encryptedCapture.ended=millis();
  portEXIT_CRITICAL(&encryptedProbeMux);
  pairReceiveStop();
  consoleOut("pairing investigation finished; download encrypted test log before restarting");
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
  snprintf(line,sizeof(line),"\nPAIRING INVESTIGATION: started=%lu active=%u finished=%u restored=%u discovered=%04X operating_source=%04X conflict=%u\nDownload /diagnostics/encrypted-test for complete packets.\n",
    (unsigned long)encryptedCapture.started,encryptedCapture.active,encryptedCapture.finished,
    encryptedCapture.restored,encryptedCapture.discovered,encryptedCapture.operatingSource,encryptedCapture.conflict);
  portEXIT_CRITICAL(&encryptedProbeMux);
  return String(line);
}

// The download reads immutable completed capture storage, one line at a time.
// A reader lease prevents a new probe from clearing it during transmission.
static size_t encryptedProbeLogLine(size_t index, char *line, size_t cap) {
  if(index==0) return snprintf(line,cap,"Pairing investigation %s; RAM capture, lost on restart. Target network may change; no local pairing/settings saved.\n",VERSION);
  if(index==1) return snprintf(line,cap,"started=%lu ended=%lu finished=%u restored=%u discovered=%04X operating_source=%04X conflict=%u\n",
    (unsigned long)encryptedCapture.started,(unsigned long)encryptedCapture.ended,
    encryptedCapture.finished,encryptedCapture.restored,encryptedCapture.discovered,encryptedCapture.operatingSource,encryptedCapture.conflict);
  if(index==2) return snprintf(line,cap,"Modes: 0=DC plain, 1=DC native AES, 6=discovery, 7=BB plain, 8=prepare 010F, 9=directed PAN 020D, 10=commit 0101, 11=BB native AES, 12=BB UID/A1 AES, 13=bootstrap 020D to FFFF. TX error 3=no MAC ACK, not proven non-delivery.\n");
  if(index==3) return snprintf(line,cap,"Phases: 3/33=before/after control DC; 0=initial FFFF discovery; 9=initial operating discovery; 1=bootstrap FFFF; 2=post-bootstrap FFFF discovery; 37=post-bootstrap operating discovery; 4/6/8=baseline DC; 5/7=baseline BB direct/broadcast; 10=prepare; 11/12=FFFF/operating discovery; 13=DC; 14/15=commit/repeats; 16/17=operating/FFFF discovery; 18/19=DC/BB; 20=directed PAN; 21/22=operating/FFFF discovery; 23/24=DC/BB; 25/26=commit/repeats; 27/28=operating/FFFF discovery; 29/30=DC/BB; 31/32/34/35/36=operating DC/BB/native DC/native BB/A1 BB. Assignment stops on operating discovery; phases may be skipped.\n");
  if(index==4) return snprintf(line,cap,"Raw retains first two/latest two; ASDU first/latest. Times are milliseconds from start; use timestamps, samples are not sorted. TX is ASDU only; mode 6 cluster=020C, modes 0/1/7/11/12 cluster=0006; profile=0F05 endpoints=14. Commands are hypotheses, not proven modem translations. No telemetry is published.\n");
  index-=5; size_t phase=index/9, row=index%9;
  if(phase>=PROBE_PHASES) return 0;
  const auto &p=encryptedCapture.phases[phase];
  if(row==0) return snprintf(line,cap,"phase=%u entered=%u mode=%u broadcast=%u pan=%04X source=%04X found=%04X start=%lu sent=%u rx=%lu related=%lu raw_omitted=%lu asdus=%lu asdu_omitted=%lu\n",
    (unsigned)phase,p.entered,p.mode,p.broadcast,p.pan,p.source,p.foundSource,(unsigned long)p.started,p.sent,
    (unsigned long)p.rx,(unsigned long)p.related,(unsigned long)(p.related>4?p.related-4:0),
    (unsigned long)p.asdus,(unsigned long)(p.asdus>2?p.asdus-2:0));
  const uint8_t *data=nullptr; size_t length=0; int used=0;
  if(row<=2) {
    size_t k=row-1;
    if(k>=p.sent) return snprintf(line,cap,"  TX %u not-run\n",(unsigned)k);
    used=snprintf(line,cap,"  TX %u ms=%lu ok=%u error=%d bytes=%u data=",(unsigned)k,
      (unsigned long)p.txMs[k],p.txOk[k],p.txError[k],p.txLength[k]);
    data=p.tx[k]; length=p.txLength[k];
  } else if(row<=6) {
    size_t k=row-3;
    if(k>=p.related) return snprintf(line,cap,"  RX %u empty\n",(unsigned)k);
    const auto &s=p.packets[k];
    used=snprintf(line,cap,"  RX %u ms=%lu pan=%04X source=%04X rssi=%d lqi=%u bytes=%u raw=",(unsigned)k,
      (unsigned long)s.ms,s.pan,s.source,s.rssi,s.lqi,s.length);
    data=s.data; length=s.length;
  } else {
    size_t k=row-7;
    if(k>=p.asdus) return snprintf(line,cap,"  ASDU %u empty\n",(unsigned)k);
    const auto &a=p.answers[k]; char version[40]={}; uint8_t model=0;
    // Control responses use their clear UID; target probes use the requested UID.
    const uint8_t *uid=(phase==3 || phase==33) && a.length>=6 ? a.data : encryptedCapture.target;
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
