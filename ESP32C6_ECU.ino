#include "AAA_INCLUDES.h"
// *****************************************************************************
// *                              SETUP
// *****************************************************************************
void setup() {
  Serial.begin(115200);
  Serial.println(F("ESP32-C6 integrated Zigbee build (no CC2530/CC2531)"));
  Serial.println(apsCryptoSelfTest() ? F("APsystems AES self-test: PASS")
                                     : F("APsystems AES self-test: FAILED"));


  pinMode(knop, INPUT_PULLUP); // de knop
  pinMode(led_onb, OUTPUT); // onboard led
 // pinMode(ZB_TX, OUTPUT);
 // digitalWrite(ZB_TX, LOW); //
  pinMode(4, OUTPUT); //
  digitalWrite(4, LOW); //

   ledblink(1, 800);

   attachInterrupt(digitalPinToInterrupt(knop), isr, FALLING);


  SPIFFS_read();
  // now we know the number of inverters we can find an interval between pollings
  //int pollintervall = 300/inverterCount;
  // takes care for the return to the last webpage after reboot
  //read_eeprom(); // put the value of diagNose back
  preferences.begin("my_data", false); //open preferences for r/w
  //DebugPrint("apFlag = " + String(apFlag) );

   for(int z=0; z < inverterCount; z++)
   {
    String key = "maxPwr" + String(z);
    desiredThrottle[z] = preferences.getInt(key.c_str(), -1);
    Serial.print("preferences key " + key);
    Serial.println(" = " + String(desiredThrottle[z], 0));
   }
   String key = "req";
   preferences.getString(key.c_str(), requestUrl, sizeof(requestUrl) );
   Serial.println("requestUrl = " + String(requestUrl));
  // takes care for the return to the last webpage after reboot
  preferences.end();

  start_wifi(); // start wifi and server
  sunspecBegin();

// we set inverterCount to the number of inverterfiles we find
  inverterCount = readInverterfiles();
  Serial.println("\ninverterCount = " + String(inverterCount));
  printInverters(); // show all the inverter files

  getTijd(); // retrieve time from the timeserver
  Update_Log(1, "boot up");

  // ****************** mqtt init *********************
  MQTT_Client.setKeepAlive(150);
  MQTT_Client.setServer(Mqtt_Broker, atoi(Mqtt_Port));
  MQTT_Client.setBufferSize(300); //to avoid freeze ups
  MQTT_Client.setCallback ( MQTT_Receive_Callback ) ;

  if ( Mqtt_Format != 0 )
  {
       Serial.println(F("setup: mqtt configure"));
       mqttConnect(); // mqtt connect
  }
  else
  {
       Update_Log(3, "not enabled");
  }

  initWebSocket();

  Serial.println(F("booted up, checking coordinator"));
  Serial.println(WiFi.localIP());

  delay(1000);
  ledblink(3,500);

  Update_Log(1,"healthcheck");
  healthCheck(); // check the state of the zigbee system and if oke then poll the inverters
  if(zigbeeUp == 1) {
        Update_Log(2,"running");
          // we poll our inverters immediatly
          if(Polling)
          {
            poll_all();
          }
     }

  resetCounter = 0;
  //events.send( "reload", "message"); //getGeneral and getAll triggered
  eventSend(0);

} // end setup()

