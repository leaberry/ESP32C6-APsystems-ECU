/*
 * changed the order of the handlers
*/
void start_server() {
if( diagNose != 0 ) consoleOut("starting server");
//server.addHandler(&ws);
server.addHandler(&events);

// Handle Web Server Events
events.onConnect([](AsyncEventSourceClient *client){
  if(client->lastId()){
    Serial.printf("Client reconnected! Last message ID that it got is: %u\n", client->lastId());
  }
});

// ***********************************************************************************
//                                     homepage
// ***********************************************************************************
server.on("/back", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!loginBoth(request, "both")) return;
    request->redirect( String(requestUrl) );
});

server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!loginBoth(request, "both")) return;
    request->send_P(200, "text/html", ECU_HOMEPAGE);
});

server.on("/stylesheet", HTTP_GET, [](AsyncWebServerRequest *request) {
   request->send_P(200, "text/css", STYLESHEET);
});

server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("favicon requested");
    AsyncWebServerResponse *response = request->beginResponse_P(200, "image/png", SUN_FAVICON, SUN_FAVICON_LEN);
    request->send(response);
});

server.on("/inverter-details", HTTP_GET, [](AsyncWebServerRequest *request) {
if (!loginBoth(request, "both")) return;
if (!request->hasParam("inv")) { request->send(400, "text/plain", "Missing inverter index"); return; }
iKeuze = request->getParam("inv")->value().toInt();
if (iKeuze < 0 || iKeuze >= inverterCount) { request->send(404, "text/plain", "Unknown inverter"); return; }
//requestUrl = request->url();
strlcpy(requestUrl, request->url().c_str(), sizeof(requestUrl));
//Serial.println("details url = " + String(requestUrl));
request->send_P(200, "text/html", DETAILSPAGE);
});
// ********************************************************************
// very often called  XHT REQUESTS handled by handleDataRequests()
//***********************************************************************
server.on("/api/data", HTTP_GET, [](AsyncWebServerRequest *request) {
  if (!loginBoth(request, "both")) return;
  strlcpy(requestUrl, request->url().c_str(), sizeof(requestUrl));
  Serial.println("get.Data url = " + String(requestUrl));
  handleDataRequests(request);
});



server.on("/menu", HTTP_GET, [](AsyncWebServerRequest *request) {
//Serial.println("requestUrl = " + request->url() ); // can we use this
if(checkRemote( request->client()->remoteIP().toString()) ) { request->redirect( "/denied" ); return; }
if (!loginBoth(request, "admin")) return;
//toSend = FPSTR(HTML_HEAD);
//toSend += FPSTR(MENUPAGE);
request->send_P(200, "text/html", MENUPAGE);
});
server.on("/security", HTTP_GET, [](AsyncWebServerRequest *request) {
   request->send_P(200, "text/css", SECURITY);
});
server.on("/denied", HTTP_GET, [](AsyncWebServerRequest *request) {
   request->send_P(200, "text/html", REQUEST_DENIED);
});


server.on("/console", HTTP_GET, [](AsyncWebServerRequest *request){
  if(checkRemote( request->client()->remoteIP().toString()) ) { request->redirect( "/denied" ); return; }
    if (!loginBoth(request, "admin")) return;
    diagNose=1;
    request->send_P(200, "text/html", CONSOLE_HTML);
  });

server.on("/diagnostics/download", HTTP_GET, [](AsyncWebServerRequest *request) {
  if (checkRemote(request->client()->remoteIP().toString())) { request->redirect("/denied"); return; }
  if (!request->authenticate("admin", pswd)) { request->requestAuthentication(); return; }
  AsyncWebServerResponse *response = request->beginResponse(200, "text/plain; charset=utf-8", diagnosticsReportText());
  response->addHeader("Content-Disposition", "attachment; filename=aps-ecu-diagnostics.txt");
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
});

server.on("/diagnostics/flight-recorder", HTTP_GET, [](AsyncWebServerRequest *request) {
  if (checkRemote(request->client()->remoteIP().toString())) { request->redirect("/denied"); return; }
  if (!request->authenticate("admin", pswd)) { request->requestAuthentication(); return; }
  AsyncWebServerResponse *response = request->beginResponse(
      200, "text/csv; charset=utf-8", flightRecorderReport(720));
  response->addHeader("Content-Disposition", "attachment; filename=aps-ecu-flight-recorder.csv");
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
});

