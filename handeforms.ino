bool handleForms(AsyncWebServerRequest *request)
{
     //every form submission is handled here
     // we find out which form with a parameter present 
     String serverUrl = request->url().c_str();
     Serial.println("serverUrl = " + serverUrl); // this is /submitform 

     // Polling/access and time/location now use validated POST handlers.
     if(request->hasParam("mqtAdres")) {
        const char *mqttParams[] = {"mqtPort", "mqtoutTopic", "mqtUser",
                                    "mqtPas", "mqidx", "fm"};
        for (const char *name : mqttParams) if (!request->hasParam(name)) {
          request->send(400, "text/plain", "Incomplete MQTT settings");
          return false;
        }
        // form mosquitto received
  strlcpy(Mqtt_Broker, request->getParam("mqtAdres")->value().c_str(), sizeof(Mqtt_Broker));
  strlcpy(Mqtt_Port, request->getParam("mqtPort")->value().c_str(), sizeof(Mqtt_Port));
  strlcpy(Mqtt_outTopic, request->getParam("mqtoutTopic")->value().c_str(), sizeof(Mqtt_outTopic));
  strlcpy(Mqtt_Username, request->getParam("mqtUser")->value().c_str(), sizeof(Mqtt_Username));
  strlcpy(Mqtt_Password, request->getParam("mqtPas")->value().c_str(), sizeof(Mqtt_Password));
  //strcpy( Mqtt_Clientid, request->getParam("mqtCi")     ->value().c_str() );  
  Mqtt_stateIDX = request->arg("mqidx").toInt(); //values are 0 1 2
  Mqtt_Format = request->arg("fm").toInt(); //values are 0 1 2 3 4 5
        mqttConfigsave();  // 
        actionFlag=24; // reconnect with these settings
        return true;
  } else
// the request is something like pMax=200 MPW=0 
  if(request->hasParam("pMax")) // name of the hidden input
  {    // because of the hidden input named maxPower we know we received the setPower form 
      Serial.println("found pMax");
      // check that the form has the right params ( should be pMax and MPW)
      int params = request->params();
      Serial.print("Number of params: ");
      Serial.println(params);   
      for (int i = 0; i < params; i++) {
        const AsyncWebParameter* p = request->getParam(i);
        Serial.print("Param name: ");
        Serial.println(p->name());
        Serial.print("Param value: ");
        Serial.println(p->value());
      }
       
       if (!request->hasParam("INV")) {
         request->send(400, "text/plain", "Missing inverter index");
         return false;
       }
       int Inv = request->arg("INV").toInt();
       int throttle = request->getParam("pMax")->value().toInt();
       if (Inv < 0 || Inv >= inverterCount || throttle < 0 || throttle > 500) {
         request->send(400, "text/plain", "Invalid inverter or throttle value");
         return false;
       }
       Serial.println("the form is for inverter " + String(Inv));
       
       desiredThrottle[Inv] = throttle;
       
       //Inv_Prop[Inv].maxPower = request->getParam("pMax")->value().toInt();
      //{
        //  int add; // to determin which inverter we are editing
          // if(request->hasParam("maxP0")) {Inv_Prop[0].maxPower = request->getParam("maxP0")->value().toInt(); add=0;}
          // if(request->hasParam("maxP1")) {Inv_Prop[1].maxPower = request->getParam("maxP1")->value().toInt(); add=1;}
          // if(request->hasParam("maxP2")) {Inv_Prop[2].maxPower = request->getParam("maxP2")->value().toInt(); add=2;}
          // if(request->hasParam("maxP3")) {Inv_Prop[3].maxPower = request->getParam("maxP3")->value().toInt(); add=3;}
          // if(request->hasParam("maxP4")) {Inv_Prop[4].maxPower = request->getParam("maxP4")->value().toInt(); add=4;}
          // if(request->hasParam("maxP5")) {Inv_Prop[5].maxPower = request->getParam("maxP5")->value().toInt(); add=5;}
          // if(request->hasParam("maxP6")) {Inv_Prop[6].maxPower = request->getParam("maxP6")->value().toInt(); add=6;}     
          // if(request->hasParam("maxP7")) {Inv_Prop[7].maxPower = request->getParam("maxP7")->value().toInt(); add=7;}
          // if(request->hasParam("maxP8")) {Inv_Prop[8].maxPower = request->getParam("maxP8")->value().toInt(); add=8;}
          //for (int i = 0; i <= 8; i++) {
          //    String paramName = "maxP" + String(i);
          //    Serial.println("parameter name = " + paramName);
          //    if (request->hasParam(paramName)) {
          //        Inv_Prop[i].maxPower = request->getParam(paramName)->value().toInt();
          //       add = i;
          //        break;
          //    }
          //}
                
          Serial.println("desiredThrottle[Inv] set to = " + String(desiredThrottle[Inv])); 
          

          
          
          actionFlag=240 + Inv; // save the settings and send zigbee to inverter
          Serial.println("actionFlag set to " + String(actionFlag));
          //Serial.println("setting the return url to /details?inv=");
          String toReturn = "/details?inv=" + String(Inv);
          strlcpy(requestUrl, toReturn.c_str(), sizeof(requestUrl));
          Serial.println("requestUrl = " + String(requestUrl));
          return true;

  }

     // if we are here something was wrong, no parameters found
     request->send(200, "text/html", "no valid form found");
     return false;
}
