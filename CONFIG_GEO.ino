void zendPageGEOconfig(AsyncWebServerRequest *request) {
  String page = ecuPageStart(F("Time and location"),
      F("Location and local time determine the optional daylight polling window and daily energy boundaries."));
  page += F("<div class=\"alert info\">Latitude is positive north and negative south. Longitude is positive east and negative west. Example: Denver is approximately 39.739, -104.990.</div><form class=\"form-card section\" method=\"post\" action=\"/TIME_SAVE\"><div class=\"form-grid\">");
  page += F("<div class=\"field\"><label for=\"lat\">Latitude</label><input id=\"lat\" name=\"lat\" type=\"number\" min=\"-90\" max=\"90\" step=\"0.0001\" value=\"");
  page += String(lati, 4);
  page += F("\" required><span class=\"help\">Range -90 to 90 degrees.</span></div><div class=\"field\"><label for=\"lon\">Longitude</label><input id=\"lon\" name=\"lon\" type=\"number\" min=\"-180\" max=\"180\" step=\"0.0001\" value=\"");
  page += String(longi, 4);
  page += F("\" required><span class=\"help\">Range -180 to 180 degrees.</span></div><div class=\"field full\"><label for=\"zone\">Time zone</label><select id=\"zone\" name=\"zone\" onchange=\"customZone()\">");
  page += ecuTimeZoneOptionsHtml();
  page += F("</select><span class=\"help\">Regional choices include their normal daylight-saving rules; no separate DST checkbox is needed.</span></div><div class=\"field full\" id=\"customField\"><label for=\"offset\">Custom UTC offset in minutes</label><input id=\"offset\" name=\"offset\" type=\"number\" min=\"-720\" max=\"840\" value=\"");
  page += String(atoi(gmtOffset));
  page += F("\"><span class=\"help\">Examples: UTC-7 is -420, UTC+1 is 60, and UTC+5:30 is 330. A custom offset does not change for daylight saving.</span></div><div class=\"field full\"><div class=\"checkline\"><input id=\"daylight\" name=\"daylight\" type=\"checkbox\"");
  if (daylightPolling) page += F(" checked");
  page += F("><label for=\"daylight\">Use daylight-aware polling<span class=\"help\">Pauses automatic inverter requests outside the calculated sunrise-to-sunset window. If time retrieval or location is invalid, the ECU safely falls back to 24-hour polling.</span></label></div></div><div class=\"field\"><label for=\"offsetSun\">Sunrise/sunset margin</label><input id=\"offsetSun\" name=\"offsetSun\" type=\"number\" min=\"-15\" max=\"15\" value=\"");
  page += String(pollOffset);
  page += F("\"><span class=\"help\">Positive values shorten polling after sunrise and before sunset; negative values extend it. Range ±15 minutes.</span></div></div><div class=\"actions\"><button type=\"submit\">Save time settings</button><a class=\"button secondary\" href=\"/MENU\">Cancel</a></div></form><script>function customZone(){document.getElementById('customField').style.display=document.getElementById('zone').value==='Custom'?'flex':'none'}customZone()</script>");
  page += ecuPageEnd();
  request->send(200, "text/html", page);
}

void handleTimeSave(AsyncWebServerRequest *request) {
  if (!request->hasParam("lat", true) || !request->hasParam("lon", true) ||
      !request->hasParam("zone", true)) {
    request->send(400, "text/plain", "Missing required time or location setting");
    return;
  }
  float submittedLat = request->getParam("lat", true)->value().toFloat();
  float submittedLon = request->getParam("lon", true)->value().toFloat();
  String submittedZone = request->getParam("zone", true)->value();
  if (!isfinite(submittedLat) || !isfinite(submittedLon) ||
      submittedLat < -90 || submittedLat > 90 ||
      submittedLon < -180 || submittedLon > 180 ||
      !ecuTimeZoneIsValid(submittedZone.c_str())) {
    request->send(400, "text/plain", "Invalid latitude, longitude, or time zone");
    return;
  }
  int submittedOffset = request->hasParam("offset", true)
      ? request->getParam("offset", true)->value().toInt() : 0;
  if (submittedZone == "Custom" &&
      (submittedOffset < -720 || submittedOffset > 840)) {
    request->send(400, "text/plain", "Custom UTC offset must be -720 to 840 minutes");
    return;
  }
  lati = submittedLat;
  longi = submittedLon;
  locationConfigured = true;
  strlcpy(timeZoneId, submittedZone.c_str(), sizeof(timeZoneId));
  snprintf(gmtOffset, sizeof(gmtOffset), "%d", submittedOffset);
  daylightPolling = request->hasParam("daylight", true);
  pollOffset = request->hasParam("offsetSun", true)
      ? constrain(request->getParam("offsetSun", true)->value().toInt(), -15, 15) : 0;
  zomerTijd = false; // legacy field retained only for old configuration migration
  wifiConfigsave();
  basisConfigsave();
  actionFlag = 25;
  request->redirect("/GEOCONFIG");
}