server.on("/diagnostics/coredump", HTTP_GET, [](AsyncWebServerRequest *request) {
  if (checkRemote(request->client()->remoteIP().toString())) { request->redirect("/denied"); return; }
  if (!request->authenticate("admin", pswd)) { request->requestAuthentication(); return; }
  size_t address = 0, size = 0;
  esp_err_t result = esp_core_dump_image_get(&address, &size);
  if (result != ESP_OK || size == 0) {
    request->send(404, "text/plain", "No valid crash dump is stored");
    return;
  }
  AsyncWebServerResponse *response = request->beginResponse(
      "application/octet-stream", size,
      [address, size](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
        if (index >= size) return 0;
        size_t count = min(maxLen, size - index);
        return esp_flash_read(nullptr, buffer, address + index, count) == ESP_OK ? count : 0;
      });
  response->addHeader("Content-Disposition", "attachment; filename=aps-ecu-coredump.elf");
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
});

server.on("/fleet-name", HTTP_GET, [](AsyncWebServerRequest *request) {
  if (!loginBoth(request, "admin")) return;
  handleFleetNamePage(request);
});

server.on("/fleet-name/save", HTTP_POST, [](AsyncWebServerRequest *request) {
  if (!loginBoth(request, "admin")) return;
  handleFleetNameSave(request);
});

// Register the longer path first. ESPAsyncWebServer 3.12 can otherwise let
// the page route shadow /diagnostics/download on ESP32-C6.
server.on("/diagnostics", HTTP_GET, [](AsyncWebServerRequest *request) {
  if (checkRemote(request->client()->remoteIP().toString())) {
    request->redirect("/denied");
    return;
  }
  if (!request->authenticate("admin", pswd)) {
    request->requestAuthentication();
    return;
  }
  handleDiagnosticsPage(request);
});

// ***********************************************************************************
//                                   basisconfig
// ***********************************************************************************
server.on("/basicconfig", HTTP_GET, [](AsyncWebServerRequest *request) {
  if(checkRemote( request->client()->remoteIP().toString()) ) { request->redirect( "/denied" ); return; }
if (!loginBoth(request, "admin")) return;
//requestUrl = request->url();// remember this to come back after reboot
strlcpy(requestUrl, request->url().c_str(), sizeof(requestUrl));
zendPageBasis(request);
//request->send(200, "text/html", toSend);
});

server.on("/inverter/throttle", HTTP_GET, [](AsyncWebServerRequest *request) {
if (!loginBoth(request, "admin")) return;
if (!handleForms(request)) return;
confirm(); // puts a response in toSend
request->send(200, "text/html", toSend);
});

// server.on("/IPCONFIG", HTTP_GET, [](AsyncWebServerRequest *request) {
//   if(checkRemote( request->client()->remoteIP().toString()) ) request->redirect( "/DENIED" );
//   loginBoth(request, "admin");
//   zendPageIPconfig();
//   request->send(200, "text/html", toSend);
// });

// server.on("/IPconfig", HTTP_GET, [](AsyncWebServerRequest *request) {
//   handleIPconfig(request);
// });

// Register the MQTT child routes before /mqtt. ESPAsyncWebServer treats the
// parent path as a match for slash-delimited children when the HTTP method is
// the same, so the old order made both Save and Send test render this page.
server.on("/mqtt/save", HTTP_GET, [](AsyncWebServerRequest *request) {
  if (!loginBoth(request, "admin")) return;
  if (!handleForms(request)) return;
  request->redirect("/mqtt");
});

