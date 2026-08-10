void getTijd() {

  timeRetrieved = false; // stays false until time is retrieved  
  timeClient.begin();
  //unsigned long epochTime = 0;
  //get the time, if fails we try again during healthcheck

  timeClient.update();
  unsigned long epochTime = timeClient.getEpochTime();


  //Serial.print("Epoch Time: ");
  //Serial.println(epochTime);

    // now convert NTP time into unix tijd:
    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    //const unsigned long seventyYears = 2208988800UL;
    // subtract seventy years:
//    unsigned long epoch = secsSince1900 - seventyYears + atof(timezone) * 60; // * 60 weggehaald omdat timezone in minuten is
//    unsigned long epochTime = timeClient.getEpochTime;
    // we have to do this conditional, if time retrieving failed
    if (epochTime < 1000) {
    ntpUDP.stop();
    return;
  } else {
   
    if (!ecuSetLocalTimeFromUtc((time_t)epochTime)) {
      ntpUDP.stop();
      return;
    }
    timeRetrieved=true;
    Update_Log(1, "got time");
    }
    //DebugPrint(" Unix time epoch = ");
    //DebugPrintln(epochTime);
  
ntpUDP.stop();
//
//  // de tijd is nu opgehaald en in setTime gestopt
//  // dus met de tijden die met setTime zijn opgeslagen gaan we  alle berekeningen doen
//  
//DebugPrint("het uur is ");  //DebugPrint(hour());
//DebugPrint("   aantal minuten "); //DebugPrintln(minute());
datum = day();
//
//yield();
delay(10);
sun_setrise(); //to calulate moonshape sunrise etc. and the switchtimes

//  switchonTime = sunrise - 900;
//  switchoffTime = sunset + 900; // nightmode starts at 15 min after sunset
}
