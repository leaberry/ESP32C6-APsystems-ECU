/*
 * Grid-protection profile support.
 *
 * Accepts the open invdriver.gridprofile/v1 JSON files published by OpenAPS.
 * Only per-inverter, read-back-verifiable parameters are written. Broadcast
 * apply is deliberately absent. Unknown families/codes and parameters a
 * particular firmware does not return are skipped rather than guessed.
 */

const char GRID_PROFILE_FILE[] = "/grid-profile.json";
File gridProfileUploadFile;
uint8_t gridProfileTarget = 0;
static char gridProfileStatus[256] = "No profile operation has run.";

struct GridProtectionValue { char code[3]; float value; };
struct GridProtectionSnapshot {
  GridProtectionValue values[36];
  uint8_t count = 0;
};

static void gridSetStatus(const String &status) {
  status.substring(0, sizeof(gridProfileStatus) - 1).toCharArray(gridProfileStatus,
                                                                 sizeof(gridProfileStatus));
  consoleOut("grid profile: " + status);
}

static void gridAdd(GridProtectionSnapshot &s, const char *code, float value) {
  if (!isfinite(value) || value < 0 || value > 2000) return;
  for (uint8_t i = 0; i < s.count; ++i) if (!strcmp(s.values[i].code, code)) {
    s.values[i].value = value; return;
  }
  if (s.count >= sizeof(s.values) / sizeof(s.values[0])) return;
  strlcpy(s.values[s.count].code, code, sizeof(s.values[s.count].code));
  s.values[s.count++].value = value;
}

static bool gridGet(const GridProtectionSnapshot &s, const char *code, float &value) {
  for (uint8_t i = 0; i < s.count; ++i) if (!strcmp(s.values[i].code, code)) {
    value = s.values[i].value; return true;
  }
  return false;
}

static uint32_t gridReadBE(const uint8_t *f, size_t len, size_t at, uint8_t width) {
  if (at + width > len) return 0;
  uint32_t value = 0;
  for (uint8_t i = 0; i < width; ++i) value = (value << 8) | f[at + i];
  return value;
}

static size_t gridBuildL2(uint8_t cmd, const uint8_t *body, size_t bodyLen,
                          uint8_t *out, size_t cap) {
  size_t total = bodyLen + 8;
  if (!out || total > cap || bodyLen > 250) return 0;
  out[0] = 0xFB; out[1] = 0xFB; out[2] = (uint8_t)(bodyLen + 1); out[3] = cmd;
  if (bodyLen) memcpy(out + 4, body, bodyLen);
  uint16_t checksum = 0;
  for (size_t i = 2; i < 4 + bodyLen; ++i) checksum += out[i];
  out[4 + bodyLen] = checksum >> 8;
  out[5 + bodyLen] = checksum;
  out[6 + bodyLen] = 0xFE; out[7 + bodyLen] = 0xFE;
  return 8 + bodyLen;
}

static void gridAppendHex(char *out, size_t cap, const uint8_t *bytes, size_t len) {
  static const char hex[] = "0123456789ABCDEF";
  size_t used = strlen(out);
  for (size_t i = 0; i < len && used + 2 < cap; ++i) {
    out[used++] = hex[bytes[i] >> 4]; out[used++] = hex[bytes[i] & 15];
  }
  out[used] = 0;
}

static bool gridSendL2(uint8_t which, const uint8_t *l2, size_t l2Len) {
  if (which >= YC600_MAX_NUMBER_OF_INVERTERS || !l2 || !l2Len || l2Len > 80) return false;
  char command[220] = {}, ecuReverse[13] = {};
  ECU_REVERSE().toCharArray(ecuReverse, sizeof(ecuReverse));
  uint8_t asduLen = (uint8_t)(6 + l2Len);
  snprintf(command, sizeof(command), "2401%s1414060001000F13", Inv_Prop[which].invID);
  // ECU_ID is transmitted least-significant byte first, as in polling().
  strlcat(command, ecuReverse, sizeof(command));
  gridAppendHex(command, sizeof(command), l2, l2Len);
  // Replace the hard-coded 0x13 length in the legacy prefix.
  char lengthHex[3]; snprintf(lengthHex, sizeof(lengthHex), "%02X", asduLen);
  command[22] = lengthHex[0]; command[23] = lengthHex[1];
  sendZB(command);
  return true;
}