server.on("/mqtt/test", HTTP_GET, [](AsyncWebServerRequest *request) {
  if (!loginBoth(request, "admin")) return;
  if (Mqtt_Format == 0) {
    request->send(409, "text/plain", "MQTT is disabled. Save an enabled message format first.");
    return;
  }
  if (!Mqtt_outTopic[0]) {
    request->send(400, "text/plain", "MQTT publish topic is empty.");
    return;
  }

  char topic[40] = {0};
  strlcpy(topic, Mqtt_outTopic, sizeof(topic));
  size_t topicLength = strlen(topic);
  if (topicLength && topic[topicLength - 1] == '/' && inverterCount) {
    strlcat(topic, String(Inv_Prop[0].invIdx).c_str(), sizeof(topic));
  }
  String payload = "{\"test\":\"" + String(topic) + "\"}";
  if (!mqttConnect()) {
    request->send(503, "text/plain", "MQTT broker connection failed.");
    return;
  }
  if (!MQTT_Client.publish(topic, payload.c_str(), true)) {
    request->send(502, "text/plain", "MQTT broker connected, but the test publish failed.");
    return;
  }
  request->send(200, "text/plain", "MQTT test published to " + String(topic) + ": " + payload);
});

server.on("/mqtt", HTTP_GET, [](AsyncWebServerRequest *request) {
  if(checkRemote( request->client()->remoteIP().toString()) ) { request->redirect( "/denied" ); return; }
  if (!loginBoth(request, "admin")) return;
  //requestUrl = request->url();
  strlcpy(requestUrl, request->url().c_str(), sizeof(requestUrl));
  zendPageMQTTconfig(request);
  //request->send(200, "text/html", toSend);
});

server.on("/time", HTTP_GET, [](AsyncWebServerRequest *request) {
  if(checkRemote( request->client()->remoteIP().toString()) ) { request->redirect( "/denied" ); return; }
  if (!loginBoth(request, "admin")) return;
  //requestUrl = request->url();
  strlcpy(requestUrl, request->url().c_str(), sizeof(requestUrl));
  zendPageGEOconfig(request);
  //request->send(200, "text/html", toSend);
});

// server.on("/POWERCONFIG", HTTP_GET, [](AsyncWebServerRequest *request) {
//   if(checkRemote( request->client()->remoteIP().toString()) ) request->redirect( "/DENIED" );
//   loginBoth(request, "admin");
//   //requestUrl = request->url();
//   strcpy( requestUrl, request->url().c_str() );
//   zendPagePowerconfig(request);
//   //request->send(200, "text/html", toSend);
// });

server.on("/settings/save", HTTP_POST, [](AsyncWebServerRequest *request) {
  if (!loginBoth(request, "admin")) return;
  handleBasisSave(request);
});

server.on("/time/save", HTTP_POST, [](AsyncWebServerRequest *request) {
  if (!loginBoth(request, "admin")) return;
  handleTimeSave(request);
});

server.on("/network", HTTP_GET, [](AsyncWebServerRequest *request) {
  if(checkRemote(request->client()->remoteIP().toString())) { request->redirect("/denied"); return; }
  if (!loginBoth(request, "admin")) return;
  handleNetworkPage(request);
});

server.on("/network/save", HTTP_POST, [](AsyncWebServerRequest *request) {
  if(checkRemote(request->client()->remoteIP().toString())) { request->redirect("/denied"); return; }
  if (!loginBoth(request, "admin")) return;
  handleNetworkSave(request);
});

// Register the GET child before /energy so it cannot be shadowed by the page.
server.on("/energy/backup", HTTP_GET, [](AsyncWebServerRequest *request) {
  if (!loginBoth(request, "admin")) return;
  energySendHistoryBackup(request);
});

server.on("/energy/restore", HTTP_POST,
  [](AsyncWebServerRequest *request) {
    if (!request->authenticate("admin", pswd)) return request->requestAuthentication();
    String message;
    if (!energyRestoreUploadFinish(message)) {
      request->send(400, "text/plain", message);
      return;
    }
    request->redirect("/energy?status=restored");
  },
  [](AsyncWebServerRequest *request, String, size_t index,
     uint8_t *data, size_t len, bool final) {
    if (!request->authenticate("admin", pswd)) return;
    if (index == 0) energyRestoreUploadBegin();
    energyRestoreUploadWrite(data, len, index + len);
    if (final) energyRestoreUploadClose();
  });

server.on("/energy/wipe", HTTP_POST, [](AsyncWebServerRequest *request) {
  if (!request->authenticate("admin", pswd)) return request->requestAuthentication();
  if (!request->hasParam("confirm", true) ||
      request->getParam("confirm", true)->value() != "WIPE") {
    request->send(400, "text/plain", "Type WIPE exactly to confirm permanent history deletion.");
    return;
  }
  String message;
  if (!energyWipeHistory(message)) {
    request->send(500, "text/plain", message);
    return;
  }
  request->redirect("/energy?status=wiped");
});