//****************************************************************************
//*                         LOOP
//*****************************************************************************
void loop() {

// ***************************************************************************
//                       day or night mode
// ***************************************************************************
#ifdef TEST
// always daytime to be able to test
 dayTime = true;
#endif

   if(now() > switchonTime && now() < switchoffTime)
    {
          if(!dayTime)
          {
             dayTime = true;
             Update_Log(1, "woke up");
             consoleOut("woke-up");
             // reset the dayly energy at wakeup and send mqtt message
             resetValues(true, true);
             //events.send( "reload", "message"); // refresh the data and state
             eventSend(1);
            }
    } else {
         if(dayTime)
         {
            dayTime = false;
            //String term= "nightmode";
            Update_Log(1, "nightmode");
            consoleOut("nightmode");
            // clean memory
            //memset( &inMessage, 0, sizeof(inMessage) ); //zero out the
            //delayMicroseconds(250);
            // we send null messages for each inverter
            resetValues(false, true); // make all values zero exept energy and send mqtt
            //events.send( "reload", "message"); // refresh the data and state
            eventSend(0);
            midnightFlag = 250; // triggers the reset values and mqtt null message at midnight

         }
    }

// ******************************************************************
//              polling every 300 seconds
// ******************************************************************
#ifndef TEST
  unsigned long nu = millis();  // the time the program is running

   if (nu - laatsteMeting >= 1000UL * 300) // 300 sec
   {
     consoleOut("300 secs passed, polling" + String(millis()) ); //
        laatsteMeting += 1000UL * 300 ; // increases each time with (300/inverterCount * miliseconds);
        if(dayTime && Polling) // we only poll at day and when Polling = true
           {
              ledblink(1,100);
              poll_all(); //if inverterCount = 9 than we have inverters 0-8
           }
 }

// ******************************************************************
//              healthcheck every 10 minutes
// ******************************************************************

   nu = millis() + 1000UL*120; // 2 minutes later //
   if (nu - lastCheck >= 1000UL * 600) // =10min
   {
   Serial.println("600 secs passed, healthcheck" + String(millis()) );
         lastCheck += 1000UL * 600;
         //we dont do healtcheck when stopped
         healthCheck(); // checks zb radio, mqtt and time, when false only message if error
   }

  // we recalcultate the switchtimes for this day when there is a new date
  // if retrieve fails, day will not be datum, so we keep trying by healthcheck
  if (day() != datum && hour() > 2) // if date overflew and later then 2
  {
          getTijd(); // retrieve time and recalculate the switch times
          //delay(500);
          //ESP.restart();
          //DebugPrintln("date overflew, retrieve time");
  }
 #endif
// ***************************************************************************
//                       m q t t
// ***************************************************************************
       // before each transmission the connection is tested
       // so we don't do this in the loop
       if(Mqtt_Format != 0 ) MQTT_Client.loop(); //looks for incoming messages

  //*********************************************************************
  //             send null data at midnight
  // ********************************************************************
  if(hour() == 0 && timeRetrieved && midnightFlag == 250)
  {
      if(second() > 0 )
      {
        resetValues(true, true); //set all values to zero and sent mqtt
        Update_Log(1, "values wipe");
        midnightFlag = 0; // to prevent repetition
        //events.send( "getall", "message"); // refresh the data
        eventSend(2);
       }
  }

  test_actionFlag();

   ws.cleanupClients();
   yield(); // to avoid wdt resets

  // SERIAL: *************** kijk of er data klaar staat op de seriele poort **********************
  if(Serial.available()) {
       handle_Serial();
   }


}
//****************  End Loop   *****************************



// // ****************************************************************
// //                  eeprom handlers
// //*****************************************************************
// void write_eeprom() {
//     EEPROM.begin(24);
//   //struct data
//   struct {
//     char str[16] = "";
//     int haha = 0;

//   } data;

//  strcpy( data.str, requestUrl );
//  data.haha = iKeuze;
//      EEPROM.put(0, data);
//     EEPROM.commit();
// }

// void read_eeprom() {
//     EEPROM.begin(24);

//   struct {
//     char str[16] = "";
//     int haha = 0;
//   } data;

// EEPROM.get(0, data);
// consoleOut("read value from EEPROM is " + String(data.str));
// strcpy(requestUrl, data.str);
// iKeuze = data.haha; // inverterkeuze
// EEPROM.commit();
// }

// // all actions called by the webinterface should run outside the async webserver environment
// // otherwise crashes will occure.