static bool gridExtractReceivedL2(char *received, uint8_t *out, size_t cap, size_t &outLen) {
  outLen = 0;
  char *begin = strstr(received, "FBFB");
  if (!begin) return false;
  char *end = strstr(begin + 4, "FEFE");
  if (!end) return false;
  size_t len = (size_t)(end + 4 - begin) / 2;
  if (len > cap) return false;
  for (size_t i = 0; i < len; ++i) {
    uint8_t b;
    if (!infoHexByte(begin + i * 2, b)) return false;
    out[i] = b;
  }
  outLen = len;
  return true;
}

static bool gridQueryPage(uint8_t which, uint8_t page, uint8_t *reply,
                          size_t replyCap, size_t &replyLen) {
  uint8_t body[5] = {}, query[16];
  size_t queryLen = gridBuildL2(page, body, sizeof(body), query, sizeof(query));
  char received[CC2530_MAX_SERIAL_BUFFER_SIZE] = {};
  empty_serial2();
  if (!gridSendL2(which, query, queryLen)) return false;
  readZB(received);
  return readCounter && gridExtractReceivedL2(received, reply, replyCap, replyLen);
}

static void gridDecodeDS3Page(const uint8_t *f, size_t len, GridProtectionSnapshot &s,
                              float lineHz) {
  if (len < 8 || f[3] != 0xDD) return;
  size_t b = 4;
  auto add = [&](const char *c, size_t off, uint8_t width, float scale, bool roundV=false) {
    uint32_t raw = gridReadBE(f, len - 2, b + off, width);
    float v = raw * scale;
    if (roundV) v = floorf(v + 0.5f);
    gridAdd(s, c, v);
  };
  if (f[4] == 0xDD) {
    add("AI",1,2,.268f,true); add("AH",3,2,.268f,true); add("AD",5,2,.268f,true);
    add("AQ",7,2,.268f,true); add("AY",9,2,.268f,true); add("AC",11,2,.268f,true);
    add("AK",17,2,.01f); add("AJ",19,2,.01f); add("AF",21,2,.01f); add("AE",23,2,.01f);
    add("BC",29,2,.01f); add("BB",31,2,.01f); add("BE",33,2,.01f); add("BD",35,2,.01f);
    add("BI",41,3,.01f); add("BH",44,3,.01f); add("BK",76,3,.01f); add("BJ",79,3,.01f);
    add("BQ",65,2,.01f); add("BP",67,2,.01f);
    add("AG",69,2,1.0f / lineHz);
  } else if (f[4] == 0xDE) {
    uint32_t mode = gridReadBE(f, len - 2, b + 5, 1);
    gridAdd(s, "CV", mode == 1 ? 13 : mode == 2 ? 14 : 15);
    add("CC",8,2,.01f); add("CB",10,2,.01f);
    add("DD",12,2,(6000.0f/2208898.0f)*100.0f);
    add("AB",49,2,.268f); add("DI",63,2,.01f); add("DH",65,2,.01f);
  }
}

static void gridDecodeQS1Page(const uint8_t *f, size_t len, GridProtectionSnapshot &s) {
  if (len < 8) return;
  size_t b = 3;
  auto raw = [&](size_t off, uint8_t width) { return gridReadBE(f, len - 2, b + off, width); };
  auto volt = [&](const char *c, size_t off) { uint32_t r=raw(off,2); if(r) gridAdd(s,c,floorf((r/1.332f)/4.0f+.5f)); };
  auto freq = [&](const char *c, size_t off) { uint32_t r=raw(off,3); if(r) gridAdd(s,c,50000000.0f/r); };
  auto sec = [&](const char *c, size_t off) { gridAdd(s,c,raw(off,2)/100.0f); };
  if (f[3] == 0xDB) {
    volt("AQ",5); volt("AD",7); volt("AC",9); volt("AY",11);
    freq("AJ",17); freq("AK",20); freq("AE",23); freq("AF",26);
    sec("BB",29); sec("BC",31); sec("BD",33); sec("BE",35);
    sec("BH",41); sec("BI",43); sec("BJ",45); sec("BK",47);
    sec("AG",49); volt("AH",53); volt("AI",55);
  } else if (f[3] == 0xDE) {
    volt("BN",1); volt("BO",3); freq("BP",5); freq("BQ",8);
    uint32_t avg=raw(25,3); if(avg) gridAdd(s,"AB",roundf((float)(avg/600)/1.3315f/4.0f));
    gridAdd(s,"DD",raw(69,2)*.169f); freq("CC",37); freq("CB",40);
  } else if (f[3] == 0xD9) {
    freq("DC",1); freq("DH",12); freq("DI",15);
  }
}