server.on("/energy", HTTP_GET, [](AsyncWebServerRequest *request) {
  if (!loginBoth(request, "both")) return;
  request->send_P(200, "text/html", ENERGY_PAGE);
});

server.on("/api/energy/hourly", HTTP_GET, [](AsyncWebServerRequest *request) {
  if (!loginBoth(request, "both")) return;
  int inverter = request->hasParam("inv") ? request->getParam("inv")->value().toInt() : -1;
  energySendHourlyJson(request, inverter);
});

server.on("/api/energy/days", HTTP_GET, [](AsyncWebServerRequest *request) {
  if (!loginBoth(request, "both")) return;
  uint16_t limit = request->hasParam("limit") ? request->getParam("limit")->value().toInt() : 90;
  energySendDailyHistoryJson(request, limit);
});

server.on("/api/energy/history.csv", HTTP_GET, [](AsyncWebServerRequest *request) {
  if (!loginBoth(request, "both")) return;
  energySendHistoryCsv(request);
});

server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request) {
  if (!loginBoth(request, "admin")) return;
  actionFlag = 10;
  confirm(); 
  strlcpy(requestUrl, "/", sizeof(requestUrl));
  request->send(200, "text/html", toSend);
});

server.on("/setup", HTTP_GET, [](AsyncWebServerRequest *request) {
  if(checkRemote( request->client()->remoteIP().toString()) ) { request->redirect( "/denied" ); return; }
  if (!loginBoth(request, "admin")) return;
  String toSend = F("<!DOCTYPE html><html><head><script type='text/javascript'>setTimeout(function(){ window.location.href='/back'; }, 5000 ); </script>");
  toSend += F("</head><body><center><h2>OK the accesspoint is started.</h2>Wait unil the led goes on.<br><br>Then go to the wifi-settings on your pc/phone/tablet and connect to ESP32-ECU");
  request->send ( 200, "text/html", toSend ); //zend bevestiging
  actionFlag = 11;
});

server.on("/system", HTTP_GET, [](AsyncWebServerRequest *request) {
Serial.println(F("/INFOPAGE requested"));
if (!loginBoth(request, "both")) return;
handleAbout(request);
});
server.on("/radio-test", HTTP_GET, [](AsyncWebServerRequest *request) {
  if (!loginBoth(request, "admin")) return;
  actionFlag = 44;
  request->send( 200, "text/html", "<center><br><br><h3>checking radio.. please wait a minute.<br>Then you can find the result in the journal.<br><br><a href=\'/journal\'>click here</a></h3>" );
});

server.on("/journal", HTTP_GET, [](AsyncWebServerRequest *request) {
  if (!loginBoth(request, "both")) return;
  //requestUrl = request->url();
  strlcpy(requestUrl, request->url().c_str(), sizeof(requestUrl));
  handleJournalPage(request);
});
  
// ********************************************************************
//                    inverters
// ******************************************************************

server.on("/inverter/script", HTTP_GET, [](AsyncWebServerRequest *request) {
   request->send_P(200, "application/javascript", INV_SCRIPT);
});

server.on("/inverters", HTTP_GET, [](AsyncWebServerRequest *request) {
  if(checkRemote( request->client()->remoteIP().toString()) ) { request->redirect( "/denied" ); return; }
  if (!loginBoth(request, "admin")) return;
  iKeuze=0;
  strlcpy(requestUrl, request->url().c_str(), sizeof(requestUrl));
  inverterForm(); // prepare the page part with the form
  request->send_P(200, "text/html", INVCONFIG_START, processor);
});

server.on("/inverter/save", HTTP_GET, [](AsyncWebServerRequest *request) {
if (!loginBoth(request, "admin")) return;
handleInverterconfig(request);
});

server.on("/inverter/pair", HTTP_GET, [](AsyncWebServerRequest *request) {
  if(checkRemote( request->client()->remoteIP().toString()) ) { request->redirect( "/denied" ); return; }
  if (!loginBoth(request, "admin")) return;
  //requestUrl = request->url();
  strlcpy(requestUrl, request->url().c_str(), sizeof(requestUrl));
  //DebugPrintln(F("pairing requested"));
  handlePair(request);
});

