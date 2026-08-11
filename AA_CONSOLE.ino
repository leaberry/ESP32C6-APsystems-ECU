void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (!info->final || info->index != 0 || info->len != len ||
      info->opcode != WS_TEXT || len < 3 || len >= sizeof(txBuffer))
    return;
  memcpy(txBuffer, data, len);
  txBuffer[len] = '\0';

  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
  {
      //diagNose = 2; // direct the output to ws

     
           if (strncasecmp(txBuffer+3,"INV_REBOOT",10) == 0) {
              ws.textAll("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n<br>");
              ws.textAll("Reboot an inverter that stopped working.");
              ws.textAll("Characteristics: not responsive, (slow blinking red led).");
              ws.textAll("type REBOOT_INVERTER=x (x=inverternumber 0, 1 etc.)");
              ws.textAll("DISCLAIMER: THIS HAS NOT BEEN TESTED, USE AT YOUR OWN RISK!");
              return;
          } else         

          if (strncasecmp(txBuffer+3,"POLL=",5) == 0) {
            //input can be 10;POLL=0; 
            //ws.textAll("received " + String( (char*)data) + "<br>"); 
              int kz = String(txBuffer[8]).toInt();
              if (kz == 9) {
                ws.textAll("poll all inverters");
                actionFlag = 48;
                return;
              }
              if (kz < 0 || kz > inverterCount - 1) {
                ws.textAll("error, no such inverter");
                return;
              }
              ws.textAll("poll inverter " + String(kz));
              iKeuze=kz;
              actionFlag=47;
              return;
          } else 
          if (strncasecmp(txBuffer+3,"QUERY=",6) == 0) {
            //input can be 10;QUERY=0; 
            //ws.textAll("received " + String( (char*)data) + "<br>"); 
              //int kz = String(txBuffer[9]).toInt();
              int kz = atoi(txBuffer + 9);
              if ( kz > inverterCount-1 ) {
              ws.textAll("error, no such inverter");
              return;  
              }
              ws.textAll("console query inverter " + String(kz));
              iKeuze=kz;
              actionFlag=57;
              return;
          } else

          if (strncasecmp(txBuffer+3,"THROTTLE=",9) == 0) {
            //input can be 10;EDIT=0-AABB; 
            char *first = txBuffer + 12;
            char *second = strchr(first, '-'); // find dash
            int kz; 
            if (second) {
                *second = '\0'; // terminate first number
                kz = atoi(first);
                int watt = atoi(second + 1);
            consoleOut("inverter = " + String(kz));
            consoleOut("watt = " + String(watt));
            desiredThrottle[kz] = watt;
            }  
              if ( kz > inverterCount-1 ) {
              ws.textAll("error, no such inverter");
              return;  
              }
             actionFlag = 240 + kz; 
              ws.textAll("actionFlag=" + String(actionFlag));
              return;
          } else  
          if (strncasecmp(txBuffer+3,"EDIT=",5) == 0) {
            //input can be 10;EDIT=0-AABB; 
            //ws.textAll("received " + String( (char*)data) + "<br>"); 
              int kz = String(txBuffer[8]).toInt();
              if ( kz > inverterCount-1 ) {
              ws.textAll("error, no such inverter");
              return;  
              }
              char invid[5];
              for(int i=10;  i<15; i++) { invid[i-10] = txBuffer[i]; }
              ws.textAll("edit inverter " + String(kz));
              ws.textAll("id = " + String(invid));
              strncpy(Inv_Prop[kz].invID, invid, 4);
              String bestand = "/Inv_Prop" + String(kz) + ".str"; // /Inv_Prop0.str
              writeStruct(bestand, kz); // save in SPIFFS 
              return;
          } else 
           
           if (strncasecmp(txBuffer+3,"HEALTH",6) == 0) {  
              ws.textAll("check zb system");
              actionFlag=44; // perform the healthcheck
              return;             
          } else          

   
 // ************  test mosquitto *******************************          
           if (strncasecmp(txBuffer+3,"TESTMQTT",8) == 0) {  
              ws.textAll("test mosquitto");
              actionFlag=49; // perform the healthcheck
              return;             
          } else 

           if (strncasecmp(txBuffer+3,"CLEAR",5) == 0) {  
              ws.textAll("clearWindow");
              return;             
          } else

          if (strncasecmp(txBuffer+3,"REBOOT_INVERTER=",16) == 0) {
              int kz = String(txBuffer[19]).toInt();
              ws.textAll("reboot inverter " + String(kz));
              if ( kz > inverterCount-1 ) 
              {
                 ws.textAll("error, non-excisting inverter");
                 return;  
              }
                 actionFlag = 34;
              return;
          } else

           if (strncasecmp(txBuffer+3,"FILES",5) == 0) {  
              //we do this in the loop
              ws.textAll("listing files..\n");
              actionFlag = 46;
              return;             
          
          } else 
 
 
 // ********************** zigbee test new*****************************          
           if (strncasecmp(txBuffer+3,"ZBT=",4) == 0) {  
              ws.textAll("going to send a teststring, len=" + String(len));
              //we do this in the loop
              actionFlag = 45;
              return;             
          } else 
 // ********************** zigbee test raw *****************************          
           if (strncasecmp(txBuffer+3,"SENDRAW=",8) == 0) {  
              ws.textAll("send a raw message, len=" + String(len));
              //we do this in the loop
              actionFlag = 55;
              return;             
          } else 
           if (strncasecmp(txBuffer+3,"ERASE",5) == 0) {  
              ws.textAll("going to delete all inverter files");
              String bestand;
              for(int i=0; i<50; i++) 
              {
                  String bestand = "/Inv_Prop" + String(i) + ".str";
                  if (SPIFFS.exists(bestand)) 
                  {
                      SPIFFS.remove(bestand);
                      ws.textAll("removed file " + bestand);
                  }

              }
              inverterCount = 0;
              basisConfigsave(); // save inverterCount
              ws.textAll("done");
              return;             
          
          } else            
           
           if (strncasecmp(txBuffer+3,"DELETE=",7) == 0) {  
              //input can be 10;DELETE=filename
              String bestand="";
              for(int i=10;  i<len+1; i++) { bestand += String(txBuffer[i]); }
               ws.textAll("bestand = " + bestand); 
              if (SPIFFS.exists(bestand)) 
              {
                  ws.textAll("going to delete file " + bestand); 
                      SPIFFS.remove(bestand);
                      ws.textAll("file " + bestand + " removed!"); 
                      if(bestand.indexOf("/Inv_Prop") != -1) {
                      consoleOut("we deleted an inverterfile");  
                      inverterCount -= 1;
                      basisConfigsave();  // save inverterCount
                      remove_gaps();
                     }
              } else 
              { 
                 ws.textAll("no such file");
              }
              return;                      
          } else

      if (strncasecmp(txBuffer+3, "DIAG",4) == 0) // normal operation
      {
         switch(diagNose) {
         case 0: 
            diagNose = 1; 
            break;
         case 1:
            diagNose = 2; 
            break;            
         default:
            diagNose = 0; 
            break; 
         }
         ws.textAll("set diagnose to " + String(diagNose) );
         write_eeprom();
         return;   
// ****************************************************************
      } else      
      
     if (strncasecmp(txBuffer+3, "INIT_N",6) == 0) // normal operation
      {
         ws.textAll("command = " + String(txBuffer) );  
         actionFlag = 21;
         diagNose=true;
         return;
// ***************************************************************
//      } else 
//
//      if (strncasecmp(txBuffer+3, "INIT_P",6) == 0)  // pairing
//      {
//         ws.textAll("command = " + String(txBuffer) );  
//         actionFlag = 22;

#ifdef TEST
      } else 

      if (strncasecmp(txBuffer+3, "TESTINV",7) == 0)  
      {
         ws.textAll("command = " + String(txBuffer) );  
 //          which = String(txBuffer[10]).toInt();
  //         ws.textAll("chosen = " + String(which) );
 
         actionFlag = 122;
#endif      
      
 
      } else {

       
       ws.textAll("unknown command"); 
      }
  
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
    //Serial.println("onEvent triggered");
    switch (type) {
      case WS_EVT_CONNECT:
        //Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
        break;
      case WS_EVT_DISCONNECT:
        //Serial.printf("WebSocket client #%u disconnected\n", client->id());
        break;
      case WS_EVT_DATA:
        //Serial.println("WebSocket received data");
        handleWebSocketMessage(arg, data, len);
        break;
      case WS_EVT_PONG:
      case WS_EVT_ERROR:
        break;
  }
}

void initWebSocket() {
  ws.setAuthentication("admin", pswd, AsyncAuthType::AUTH_BASIC);
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}