static uint8_t gridModelFor(uint8_t which) {
  if (Inv_Data[which].modelCode) return Inv_Data[which].modelCode;
  return Inv_Prop[which].invType == 2 ? 0x20 : Inv_Prop[which].invType == 1 ? 0x08 : 0x07;
}

static bool gridReadProtection(uint8_t which, GridProtectionSnapshot &snapshot) {
  snapshot.count = 0;
  uint8_t model = gridModelFor(which);
  bool ds3 = model == 0x20 || model == 0x21 || model == 0x22 || model == 0x36;
  bool qs1 = model == 0x08 || model == 0x18;
  if (!ds3 && !qs1) return false;
  float lineHz = Inv_Data[which].freq >= 55.0f ? 60.0f : 50.0f;
  for (uint8_t page : { (uint8_t)0xDD, (uint8_t)0xDE, (uint8_t)0xD9 }) {
    uint8_t reply[180] = {}; size_t replyLen = 0;
    if (gridQueryPage(which, page, reply, sizeof(reply), replyLen)) {
      if (ds3) gridDecodeDS3Page(reply, replyLen, snapshot, lineHz);
      else gridDecodeQS1Page(reply, replyLen, snapshot);
    }
    delay(200);
  }
  return snapshot.count > 0;
}

enum GridScale : uint8_t { GS_VOLT, GS_HZ, GS_SEC100, GS_SEC1, GS_RECOVERY, GS_SLOPE, GS_MODE };
struct GridDS3Map { const char *code; uint8_t sub; GridScale scale; };
static const GridDS3Map gridDS3Map[] = {
  {"CB",0x2B,GS_HZ},{"CC",0x2A,GS_HZ},{"DH",0x56,GS_HZ},{"DI",0x55,GS_HZ},
  {"DD",0x2D,GS_SLOPE},{"CG",0x2E,GS_SEC1},{"CV",0x28,GS_MODE},{"AG",0x25,GS_RECOVERY},
  {"AI",0x01,GS_VOLT},{"AH",0x02,GS_VOLT},{"AD",0x03,GS_VOLT},{"AQ",0x04,GS_VOLT},
  {"AY",0x05,GS_VOLT},{"AC",0x06,GS_VOLT},{"AK",0x09,GS_HZ},{"AJ",0x0A,GS_HZ},
  {"AF",0x0B,GS_HZ},{"AE",0x0C,GS_HZ},{"BC",0x0F,GS_SEC100},{"BB",0x10,GS_SEC100},
  {"BE",0x11,GS_SEC100},{"BD",0x12,GS_SEC100},{"BI",0x15,GS_SEC100},{"BH",0x16,GS_SEC100},
  {"BK",0x17,GS_SEC100},{"BJ",0x18,GS_SEC100},{"AB",0x42,GS_VOLT},{"BQ",0x23,GS_HZ},{"BP",0x24,GS_HZ}
};

static bool gridSafeValue(const char *code, float value) {
  if (!isfinite(value) || value <= 0) return false;
  if (!strcmp(code,"AI")||!strcmp(code,"AH")||!strcmp(code,"AD")||!strcmp(code,"AQ")||
      !strcmp(code,"AY")||!strcmp(code,"AC")||!strcmp(code,"AB")||!strcmp(code,"BN")||!strcmp(code,"BO"))
    return value >= 100 && value <= 600;
  if (!strcmp(code,"AK")||!strcmp(code,"AJ")||!strcmp(code,"AF")||!strcmp(code,"AE")||
      !strcmp(code,"BP")||!strcmp(code,"BQ")||!strcmp(code,"CB")||!strcmp(code,"CC")||
      !strcmp(code,"DH")||!strcmp(code,"DI")||!strcmp(code,"CA"))
    return value >= 40 && value <= 70;
  if (!strcmp(code,"CV")) return value >= 1 && value <= 15;
  if (!strcmp(code,"DD")) return value <= 200;
  return value <= 1000;
}