server.on("/inverter/delete", HTTP_GET, [](AsyncWebServerRequest *request) {
  if(checkRemote( request->client()->remoteIP().toString()) ) { request->redirect( "/denied" ); return; }
  if (!loginBoth(request, "admin")) return;
  handleInverterdel(request);
});

server.on("/inverter/select", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!loginBoth(request, "admin")) return;
    if (!request->hasParam("welke")) { request->send(400, "text/plain", "Missing inverter index"); return; }
    strlcpy(requestUrl, request->url().c_str(), sizeof(requestUrl));
    //bool nothing = false;
    int i = atoi(request->arg("welke").c_str()) ;
    if (i != 99 && (i < 0 || i >= inverterCount)) { request->send(404, "text/plain", "Unknown inverter"); return; }
    iKeuze = i;
    consoleOut("?INV iKeuze at enter = " + String(iKeuze));
    if( iKeuze == 99 ) {
        iKeuze = inverterCount; //indicate this is an adition
        inverterCount += 88;
        }
     String bestand = "/Inv_Prop" + String(iKeuze) + ".str";
     consoleOut("iKeuze = " + String(iKeuze));
     if (!SPIFFS.exists(bestand)) Inv_Prop[iKeuze].invType = 2;
     inverterForm(); // prepare the form page
     request->send_P(200, "text/html", INVCONFIG_START, processor); //send the html code to the client
});

//server.on("/CONFIRM_INV", HTTP_GET, [](AsyncWebServerRequest *request) {
//    toSend = FPSTR(CONFIRM_INV); // prepare the page
//    request->send(200, "text/html", toSend); //send the html code to the client
//});
// ********************************************************************
//                    X H T  R E Q U E S T S
//***********************************************************************

server.on("/pair/status", HTTP_GET, [](AsyncWebServerRequest *request) {
if (!loginBoth(request, "both")) return;
if (iKeuze < 0 || iKeuze >= inverterCount) { request->send(404, "application/json", "{\"error\":\"unknown inverter\"}"); return; }
// set the array into a json object
  String json="{";
  json += "\"invID\":\"" + String(Inv_Prop[iKeuze].invID) + "\"";
  json += "}";
  request->send(200, "text/json", json);
  json = String();
});

