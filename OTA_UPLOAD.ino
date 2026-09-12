// _tempObject is freed by ESPAsyncWebServer when the request disconnects.
// Keep it POD, and hold the global Update writer for the lifetime of one request.
struct EcuOtaUpload {
  int status;
  bool started;
  bool finished;
  char message[192];
};
AsyncWebServerRequest *ecuOtaOwner = nullptr;

void otaUploadComplete(AsyncWebServerRequest *request) {
  if (!settingsAuthorized(request)) return;
  auto state = static_cast<EcuOtaUpload *>(request->_tempObject);
  if (!state) { request->send(400,"text/plain","No firmware image received"); return; }
  // Select the new boot image only after the entire HTTP request is complete.
  if (ecuOtaOwner==request && state->started && state->finished && state->status==500) {
    if (Update.end(true)) {
      state->status=200;
      strlcpy(state->message,"UPDATE SUCCESS. Restart when ready.",sizeof(state->message));
    }
  }
  request->send(state->status, "text/plain", state->message);
}

void otaUploadChunk(AsyncWebServerRequest *request, String filename,
                    size_t index, uint8_t *data, size_t len, bool final) {
  if (checkRemote(request->client()->remoteIP().toString()) ||
      !request->authenticate("admin", pswd)) return;
  auto state = static_cast<EcuOtaUpload *>(request->_tempObject);
  if (!state) {
    state = static_cast<EcuOtaUpload *>(calloc(1,sizeof(EcuOtaUpload)));
    if (!state) return;
    request->_tempObject = state;
    state->status=400;
    strlcpy(state->message,"Invalid or incomplete firmware upload",sizeof(state->message));
    if (index || filename.isEmpty()) return;
    if (!esp_ota_get_next_update_partition(nullptr)) {
      state->status=409; strlcpy(state->message,"OTA unavailable on this partition layout",sizeof(state->message)); return;
    }
    if (ecuOtaOwner) {
      state->status=409; strlcpy(state->message,"Another firmware upload is active",sizeof(state->message)); return;
    }
    ecuOtaOwner=request;
    request->onDisconnect([request]() {
      if (ecuOtaOwner==request) {
        if (Update.isRunning()) Update.abort();
        ecuOtaOwner=nullptr;
      }
    });
    // Query parameters are available before the first multipart file chunk.
    bool saveToday=!request->hasParam("saveToday") || request->getParam("saveToday")->value()!="0";
    if (saveToday) {
      bool hasEnergy=false;
      for (uint8_t i=0;i<YC600_MAX_NUMBER_OF_INVERTERS;++i) hasEnergy=hasEnergy || energyTodayWh[i]>0;
      String message;
      // Zero production needs no checkpoint and must not prevent an update.
      if (hasEnergy && !energySaveTodayCheckpoint(message)) {
        state->status=500; strlcpy(state->message,message.c_str(),sizeof(state->message)); return;
      }
    }
    state->status=500;
    strlcpy(state->message,"Firmware write failed; do not restart to install it",sizeof(state->message));
    state->started=Update.begin((ESP.getFreeSketchSpace()-0x1000)&0xFFFFF000);
  } else if (!index) {
    // A multipart request must contain exactly one image.
    if (ecuOtaOwner==request && Update.isRunning()) Update.abort();
    state->started=false; state->status=400;
    strlcpy(state->message,"Send exactly one firmware image",sizeof(state->message)); return;
  }
  if (!state->started || state->finished || ecuOtaOwner!=request) return;
  if (Update.write(data,len)!=len) { Update.abort(); state->started=false; return; }
  if (final) state->finished=true;
}