static uint32_t gridScaled(GridScale scale, float value, float lineHz) {
  switch (scale) {
    case GS_VOLT: return (uint32_t)(value / .268f);
    case GS_HZ: return (uint32_t)(value * 100.0f);
    case GS_SEC100: return (uint32_t)(value * 100.0f);
    case GS_SEC1: return (uint32_t)(value + .5f);
    case GS_RECOVERY: return (uint32_t)(value * lineHz);
    case GS_SLOPE: return (uint32_t)(value * 163.68f);
    case GS_MODE: return (int)value == 15 ? 0 : ((int)value == 3 || (int)value == 4 || (int)value == 13) ? 1 : 2;
  }
  return 0;
}

static bool gridEncodeDS3(const char *code, float value, float lineHz, uint8_t *l2, size_t &len) {
  for (const auto &m : gridDS3Map) if (!strcmp(code, m.code)) {
    uint32_t wire = gridScaled(m.scale, value, lineHz);
    if (wire > 0xFFFF) return false;
    uint8_t body[5] = {m.sub,0,0,(uint8_t)(wire>>8),(uint8_t)wire};
    len = gridBuildL2(0xAA, body, sizeof(body), l2, 20);
    return len > 0;
  }
  return false;
}

static bool gridEncodeQS1(const char *code, float value, uint8_t *l2, size_t &len) {
  uint8_t cmd=0, sub=0, width=2; bool newOne=false; uint32_t wire=0;
  struct Map { const char *code; uint8_t sub; };
  static const Map slowV[]={{"AQ",0x11},{"AD",0x12},{"AC",0x13},{"AY",0x14}};
  static const Map tripHz[]={{"AJ",0x58},{"AK",0x57},{"AE",0x5A},{"AF",0x59}};
  static const Map clear[]={{"BB",0x36},{"BC",0x38},{"BD",0x3A},{"BE",0x3C},
                            {"BH",0x3E},{"BI",0x40},{"BJ",0x42},{"BK",0x44}};
  static const Map curve[]={{"CA",0x62},{"CB",0x68},{"CC",0x65},{"DH",0xA1},{"DI",0xA4},
                            {"BP",0x51},{"BQ",0x4E}};
  if (!strcmp(code,"AI") || !strcmp(code,"AH")) {
    sub=!strcmp(code,"AI")?0x90:0x8E; wire=(uint32_t)(value*5.328f); newOne=true;
  } else if (!strcmp(code,"AB")) {
    sub=0x7D; wire=(uint32_t)(value*3196.8f); width=3; newOne=true;
  } else if (!strcmp(code,"AG")) {
    cmd=0x5D; wire=(uint32_t)(value*100); width=2;
  } else {
    for (const auto &m:slowV) if(!strcmp(code,m.code)){cmd=m.sub;wire=(uint32_t)(value*5.328f);}
    for (const auto &m:tripHz) if(!strcmp(code,m.code)){cmd=m.sub;wire=(uint32_t)(50000000.0f/value);width=3;}
    for (const auto &m:clear) if(!strcmp(code,m.code)){sub=m.sub;wire=(uint32_t)(value*100);newOne=true;}
    for (const auto &m:curve) if(!strcmp(code,m.code)){sub=m.sub;wire=(uint32_t)(50000000.0f/value);width=3;newOne=true;}
  }
  if (wire == 0 || wire > (width==3?0xFFFFFFUL:0xFFFFUL)) return false;
  uint8_t body[5] = {};
  if (newOne) {
    cmd=0x1C; body[0]=sub; body[1]=width;
    if(width==3){body[2]=wire>>16;body[3]=wire>>8;body[4]=wire;}
    else {body[2]=wire>>8;body[3]=wire;}
  } else {
    if(!cmd) return false;
    if(width==3){body[0]=wire>>16;body[1]=wire>>8;body[2]=wire;}
    else {body[0]=wire>>8;body[1]=wire;}
  }
  len=gridBuildL2(cmd,body,sizeof(body),l2,20);
  return len>0;
}

static bool gridEncode(uint8_t which, const char *code, float value, uint8_t *l2, size_t &len) {
  if (!gridSafeValue(code,value)) return false;
  uint8_t model=gridModelFor(which);
  if(model==0x20||model==0x21||model==0x22||model==0x36)
    return gridEncodeDS3(code,value,Inv_Data[which].freq>=55?60.0f:50.0f,l2,len);
  if(model==0x08||model==0x18) return gridEncodeQS1(code,value,l2,len);
  return false;
}