// ***************************************************************************************
//                           Simple Firmware Update
// ***************************************************************************************
  server.on("/grid-profile", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!request->authenticate("admin", pswd)) return request->requestAuthentication();
    gridProfilePage(request);
  });

  server.on("/grid-profile/action", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!request->authenticate("admin", pswd)) return request->requestAuthentication();
    if (!request->hasParam("inv", true) || !request->hasParam("op", true)) {
      request->send(400, "text/plain", "missing inverter or operation"); return;
    }
    int target = request->getParam("inv", true)->value().toInt();
    if (target < 0 || target >= inverterCount) {
      request->send(400, "text/plain", "invalid inverter"); return;
    }
    gridProfileTarget = target;
    String op = request->getParam("op", true)->value();
    if (op == "apply") actionFlag = 70;
    else if (op == "read") actionFlag = 71;
    else if (op == "restore") actionFlag = 72;
    else { request->send(400, "text/plain", "invalid operation"); return; }
    request->redirect("/grid-profile");
  });

  server.on("/grid-profile/upload", HTTP_POST,
    [](AsyncWebServerRequest *request){
      if (!request->authenticate("admin", pswd)) return request->requestAuthentication();
      if (gridProfileUploadFile) gridProfileUploadFile.close();
      if (!gridProfileValidateFile()) {
        if (SPIFFS.exists(GRID_PROFILE_FILE)) SPIFFS.remove(GRID_PROFILE_FILE);
        request->send(400, "text/plain", "profile must use invdriver.gridprofile/v1 and contain points"); return;
      }
      gridSetStatus(F("Profile uploaded. Read the current settings before applying."));
      request->redirect("/grid-profile");
    },
    [](AsyncWebServerRequest *request, String filename, size_t index,
       uint8_t *data, size_t len, bool final){
      if (!request->authenticate("admin", pswd)) return;
      if (index == 0) {
        if (SPIFFS.exists(GRID_PROFILE_FILE)) SPIFFS.remove(GRID_PROFILE_FILE);
        gridProfileUploadFile = SPIFFS.open(GRID_PROFILE_FILE, "w");
      }
      if (index + len > 32768) {
        if (gridProfileUploadFile) gridProfileUploadFile.close();
        SPIFFS.remove(GRID_PROFILE_FILE);
        return;
      }
      if (gridProfileUploadFile && len) gridProfileUploadFile.write(data, len);
      if (final && gridProfileUploadFile) gridProfileUploadFile.close();
    });

  server.on("/firmware", HTTP_GET, [](AsyncWebServerRequest *request){
    if(checkRemote( request->client()->remoteIP().toString()) ) { request->redirect( "/denied" ); return; }
    strlcpy(requestUrl, "/", sizeof(requestUrl));
    if (!request->authenticate("admin", pswd) ) return request->requestAuthentication();
    if (esp_ota_get_next_update_partition(nullptr) == nullptr) {
      request->send(409, "text/html", "<h2>OTA is disabled in the 4 MB USB-only build.</h2><p>Connect USB to install new firmware.</p><a href='/menu'>Back</a>");
      return;
    }
    request->send_P(200, "text/html", otaIndex); 
    });
  server.on("/firmware/upload", HTTP_POST, [](AsyncWebServerRequest *request){
    if(checkRemote( request->client()->remoteIP().toString()) ) { request->redirect( "/denied" ); return; }
    if (!request->authenticate("admin", pswd)) return request->requestAuthentication();
    Serial.println("FWUPDATE requested");
    if( !Update.hasError() ) {
    toSend="<br><br><center><h2>UPDATE SUCCESS !!</h2><br><br>";
    toSend +="click here to reboot<br><br><a href='/reboot'><input style='font-size:3vw;' type='submit' value='REBOOT'></a>";
    } else {
    toSend="<br><br><center><kop>update failed<br><br>";
    toSend +="click here to go back <a href='/firmware'>BACK</a></center>";
    }
    AsyncWebServerResponse *response = request->beginResponse(200, "text/html", toSend);
    response->addHeader("Connection", "close");
    request->send(response);
  
  },[](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
    if (!request->authenticate("admin", pswd)) return;
    if (esp_ota_get_next_update_partition(nullptr) == nullptr) return;
    //Serial.println("filename = " + filename);
    if(filename != "") {
    if(!index){
      //#ifdef DEBUG
        Serial.printf("start firmware update: %s\n", filename.c_str());
      //#endif
      //Update.runAsync(true);
      if(!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000)){
        //#ifdef DEBUG
          Update.printError(Serial);
        //#endif
      }
    }
    } else {
      if( diagNose != 0 ) consoleOut("filename empty, aborting");
//     Update.hasError()=true;
    }
    if(!Update.hasError()){
      if(Update.write(data, len) != len){
          Serial.println("update failed with error: " );
          Update.printError(Serial);
      }
    }
    if(final){
      if(Update.end(true)){
        Serial.printf("firmware Update Success: %uB\n", index+len);
      } else {
        Update.printError(Serial);
      }
    }
  });


// if everything failed we come here
server.onNotFound([](AsyncWebServerRequest *request){
  //Serial.println("unknown request");
  handleNotFound(request);
});

server.begin(); 
}

void confirm() {
  String destination = String(requestUrl);
  if (!destination.startsWith("/") || destination.indexOf("//") >= 0) destination = "/";
  toSend = F("<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>Applying changes · APsystems ECU</title><link rel=\"stylesheet\" href=\"/stylesheet\"></head><body><main class=\"page\"><section class=\"card\"><span class=\"badge\">Saved</span><h1>Applying your changes</h1><p>The ECU will return automatically in a moment.</p></section></main><script>setTimeout(()=>location.href='");
  toSend += destination;
  toSend += F("',1800)</script></body></html>");
}

double round2(double value) {
   return (int)(value * 100 + 0.5) / 100.0;
}
double round1(double value) {
   return (int)(value * 10 + 0.5) / 10.0;
}
