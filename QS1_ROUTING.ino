// One QS1 handshake with cross-PAN route discovery, followed by filtered reads.
bool pairQs1Routed(int which) {
  uint8_t uid[6],ecu[6]; char text[13];
  if (which<0 || which>=inverterCount || Inv_Prop[which].invType!=1 ||
      !pairSerialBytes(Inv_Prop[which].invSerial,uid)) return false;
  ECU_REVERSE().toCharArray(text,sizeof(text));
  if (!apsSerialToBcd(text,ecu) || !zbOperationalPan || zbOperationalPan==0xFFFF ||
      !qs1CaptureBegin(Inv_Prop[which].invSerial,zbOperationalPan)) return false;
  bool ok=pairReceiveBegin(Inv_Prop[which].invSerial,zbOperationalPan);
  empty_serial2(); pairTransportStop();
  ok=ok && radioTraceBegin() && rawQs1Reception(zbOperationalPan,true) &&
     qs1RoutingBegin(uid,ecu,zbOperationalPan);
  pairAuditStep(PA_RADIO,ok,1,zbOperationalPan);
  const uint16_t clusters[]={0x020D,0x020C,0x010F,0x0101};
  const char *stages[]={"route-bootstrap","route-discovery","route-assign","route-commit"};
  for (unsigned step=0;step<4 && ok;++step) {
    uint8_t data[17]={}; size_t n=(step==0 || step==2)?17:6;
    memcpy(data,step==3?ecu:uid,6);
    if (n==17) {
      uint16_t pan=step==0?0xFFFF:zbOperationalPan;
      data[6]=pan>>8;data[7]=pan;data[8]=0x10;data[9]=data[10]=0xFF;
      memcpy(data+11,ecu,6);
    }
    qs1CaptureStage(stages[step],zbOperationalPan);
    pairTraceStage(stages[step],zbOperationalPan,0);
    uint32_t at=millis();
    ok=apsSendQs1Pairing(zbOperationalPan,0xFFFF,clusters[step],data,n,true);
    pairAuditStep(PA_COMMAND,ok,step,zbOperationalPan);
    qs1CaptureResult(ok?0:-1,qs1RoutingSource(),nullptr,0);
    if (ok && millis()-at<2150) delay(2150-(millis()-at));
  }
  // Unfiltered discovery never confirms pairing. Restore automatic MAC ACKs
  // before asking for status and power on the assigned network.
  if (ok) {
    qs1CaptureStage("route-settle",zbOperationalPan);
    ok=rawQs1Reception(zbOperationalPan,false);
    if (ok) delay(10000);
    qs1CaptureResult(ok?0:-1,qs1RoutingSource(),nullptr,0);
  }
  char id[5]={}; uint16_t pan=0,source=0;
  bool verified=false;
  if (ok) {
    pairReceiveVerify(); qs1CaptureStage("route-status",zbOperationalPan);
    pairTraceStage("route-status",zbOperationalPan,0);
    for (unsigned attempt=0;attempt<2 && ok;++attempt) {
      ok=apsSendQs1Pairing(zbOperationalPan,zbOperationalPan,0x020C,uid,6,false);
      pairAuditStep(PA_VERIFY_QUERY,ok,attempt,zbOperationalPan);
      if (ok) delay(3500);
    }
    verified=pairReceiveFinish(id,&pan,&source);
    qs1CaptureResult(verified?0:-2,source,nullptr,0);
    // A route request supplies a candidate address, never a saved peer. Require
    // two serial-matched valid power replies if no direct status was verified.
    uint16_t candidate=qs1RoutingSource();
    if (!verified && candidate) {
      pairTransportBegin(uid,zbOperationalPan);
      pairTransportStage(zbOperationalPan,candidate,false);
      unsigned valid=0;
      for (unsigned attempt=0;attempt<3 && valid<2;++attempt) {
        qs1CaptureStage("route-power-verify",zbOperationalPan);
        pairTraceStage("route-power-verify",zbOperationalPan,candidate);
        pairTransportWaitForReply();
        bool sent=apsSendQs1Read(zbOperationalPan,candidate,ecu,0xBB,false);
        delay(3500);
        PairPowerReply reply;
        bool received=pairTransportTakeReply(reply);
        bool accepted=received && pairPowerCheck(uid,reply,1,false)==PP_VALID;
        qs1CaptureResult(accepted?0:sent?-2:-1,candidate,received?reply.data:nullptr,received?reply.length:0);
        if (accepted) ++valid;
      }
      verified=valid>=2 && qs1RoutingSource()==candidate;
      if (verified) {source=candidate;pan=zbOperationalPan;snprintf(id,sizeof(id),"%02X%02X",source&255,source>>8);}
    }
  }
  pairTransportStop(); qs1RoutingEnd(); radioTraceEnd();
  qs1CaptureStage("restore",zbOperationalPan);
  bool restored=rawQs1Reception(zbOperationalPan,false);
  qs1CaptureResult(restored?0:-1,0,nullptr,0);
  pairAuditStep(PA_RESTORE,restored,0,zbOperationalPan);
  bool saved=ok && verified && restored && saveVerifiedPairing(which,id,pan,source);
  if (ok && verified && restored) pairAuditStep(PA_STORAGE,saved,0,pan);
  pairTraceResult(saved,apsStoredTransportMode(Inv_Prop[which].transportMode,Inv_Prop[which].transportTag),restored);
  pairReceiveStop(); empty_serial2();
  pairAuditStep(PA_DONE,saved,0,pan);
  qs1CaptureEnd(saved,restored);
  consoleOut(saved?"QS1 pairing verified and saved":"QS1 pairing failed; download QS1 test bundle");
  if (saved) {sendNO();checkCoordinator();}
  return saved;
}
