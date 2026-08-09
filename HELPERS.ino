// flash the led
void ledblink(int i, int wacht) {
  for(int x=0; x<i; x++) {
    digitalWrite(led_onb, LED_AAN);
    delay(wacht);
    digitalWrite(led_onb, LED_UIT);
    delay(wacht);
   }
}


void empty_serial() { // remove any remaining data in serial buffer
  while(Serial.available()) {
    Serial.read();
  }
}
void delayedReset() {
  clearStoredWifiCredentials();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  SPIFFS.end();       // Recommended before formatting
  SPIFFS.format();
  ESP.restart();
}

void flashErase(AsyncWebServerRequest *request) {
// call this from the portal to erase the wifi and the flash
     confirm();
     request->send(200, "text/html", toSend);
     resetTicker.once(3.0, delayedReset); // 1-second delay
}

void eventSend(byte what) {
  if (what == 1) {
      events.send( "general", "message"); //getGeneral triggered            
  } else 
     if (what == 2) {
     events.send( "getall", "message"); //getAll triggered
  } else {  
     events.send( "reload", "message"); // both triggered
  }
}

int readInverterfiles() {
  // we must try to read from 0 to 8 because in some situations there can be a gap
  int count =0;
  for (int x=0; x < 9; x++) 
  {
  String bestand = "/Inv_Prop" + String(x) + ".str";  
  //consoleOut("reading file " + bestand);
      if (!leesStruct(bestand)) 
      { 
        // if the file not exists we break
        consoleOut("no file " + bestand); 
        //break;
      } else { count += 1; }
  }
  return count;
}

    void test_actionFlag() {
    if(actionFlag == 0) return;
    //if(actionFlag != 0) Serial.println("test_actionFlag 1 val = " + String(actionFlag));  
    // ******************  reset the nework data and reboot in AP *************************
    if (actionFlag == 11 || value == 11)
    {
     delay(1000); //reserve time to release the button
     clearStoredWifiCredentials();
     WiFi.disconnect(true, true);
     consoleOut(F("Wi-Fi configuration erased"));
     ESP.restart();
    }  

    if (actionFlag == 10) { // the button was pressed a long time, start ap
     delay(2000); // give the server the time to send the confirm
     consoleOut("rebooting");
     write_eeprom();
     ESP.restart();
  }
    
    //Serial.println("test_actionFlag 2 val = " + String(actionFlag));
    // interrrupt button test
    if (actionFlag == 15) {
      actionFlag = 0;
      buttonPressed();
    }
     //Serial.println("test_actionFlag 3 val = " + String(actionFlag));
    if (actionFlag == 60) {
      actionFlag = 0; //reset the actionflag
      pairOnActionflag();
    }
    if (actionFlag >= 70 && actionFlag <= 72) {
      uint8_t gridAction = actionFlag;
      actionFlag = 0;
      gridProfileHandleAction(gridAction);
    }
     //Serial.println("test_actionFlag 4 val = " + String(actionFlag));
    if (actionFlag == 21) {
      actionFlag = 0; //reset the actionflag
      coordinator(true); // start normal operation mode
    }
 //Serial.println("test_actionFlag 5 val = " + String(actionFlag));
// mosquitto test **********************************************    
    if (actionFlag == 24) { // set by the mqtt config page
        actionFlag = 0; //reset the actionflag
        MQTT_Client.disconnect();
       //reset the mqttserver
        MQTT_Client.setServer(Mqtt_Broker, atoi(Mqtt_Port) );
        MQTT_Client.setCallback ( MQTT_Receive_Callback ) ;
        if (Mqtt_Format != 0) mqttConnect(); // reconnect mqtt after change of settings
    }    
 //Serial.println("test_actionFlag 6 val = " + String(actionFlag));
    if (actionFlag == 25) {
      actionFlag = 0; //reset the actionflag
      getTijd(); // recalculate time after change of settings
    }
    //Serial.println("test_actionFlag 7 val = " + String(actionFlag));
    if (actionFlag > 239 && actionFlag < 249) {
      int whichInv = actionFlag - 240; // with 248 this would be 8  0 1 2 3 4 5 6 7 8 is in range
      consoleOut("inside actionFlag: whichInv is " + String(whichInv));
      actionFlag = 0; //reset the actionflag
      // als setmaxpower geslaagd is dan 
      if(setMaxPower(whichInv) == true) {
         //Inv_Prop[whichInv].throttled = true;
          String term= "throttle inv " + String(whichInv) + " success";
          Update_Log(2, term.c_str());
      } else {
        // the setPower command failed, so we set out value to 800
        desiredThrottle[whichInv] = -1;
        //Inv_Prop[whichInv].throttled = false;
        String term= "throttle inv " + String(whichInv) + " failed";
        consoleOut("throttle failed inv " + String(whichInv));
        Update_Log(2, term.c_str());
      }
      //now save in preferences
      //Preferences 'namespace' (map) in writemodus (false = write mode)
      preferences.begin("my-data", false); 

      // ake a key for this specific inverter (ed. "maxPwr2")
      String key = "maxPwr" + String(whichInv);
      // save
      preferences.putInt(key.c_str(), desiredThrottle[whichInv]);
      // close preferences 
      preferences.end();
      // Optioneel: Print een bevestiging naar de Seriële Monitor
      consoleOut("Successfully saved: " + key + " = " + String(desiredThrottle[whichInv]));
    
    //String bestand = "/Inv_Prop" + String(whichInv) + ".str"; // /Inv_Prop0.str
    //consoleOut("going to write " + bestand );
    //writeStruct(bestand, whichInv); // alles opslaan in SPIFFS
    }
    
    
    if (actionFlag == 43) { //triggered by the console
        actionFlag = 0; //reset the actionflag
        inverterReboot(iKeuze);
    }
    if (actionFlag == 44) { //triggered by the webpage zbtest
        actionFlag = 0; //reset the actionflag
        healthCheck(); 
    }
    if (actionFlag == 45) { //triggered by the webconsole
        actionFlag = 0; //reset the actionflag
        //Serial.println("someone made actionFlag 45 !!"); the uin8_t doesn't allow 301
        testMessage(true); // the bool decides where to find the input
    }
        if (actionFlag == 55) { //triggered by the webconsole
        actionFlag = 0; //reset the actionflag
        //Serial.println("someone made actionFlag 45 !!"); the uin8_t doesn't allow 301
        rawMessage(true); // the bool decides where to find the input
    }
    if (actionFlag == 46) { //triggered by the webpage zbtest
        actionFlag = 0; //reset the actionflag
        showDir(); 
    }
    
    // polling a single inverter
    if (actionFlag == 47) { //triggered by the webpage zbtest and mqtt
        actionFlag = 0; //reset the actionflag
        polling(iKeuze);
        if (polled[iKeuze]) inverterInfoMaybeQuery(iKeuze);
        //events.send( "getall", "message");
        eventSend(2); 
    }
    // query a single inverter
    if (actionFlag == 57) { //triggered by the webpage zbtest and mqtt
        actionFlag = 0; //reset the actionflag
        querying(iKeuze);
        //events.send( "getall", "message");
        //eventSend(2); 
    }
    //polling all inverters
    if (actionFlag == 48) { //triggered by the webpage zbtest and mqtt
        actionFlag = 0; //reset the actionflag
        ledblink(1,100);
        pollSchedulerRequest(true);
    }

    if (actionFlag == 49) { //triggered by console testmqtt
        actionFlag = 0; //reset the actionflag
        ledblink(1,100);
        // always first drop the existing connection
        MQTT_Client.disconnect();
        ws.textAll("dropped connection");
        delay(100);
        char Mqtt_send[26] = {0};
       
        if(mqttConnect() ) {
        String toMQTT=""; // if we are connected we do this
        
        strcpy( Mqtt_send , Mqtt_outTopic);
        
        if(Mqtt_send[strlen(Mqtt_send -1)] == '/') strcat(Mqtt_send, String(Inv_Prop[0].invIdx).c_str());
        toMQTT = "{\"test\":\"" + String(Mqtt_send) + "\"}";
        
        if(Mqtt_Format == 5) toMQTT = "field1=12.3&field4=44.4&status=MQTTPUBLISH";
        
        if( MQTT_Client.publish (Mqtt_outTopic, toMQTT.c_str() ) ) {
            ws.textAll("sent mqtt message : " + toMQTT);
        } else {
            ws.textAll("sending mqtt message failed : " + toMQTT);    
        }
      } 
     // the not connected message is displayed by mqttConnect()
    }


} // end test actionflag


void poll_all() {
 // Compatibility entry point: schedule rather than blocking the application
 // for an entire fleet round.
 pollSchedulerRequest(true);
 }

String getChipId(bool sec) {
    uint32_t chipId = 0;
    for(int i=0; i<17; i=i+8) {
    chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }
  if(sec) return String(chipId); else return "ESP32-ECU-" + String(chipId);
}

void showDir() {
    char temp[50]={0};
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    while (file) {
      sprintf(temp, "%s  size %u<br>" , file.name(), file.size() );
      consoleOut(String(temp));
      delay(100);
      file = root.openNextFile();
    }
  }

  // function to 
void consoleOut(String toLog) {
 
  if( diagNose == 0 ) return; 
  if (diagNose == 2 )
  {
      Serial.println(toLog);
  } else {
      delay(100); // otherwise the socket cannot keep up
      ws.textAll(toLog);
  }

}
