// Issue 39 diagnostic suite. Application reads never publish telemetry or save peers.
static uint16_t qs1ReadPan = 0, qs1ReadSource = 0;

String qs1DiagnosticIdentity() {
  uint8_t address[8] = {};
  esp_ieee802154_get_extended_address(address);
  char line[240];
  snprintf(line, sizeof(line), "QS1 batch ECU_ID=%s radio_IEEE_wire=%02X%02X%02X%02X%02X%02X%02X%02X operating_pan=%04X old_pan_preset=A3D8 prior_esp_pan_preset=10C4\n",
    ECU_ID, address[0],address[1],address[2],address[3],address[4],address[5],address[6],address[7],zbOperationalPan);
  return String(line);
}

static bool qs1Discover(int which, const uint8_t uid[6], uint16_t pan, const char *stage, uint16_t &found) {
  qs1CaptureStage(stage, pan);
  if (!rawQs1Reception(pan, false)) { qs1CaptureResult(-1,0,nullptr,0); return false; }
  qs1CaptureSetProbe(false);
  pairReceiveBegin(Inv_Prop[which].invSerial, zbOperationalPan);
  pairTransportBegin(uid, zbOperationalPan);
  pairTransportStage(pan, 0, true);
  bool ok = apsSendQs1Pairing(pan, pan, 0x020C, uid, 6, false);
  if (ok) delay(3500);
  found = pairTransportFound(false);
  bool conflict = pairTransportConflict();
  pairTransportStop();
  qs1CaptureResult(!ok ? -1 : found && !conflict ? 0 : -2, found, nullptr, 0);
  if (found && !conflict) { qs1ReadPan = pan; qs1ReadSource = found; }
  return ok;
}

static bool qs1Reads(int which, const uint8_t uid[6], const uint8_t ecu[6], uint16_t pan, uint16_t source) {
  bool identified=pairValidAddress(source);
  // The old-controller identity is used only in read requests, never stored.
  static const uint8_t oldEcu[] = {0x80,0x97,0x1B,0x01,0xA3,0xD8};
  static const char *names[] = {"DC-current-short", "BB-current-short", "DC-old-short",
    "BB-old-short", "DC-current-native", "BB-current-native"};
  static const char *broadcastNames[] = {"broadcast-DC-current", "broadcast-BB-current", "broadcast-DC-old",
    "broadcast-BB-old", "broadcast-DC-native", "broadcast-BB-native"};
  for (uint8_t variant=0; variant<6; ++variant) {
    if (!identified) pan=variant==2 || variant==3 ? 0xA3D8 : zbOperationalPan;
    qs1CaptureStage(identified?names[variant]:broadcastNames[variant], pan);
    if (!rawQs1Reception(pan,false)) { qs1CaptureResult(-1,source,nullptr,0); return false; }
    qs1CaptureSetProbe(false);
    pairReceiveBegin(Inv_Prop[which].invSerial,zbOperationalPan);
    pairTransportBegin(uid,zbOperationalPan); pairTransportStage(pan,source,false);
    const uint8_t *identity = variant==2 || variant==3 ? oldEcu : ecu;
    uint8_t command = variant & 1 ? 0xBB : 0xDC;
    pairTransportWaitForReply();
    bool sent;
    if (!identified || variant<4) sent = apsSendQs1Read(pan,identified?source:0xFFFF,identity,command,variant>=4);
    else {
      uint8_t payload[19] = {}; memcpy(payload,identity,6);
      const uint8_t body[] = {0xFB,0xFB,6,command,0,0,0,0,0,0,uint8_t(6+command),0xFE,0xFE};
      memcpy(payload+6,body,sizeof(body)); int error = -1;
      sent = apsSendPairingQuery(pan,source,payload,sizeof(payload),&error);
    }
    // A missing MAC ACK can still be followed by an application reply.
    delay(6000);
    PairPowerReply reply;
    int result = sent ? identified ? -4 : -5 : -1;
    if (pairTransportTakeReply(reply)) {
      if (command==0xBB) result = int(pairPowerCheck(uid,reply,1,false));
      else {
        uint8_t model=0; char version[32]={};
        result = reply.length>6 && decodeInverterInfoL2(reply.data+6,reply.length-6,model,version,sizeof(version)) ? 0 : 1;
      }
      qs1CaptureResult(result,source,reply.data,reply.length);
    } else qs1CaptureResult(result,source,nullptr,0);
    pairTransportStop();
  }
  return true;
}

bool qs1BatchBefore(int which, const uint8_t uid[6], const uint8_t ecu[6], bool &already) {
  already=false; qs1ReadPan=qs1ReadSource=0;
  qs1CaptureSetProbe(true);
  qs1CaptureStage("quiet-baseline",zbOperationalPan);
  if (!rawQs1Reception(zbOperationalPan,true)) return false;
  delay(2000); qs1CaptureResult(0,0,nullptr,0);
  uint16_t previous=0, ignored=0;
  apsRadioLoadPeer(Inv_Prop[which].invSerial,&previous,&ignored);
  const uint16_t pans[]={zbOperationalPan,0xFFFF,0xA3D8,0x10C4,previous};
  for (uint8_t i=0;i<5;++i) {
    bool duplicate=!pans[i];
    for (uint8_t j=0;j<i;++j) duplicate=duplicate || pans[j]==pans[i];
    if (duplicate) continue;
    uint16_t found=0;
    if (!qs1Discover(which,uid,pans[i],"before-discovery",found)) return false;
    if (!i && found) already=true;
  }
  return qs1Reads(which,uid,ecu,qs1ReadPan,qs1ReadSource);
}