static float gridTolerance(const char *code) {
  if (!strcmp(code,"CV")) return .01f;
  if (!strcmp(code,"DD")) return .25f;
  if (!strcmp(code,"AI")||!strcmp(code,"AH")||!strcmp(code,"AD")||!strcmp(code,"AQ")||
      !strcmp(code,"AY")||!strcmp(code,"AC")||!strcmp(code,"AB")) return 1.1f;
  if (!strcmp(code,"AK")||!strcmp(code,"AJ")||!strcmp(code,"AF")||!strcmp(code,"AE")||
      !strcmp(code,"BP")||!strcmp(code,"BQ")||!strcmp(code,"CB")||!strcmp(code,"CC")||
      !strcmp(code,"DH")||!strcmp(code,"DI")) return .03f;
  return .03f;
}

static bool gridSaveBackup(uint8_t which, const GridProtectionSnapshot &snapshot) {
  String path="/grid-backup-"+String(which)+".json";
  File file=SPIFFS.open(path,"w"); if(!file) return false;
  JsonDocument doc; doc["schema"]="esp32c6-ecu.gridbackup/v1"; doc["inverter"]=which;
  doc["serial"]=Inv_Prop[which].invSerial;
  for(uint8_t i=0;i<snapshot.count;++i) doc["values"][snapshot.values[i].code]=snapshot.values[i].value;
  bool ok=serializeJson(doc,file)>0; file.close(); return ok;
}

bool gridProfileValidateFile() {
  File file=SPIFFS.open(GRID_PROFILE_FILE,"r");
  if(!file) return false;
  JsonDocument doc; DeserializationError err=deserializeJson(doc,file); file.close();
  return !err && String((const char*)(doc["schema"]|""))=="invdriver.gridprofile/v1" &&
         doc["points"].is<JsonArray>() && doc["points"].size()>0;
}

static bool gridApplyValue(uint8_t which, const char *code, float value) {
  uint8_t l2[20]; size_t len=0;
  if(!gridEncode(which,code,value,l2,len)) return false;
  empty_serial2();
  if(!gridSendL2(which,l2,len)) return false;
  delay(180);
  empty_serial2();
  return true;
}

static bool gridApplyUploadedProfile(uint8_t which) {
  if(which>=inverterCount||!SPIFFS.exists(GRID_PROFILE_FILE)){gridSetStatus("No uploaded profile or invalid inverter.");return false;}
  File file=SPIFFS.open(GRID_PROFILE_FILE,"r"); JsonDocument doc;
  DeserializationError err=deserializeJson(doc,file); file.close();
  if(err||String((const char*)(doc["schema"]|""))!="invdriver.gridprofile/v1"){
    gridSetStatus("Profile JSON/schema is invalid."); return false;
  }
  if(!queryInverterInfo(which) && !Inv_Data[which].modelCode)
    consoleOut(F("grid profile: version query unavailable; using configured inverter family"));
  GridProtectionSnapshot before;
  if(!gridReadProtection(which,before)){gridSetStatus("Protection read-back unavailable; nothing was written.");return false;}
  if(!gridSaveBackup(which,before)){gridSetStatus("Could not save the pre-change backup; nothing was written.");return false;}

  uint8_t written=0, inSync=0, skipped=0;
  struct Intended { char code[3]; float value; } intended[36]; uint8_t intendedCount=0;
  for(JsonObject point:doc["points"].as<JsonArray>()){
    const char *code=point["apply"]["aps_code"]|""; float value=point["native"]["value"]|NAN;
    if(strlen(code)!=2||!isfinite(value)){++skipped;continue;}
    float minV=point["range"]["min"]|value,maxV=point["range"]["max"]|value;
    float current=0; uint8_t l2[20]; size_t l2Len=0;
    // Fail closed: only write codes this exact inverter returned and we can encode.
    if(value<minV||value>maxV||!gridSafeValue(code,value)||!gridGet(before,code,current)||
       !gridEncode(which,code,value,l2,l2Len)){++skipped;continue;}
    if(intendedCount<36){strlcpy(intended[intendedCount].code,code,3);intended[intendedCount++].value=value;}
    if(fabsf(current-value)<=gridTolerance(code)){++inSync;continue;}
    if(gridApplyValue(which,code,value)) ++written; else {gridSetStatus("A protection write failed; stopped.");return false;}
  }
  if(!written){gridSetStatus("Profile checked: "+String(inSync)+" values already matched, "+String(skipped)+" unsupported/unverifiable.");return inSync>0;}
  delay(1200);
  GridProtectionSnapshot after;
  if(!gridReadProtection(which,after)){gridSetStatus("Writes sent, but final read-back failed. Inspect the inverter before retrying.");return false;}
  uint8_t verified=0;
  for(uint8_t i=0;i<intendedCount;++i){float got=0;if(gridGet(after,intended[i].code,got)&&fabsf(got-intended[i].value)<=gridTolerance(intended[i].code))++verified;}
  bool ok=verified==intendedCount;
  gridSetStatus(String(doc["id"]|"profile")+": wrote "+String(written)+", verified "+String(verified)+"/"+String(intendedCount)+", skipped "+String(skipped)+(ok?".":"; verification FAILED."));
  return ok;
}

