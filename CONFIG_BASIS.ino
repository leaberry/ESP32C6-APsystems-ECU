void zendPageBasis(AsyncWebServerRequest *request) {
  String page = ecuPageStart(F("Polling and access"),
      F("Configure normal data collection and local web access."));
  page += F("<form class=\"form-card\" method=\"post\" action=\"/SETTINGS_SAVE\"><div class=\"form-grid\">");
  page += F("<div class=\"field full\"><label for=\"ecuid\">ECU identifier</label><input id=\"ecuid\" name=\"ecuid\" maxlength=\"12\" minlength=\"12\" pattern=\"[0-9A-Fa-f]{12}\" value=\"");
  page += webEscape(ECU_ID);
  page += F("\" required><span class=\"help\">A 12-digit hexadecimal coordinator identifier. Keep the generated value unless replacing an existing ECU.</span></div>");
  page += F("<div class=\"field full\"><label for=\"userpw\">Read-only user password</label><input id=\"userpw\" name=\"userpw\" type=\"password\" minlength=\"4\" maxlength=\"10\" placeholder=\"Leave blank to keep the current password\"><span class=\"help\">Used by pages that permit either the administrator or read-only user.</span></div>");
  page += F("<div class=\"field full\"><div class=\"checkline\"><input id=\"polling\" name=\"polling\" type=\"checkbox\"");
  if (Polling) page += F(" checked");
  page += F("><label for=\"polling\">Enable automatic polling<span class=\"help\">The recommended default is enabled. Manual Poll actions continue to work when disabled.</span></label></div></div>");
  page += F("<div class=\"field\"><label for=\"pollsec\">Fleet poll interval</label><input id=\"pollsec\" name=\"pollsec\" type=\"number\" min=\"5\" max=\"86400\" value=\"");
  page += String(pollIntervalSeconds);
  page += F("\" required><span class=\"help\">Seconds between complete fleet rounds. Default: 300 seconds.</span></div><div class=\"field\"><label>Safe minimum now</label><input value=\"");
  page += String(pollingMinimumSeconds());
  page += F(" seconds\" disabled><span class=\"help\">Three seconds per configured inverter, never below five seconds. Faster requests are automatically raised to this value.</span></div></div><div class=\"actions\"><button type=\"submit\">Save settings</button><a class=\"button secondary\" href=\"/MENU\">Cancel</a></div></form>");
  page += ecuPageEnd();
  request->send(200, "text/html", page);
}

void handleBasisSave(AsyncWebServerRequest *request) {
  if (!request->hasParam("ecuid", true) || !request->hasParam("pollsec", true)) {
    request->send(400, "text/plain", "Missing required settings");
    return;
  }
  String id = request->getParam("ecuid", true)->value();
  if (id.length() != 12) {
    request->send(400, "text/plain", "ECU identifier must contain 12 hexadecimal digits");
    return;
  }
  for (char c : id) if (!isxdigit((unsigned char)c)) {
    request->send(400, "text/plain", "ECU identifier must contain only hexadecimal digits");
    return;
  }
  id.toUpperCase();
  strlcpy(ECU_ID, id.c_str(), sizeof(ECU_ID));
  if (request->hasParam("userpw", true)) {
    String newPassword = request->getParam("userpw", true)->value();
    if (!newPassword.isEmpty()) {
      if (newPassword.length() < 4 || newPassword.length() > 10) {
        request->send(400, "text/plain", "User password must be 4 to 10 characters");
        return;
      }
      strlcpy(userPwd, newPassword.c_str(), sizeof(userPwd));
    }
  }
  pollIntervalSeconds = pollingClampSeconds(
      request->getParam("pollsec", true)->value().toInt());
  Polling = request->hasParam("polling", true);
  basisConfigsave();
  request->redirect("/BASISCONFIG");
}