static bool qs1Verify(int which, const uint8_t uid[6], bool &verified, char id[5], uint16_t &pan, uint16_t &source) {
  qs1CaptureStage("batch-filtered-verify",zbOperationalPan);
  if (!rawQs1Reception(zbOperationalPan,false)) return false;
  qs1CaptureSetProbe(false);
  pairReceiveBegin(Inv_Prop[which].invSerial,zbOperationalPan); pairReceiveVerify();
  for (uint8_t i=0;i<2;++i) {
    if (!apsSendQs1Pairing(zbOperationalPan,zbOperationalPan,0x020C,uid,6,false)) return false;
    delay(3500);
  }
  verified=pairReceiveFinish(id,&pan,&source);
  qs1CaptureResult(verified?0:-2,verified?source:0,nullptr,0);
  return true;
}

static bool qs1Assignment(const uint8_t uid[6], const uint8_t ecu[6], uint16_t cluster,
                          uint16_t requested, const char *stage, bool repeats) {
  uint8_t data[17]={}; size_t n=cluster==0x0101?6:17;
  memcpy(data,cluster==0x0101?ecu:uid,6);
  if (n==17) { data[6]=requested>>8; data[7]=requested; data[8]=0x10;
    data[9]=data[10]=0xFF; memcpy(data+11,ecu,6); }
  qs1CaptureStage(stage,esp_ieee802154_get_panid());
  bool ok=apsSendQs1Pairing(zbOperationalPan,0xFFFF,cluster,data,n,repeats);
  qs1CaptureResult(ok?0:-1,0,nullptr,0); return ok;
}

bool qs1BatchAfter(int which, const uint8_t uid[6], const uint8_t ecu[6], bool &verified,
                    char id[5], uint16_t &pan, uint16_t &source) {
  if (!verified) {
    uint16_t found=0;
    if (!qs1Discover(which,uid,0xFFFF,"after-discovery-FFFF",found)) return false;
    if (!qs1Assignment(uid,ecu,0x010F,zbOperationalPan,"slow-prepare",false)) return false;
    delay(5000);
    for (uint8_t i=0;i<3;++i) {
      if (!qs1Assignment(uid,ecu,0x0101,0,"slow-commit",false)) return false;
      delay(1000);
    }
    delay(10000);
    if (!qs1Verify(which,uid,verified,id,pan,source)) return false;
  } else { qs1CaptureStage("slow-sequence-skipped-verified",pan); qs1CaptureResult(-3,source,nullptr,0); }
  if (!verified) {
    // Issue 25 probe5 obtained operating-network contact after this variation.
    if (!rawQs1Reception(0xFFFF,false) ||
        !qs1Assignment(uid,ecu,0x020D,zbOperationalPan,"directed-020D-operating",false)) return false;
    delay(5000);
    if (!qs1Verify(which,uid,verified,id,pan,source)) return false;
  } else { qs1CaptureStage("directed-020D-skipped-verified",pan); qs1CaptureResult(-3,source,nullptr,0); }
  if (!verified) {
    qs1CaptureSetProbe(true); pairTransportStop();
    if (!rawQs1Reception(zbOperationalPan,true)) return false;
    const uint16_t clusters[]={0x020D,0x020C,0x010F,0x0101};
    const char *names[]={"unfiltered-bootstrap","unfiltered-discovery","unfiltered-assign","unfiltered-commit"};
    for (uint8_t i=0;i<4;++i) {
      uint32_t at=millis();
      if (i==1) {
        qs1CaptureStage(names[i],zbOperationalPan);
        if (!apsSendQs1Pairing(zbOperationalPan,0xFFFF,clusters[i],uid,6,true)) return false;
        qs1CaptureResult(0,0,nullptr,0);
      } else if (!qs1Assignment(uid,ecu,clusters[i],i?zbOperationalPan:0xFFFF,names[i],true)) return false;
      uint32_t elapsed=millis()-at; if (elapsed<2150) delay(2150-elapsed);
    }
    qs1CaptureStage("unfiltered-settle",zbOperationalPan); delay(10000); qs1CaptureResult(0,0,nullptr,0);
    const uint16_t pans[]={0xFFFF,zbOperationalPan,0xA3D8,0x10C4};
    for (uint8_t i=0;i<4;++i) {
      bool duplicate=false; for(uint8_t j=0;j<i;++j) duplicate=duplicate || pans[j]==pans[i];
      if (duplicate) continue;
      qs1CaptureStage("unfiltered-query",pans[i]);
      if (!rawQs1Reception(pans[i],true) || !apsSendQs1Pairing(pans[i],pans[i],0x020C,uid,6,false)) return false;
      delay(3500); qs1CaptureResult(0,0,nullptr,0);
    }
    if (!qs1Verify(which,uid,verified,id,pan,source)) return false;
  } else { qs1CaptureStage("unfiltered-handshake-skipped-verified",pan); qs1CaptureResult(-3,source,nullptr,0); }
  // Rediscover before read requests so an earlier address is never assumed current.
  uint16_t readPan=verified?pan:qs1ReadPan, found=0;
  if (readPan && !qs1Discover(which,uid,readPan,"after-read-discovery",found)) return false;
  if (!qs1Reads(which,uid,ecu,readPan,found)) return false;
  if (!verified) {
    qs1CaptureSetProbe(true); qs1CaptureStage("receive-only-channel-scan",zbOperationalPan);
    if (!rawQs1Reception(zbOperationalPan,true)) return false;
    for (uint8_t channel=11;channel<=26;++channel) { if (!rawQs1Channel(channel)) return false; delay(500); }
    qs1CaptureResult(0,0,nullptr,0);
  } else { qs1CaptureStage("channel-scan-skipped-verified",pan); qs1CaptureResult(-3,source,nullptr,0); }
  return true;
}