static bool gridRestoreBackup(uint8_t which) {
  String path="/grid-backup-"+String(which)+".json"; File file=SPIFFS.open(path,"r");
  if(!file){gridSetStatus("No backup exists for this inverter.");return false;}
  JsonDocument doc; DeserializationError err=deserializeJson(doc,file); file.close();
  if(err||String((const char*)(doc["serial"]|""))!=Inv_Prop[which].invSerial){gridSetStatus("Backup is invalid or belongs to another inverter.");return false;}
  GridProtectionSnapshot current;if(!gridReadProtection(which,current)){gridSetStatus("Cannot read current protection values; restore cancelled.");return false;}
  uint8_t written=0,skipped=0; GridProtectionSnapshot intended;
  for(JsonPair kv:doc["values"].as<JsonObject>()){
    const char *code=kv.key().c_str();float value=kv.value().as<float>(),cur=0;
    if(!gridGet(current,code,cur)){++skipped;continue;}
    gridAdd(intended,code,value);
    if(fabsf(cur-value)<=gridTolerance(code))continue;
    if(gridApplyValue(which,code,value))++written;else ++skipped;
  }
  delay(1200);GridProtectionSnapshot after;bool read=gridReadProtection(which,after);
  uint8_t verified=0;
  if(read) for(uint8_t i=0;i<intended.count;++i){float got=0;if(gridGet(after,intended.values[i].code,got)&&fabsf(got-intended.values[i].value)<=gridTolerance(intended.values[i].code))++verified;}
  bool ok=read&&skipped==0&&verified==intended.count;
  gridSetStatus("Backup restore sent "+String(written)+", verified "+String(verified)+"/"+String(intended.count)+", skipped "+String(skipped)+(ok?".":"; verification FAILED."));
  return ok;
}

void gridProfileHandleAction(uint8_t action) {
  if(action==70) gridApplyUploadedProfile(gridProfileTarget);
  else if(action==71){GridProtectionSnapshot s;gridSetStatus(gridReadProtection(gridProfileTarget,s)?"Read "+String(s.count)+" protection values from inverter "+String(gridProfileTarget)+".":"Protection read failed or family unsupported.");}
  else if(action==72) gridRestoreBackup(gridProfileTarget);
}

void gridProfilePage(AsyncWebServerRequest *request) {
  String page=F("<!doctype html><html><meta name='viewport' content='width=device-width'><body><h2>Grid protection profiles</h2>"
    "<p><b>Warning:</b> these settings control mandatory voltage/frequency disconnection behavior. Use only a profile required by your utility and inverter model.</p>"
    "<p>This accepts OpenAPS <code>invdriver.gridprofile/v1</code> JSON. Writes are per-inverter and only occur for parameters that can be read back first.</p><p>Status: ");
  page+=gridProfileStatus; page+=F("</p><form method='post' action='/GRIDPROFILE_UPLOAD' enctype='multipart/form-data'><input type='file' name='profile' accept='.json,application/json' required><button>Upload profile</button></form><hr><form method='post' action='/GRIDPROFILE_ACTION'><label>Inverter <select name='inv'>");
  for(int i=0;i<inverterCount;++i){page+="<option value='"+String(i)+"'>"+String(i)+" — "+String(Inv_Prop[i].invSerial)+"</option>";}
  page+=F("</select></label><button name='op' value='read'>Read current profile</button><button name='op' value='apply' onclick=\"return confirm('Apply this protection profile? Incorrect values may disconnect the inverter or violate grid rules.')\">Apply uploaded profile</button><button name='op' value='restore' onclick=\"return confirm('Restore the pre-change backup?')\">Restore backup</button></form><p><a href='/MENU'>Back</a></p></body></html>");
  request->send(200,"text/html",page);
}
