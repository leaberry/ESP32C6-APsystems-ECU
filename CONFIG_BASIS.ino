static bool accessPasswordIsValid(const String &password) {
  if (password.length() < 8 || password.length() > 32) return false;
  for (size_t i = 0; i < password.length(); ++i) {
    uint8_t character = (uint8_t)password[i];
    if (character < 33 || character > 126) return false;
  }
  return true;
}

void zendPageBasis(AsyncWebServerRequest *request) {
  String page = ecuPageStart(F("Polling and access"),
      F("Configure normal data collection and the two local web accounts."));
  page += F("<form class=\"form-card\" method=\"post\" action=\"/settings/save\"><div class=\"form-grid\">");
  page += F("<div class=\"field full\"><label for=\"ecuid\">ECU identifier</label><input id=\"ecuid\" name=\"ecuid\" maxlength=\"12\" minlength=\"12\" pattern=\"[0-9A-Fa-f]{12}\" value=\"");
  page += webEscape(ECU_ID);
  page += F("\" required><span class=\"help\">A 12-digit hexadecimal coordinator identifier. Keep the generated value unless replacing an existing ECU.</span></div>");
  page += F("<div class=\"field full\"><div class=\"alert\"><strong>Two account levels</strong><br>The <code>admin</code> account can change settings, control inverters, install firmware and manage history. The <code>user</code> account can only view dashboards, inverter details, energy history and data APIs. Use different passwords for the two accounts.</div></div>");
  page += F("<div class=\"field full\"><h2>Administrator account</h2><span class=\"help\">Changing this password signs out existing diagnostic WebSocket sessions. Your browser will ask you to sign in again as <code>admin</code>.</span></div>");
  page += F("<div class=\"field\"><label for=\"currentadminpw\">Current administrator password</label><input id=\"currentadminpw\" name=\"currentadminpw\" type=\"password\" maxlength=\"32\" autocomplete=\"current-password\" placeholder=\"Required only when changing it\"></div>");
  page += F("<div class=\"field\"><label for=\"adminpw\">New administrator password</label><input id=\"adminpw\" name=\"adminpw\" type=\"password\" minlength=\"8\" maxlength=\"32\" autocomplete=\"new-password\" placeholder=\"Leave blank to keep it\"><span class=\"help\">8 to 32 printable characters with no spaces.</span></div>");
  page += F("<div class=\"field\"><label for=\"adminpwconfirm\">Confirm new administrator password</label><input id=\"adminpwconfirm\" name=\"adminpwconfirm\" type=\"password\" maxlength=\"32\" autocomplete=\"new-password\"></div>");
  page += F("<div class=\"field full\"><h2>Read-only account</h2><span class=\"help\">Username <code>user</code>. Leave both fields blank to keep its current password.</span></div>");
  page += F("<div class=\"field\"><label for=\"userpw\">New read-only password</label><input id=\"userpw\" name=\"userpw\" type=\"password\" minlength=\"8\" maxlength=\"32\" autocomplete=\"new-password\" placeholder=\"Leave blank to keep it\"><span class=\"help\">8 to 32 printable characters with no spaces.</span></div>");
  page += F("<div class=\"field\"><label for=\"userpwconfirm\">Confirm new read-only password</label><input id=\"userpwconfirm\" name=\"userpwconfirm\" type=\"password\" maxlength=\"32\" autocomplete=\"new-password\"></div>");
  page += F("<div class=\"field full\"><div class=\"checkline\"><input id=\"polling\" name=\"polling\" type=\"checkbox\"");
  if (Polling) page += F(" checked");
  page += F("><label for=\"polling\">Enable automatic polling<span class=\"help\">The recommended default is enabled. Manual Poll actions continue to work when disabled.</span></label></div></div>");
  page += F("<div class=\"field\"><label for=\"pollsec\">Fleet poll interval</label><input id=\"pollsec\" name=\"pollsec\" type=\"number\" min=\"5\" max=\"86400\" value=\"");
  page += String(pollIntervalSeconds);
  page += F("\" required><span class=\"help\">Seconds between complete fleet rounds. Default: 300 seconds.</span></div><div class=\"field\"><label>Safe minimum now</label><input value=\"");
  page += String(pollingMinimumSeconds());
  page += F(" seconds\" disabled><span class=\"help\">Three seconds per configured inverter, never below five seconds. Faster requests are automatically raised to this value.</span></div></div><div class=\"actions\"><button type=\"submit\">Save settings</button><a class=\"button secondary\" href=\"/menu\">Cancel</a></div></form>");
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
  String currentAdminPassword = request->hasParam("currentadminpw", true) ?
      request->getParam("currentadminpw", true)->value() : String();
  String newAdminPassword = request->hasParam("adminpw", true) ?
      request->getParam("adminpw", true)->value() : String();
  String confirmedAdminPassword = request->hasParam("adminpwconfirm", true) ?
      request->getParam("adminpwconfirm", true)->value() : String();
  String newUserPassword = request->hasParam("userpw", true) ?
      request->getParam("userpw", true)->value() : String();
  String confirmedUserPassword = request->hasParam("userpwconfirm", true) ?
      request->getParam("userpwconfirm", true)->value() : String();

  if (!newAdminPassword.isEmpty()) {
    if (currentAdminPassword != String(pswd)) {
      request->send(403, "text/plain", "The current administrator password is incorrect");
      return;
    }
    if (!accessPasswordIsValid(newAdminPassword)) {
      request->send(400, "text/plain", "Administrator password must be 8 to 32 printable non-space characters");
      return;
    }
    if (newAdminPassword != confirmedAdminPassword) {
      request->send(400, "text/plain", "Administrator password confirmation does not match");
      return;
    }
  }
  if (!newUserPassword.isEmpty()) {
    if (!accessPasswordIsValid(newUserPassword)) {
      request->send(400, "text/plain", "Read-only password must be 8 to 32 printable non-space characters");
      return;
    }
    if (newUserPassword != confirmedUserPassword) {
      request->send(400, "text/plain", "Read-only password confirmation does not match");
      return;
    }
  }

  String effectiveAdminPassword = newAdminPassword.isEmpty() ? String(pswd) : newAdminPassword;
  String effectiveUserPassword = newUserPassword.isEmpty() ? String(userPwd) : newUserPassword;
  if (effectiveAdminPassword == effectiveUserPassword) {
    request->send(400, "text/plain", "Administrator and read-only passwords must be different");
    return;
  }

  id.toUpperCase();
  strlcpy(ECU_ID, id.c_str(), sizeof(ECU_ID));
  if (!newUserPassword.isEmpty()) strlcpy(userPwd, newUserPassword.c_str(), sizeof(userPwd));
  bool administratorChanged = !newAdminPassword.isEmpty();
  if (administratorChanged) strlcpy(pswd, newAdminPassword.c_str(), sizeof(pswd));
  pollIntervalSeconds = pollingClampSeconds(
      request->getParam("pollsec", true)->value().toInt());
  Polling = request->hasParam("polling", true);
  basisConfigsave();
  if (administratorChanged) {
    wifiConfigsave();
    ws.closeAll();
    ws.setAuthentication("admin", pswd, AsyncAuthType::AUTH_BASIC);
    String page = ecuPageStart(F("Administrator password changed"),
        F("The new administrator password is active and stored. Sign in again to continue."));
    page += F("<div class=\"alert\">Your browser may initially retry its cached old credentials. Use username <code>admin</code> and the new password when prompted.</div><div class=\"actions\"><a class=\"button\" href=\"/menu\">Sign in again</a></div>");
    page += ecuPageEnd();
    request->send(200, "text/html", page);
    return;
  }
  request->redirect("/basicconfig");
}
