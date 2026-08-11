const char MQTTCONFIG[] PROGMEM = R"=====(
<body>
<div id='msect'>
<div id='menu'>
    <a href="#" class='close' onclick='cl();'>&times;</a>
    <a href="#" id="sub" style='background:green; display: none' onclick='submitFunction()'>save</a><br>
</div>
<kop>MOSQUITTO CONFIGURATION</kop>
  <div class='divstijl'><center>
  <form id='formulier' method='get' action='submitform' oninput='showSubmit();'><table>
  <tr><td style='width:50px;'>format:&nbsp<td><select name='fm' class='sb1' id='sel'>
    <option value='0' fm_0>disabled</option>
    <option value='1' fm_1>format 1</option>
    <option value='2' fm_2>format 2</option>
    <option value='3' fm_3>format 3</option>
    <option value='4' fm_4>format 4</option>
    <option value='5' fm_5>format 5</option>    
    </select>
  <tr><td >address<td><input class='inp6' name='mqtAdres' value='{mqttAdres}' size='31' placeholder='broker adres'></tr>
  <tr><td >port<td><input class='inp2' name='mqtPort' value='{mqttPort}' size='31' placeholder='mqtt port'></tr>
  <tr><td>state idx:&nbsp<td><input class='inp2' name='mqidx' value='{idx}' size='4' length='4'></tr>
  <tr><td>outtopic:&nbsp<td><input class='inp6' name='mqtoutTopic' value='{mqttoutTopic}' placeholder='transmit topic' length='60'></tr>
  <tr><td>intopic:&nbsp<td><input class='inp6' name='mqtinTopic' value='{mqttinTopic}' readonly placeholder='receive topic' length='60'></tr>
  <tr><td>username:&nbsp<td><input class='inp6' name='mqtUser' value='{mqtu}'></td></tr>
  <tr><td>password:&nbsp<td><input class='inp6' name='mqtPas' value='{mqtp}'></td></tr>
  <tr><td>client id:&nbsp<td><input class='inp6' name='mqtCi' value='{mqtc}' readonly></td></tr>
  </form>
  </td></table>
  </div><br>
</div>

</body></html>
)=====";
  //<li><a href='/mqtt/test' >test</a></ul>

void zendPageMQTTconfig(AsyncWebServerRequest *request) {
  String page = ecuPageStart(F("MQTT"), F("Publish inverter telemetry to an MQTT broker. Leave the format disabled when MQTT is not used."));
  page += F("<form class=\"form-card\" method=\"get\" action=\"/mqtt/save\"><div class=\"form-grid\"><div class=\"field\"><label for=\"fm\">Message format</label><select id=\"fm\" name=\"fm\">");
  const char *labels[] = {"Disabled", "Format 1", "Format 2", "Format 3", "Format 4", "Format 5"};
  for (uint8_t i = 0; i < 6; ++i) { page += F("<option value=\""); page += i; page += '"'; if (Mqtt_Format == i) page += F(" selected"); page += '>'; page += labels[i]; page += F("</option>"); }
  page += F("</select><span class=\"help\">Use the format expected by your existing subscriber. This is independent of Modbus/TCP.</span></div><div class=\"field\"><label for=\"broker\">Broker address</label><input id=\"broker\" name=\"mqtAdres\" maxlength=\"31\" value=\""); page += webEscape(Mqtt_Broker); page += F("\"></div><div class=\"field\"><label for=\"port\">Port</label><input id=\"port\" name=\"mqtPort\" inputmode=\"numeric\" maxlength=\"5\" value=\""); page += webEscape(Mqtt_Port); page += F("\"></div><div class=\"field\"><label for=\"idx\">State device ID</label><input id=\"idx\" name=\"mqidx\" type=\"number\" min=\"0\" max=\"65535\" value=\""); page += Mqtt_stateIDX; page += F("\"><span class=\"help\">Used by formats that publish to a numeric automation-system device ID.</span></div><div class=\"field full\"><label for=\"out\">Publish topic</label><input id=\"out\" name=\"mqtoutTopic\" maxlength=\"60\" value=\""); page += webEscape(Mqtt_outTopic); page += F("\"></div><div class=\"field\"><label for=\"user\">Username</label><input id=\"user\" name=\"mqtUser\" maxlength=\"31\" value=\""); page += webEscape(Mqtt_Username); page += F("\"></div><div class=\"field\"><label for=\"password\">Password</label><input id=\"password\" name=\"mqtPas\" type=\"password\" maxlength=\"31\" placeholder=\"Leave blank to keep current password\"></div></div><div class=\"actions\"><button type=\"submit\">Save MQTT settings</button><a class=\"button secondary\" href=\"/menu\">Cancel</a><a class=\"button secondary\" href=\"/mqtt/test\">Send test</a></div></form>");
  page += ecuPageEnd();
  request->send(200, "text/html", page);
}

//void handleMQTTconfig(AsyncWebServerRequest *request) {
//  //collect serverarguments
//  strcpy( Mqtt_Broker  , request->getParam("mqtAdres")   ->value().c_str() );
//  strcpy( Mqtt_Port    , request->getParam("mqtPort")    ->value().c_str() );
//  strcpy( Mqtt_outTopic, request->getParam("mqtoutTopic")->value().c_str() );
//  strcpy( Mqtt_Username, request->getParam("mqtUser")    ->value().c_str() );
//  strcpy( Mqtt_Password, request->getParam("mqtPas")     ->value().c_str() );
//  strcpy( Mqtt_Clientid, request->getParam("mqtCi")     ->value().c_str() );  
//  Mqtt_stateIDX = request->arg("mqidx").toInt(); //values are 0 1 2
//  Mqtt_Format = request->arg("fm").toInt(); //values are 0 1 2 3 4 5
//
//  //DebugPrintln("saved mqttconfig");
//  mqttConfigsave();  // 
//  actionFlag=24; // reconnect with these settings
//  
//}
