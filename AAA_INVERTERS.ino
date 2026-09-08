void handleInverterconfig(AsyncWebServerRequest *request)
{
  if (pendingPairInverter >= 0) {
    request->send(409, "text/plain", "Wait for pairing to finish before editing inverters");
    return;
  }

  // form action = handleInverterconfig
  // we only collect the data for this specific inverter
  // collect the serverarguments
   int requestedIndex = -1;
   if (!inverterRequestIndex(request, "inv", true, requestedIndex)) {
     request->send(400, "text/plain", "Missing or invalid inverter index");
     return;
   }
   iKeuze = requestedIndex;
   if (iKeuze < 0 || iKeuze >= YC600_MAX_NUMBER_OF_INVERTERS ||
       !request->hasParam("il") || !request->hasParam("iv") ||
       !request->hasParam("invt") || !request->hasParam("mqidx") ||
       !request->hasParam("cal")) {
     request->send(400, "text/plain", "Invalid or incomplete inverter settings");
     return;
   }
   String submittedSerial = request->arg("iv");
   if (submittedSerial.length() != 12) {
     request->send(400, "text/plain", "Inverter serial number must contain 12 digits");
     return;
   }
   for (size_t i = 0; i < submittedSerial.length(); ++i) {
     if (!isdigit((unsigned char)submittedSerial[i])) {
       request->send(400, "text/plain", "Inverter serial number must contain only digits");
       return;
     }
   }
   strlcpy(Inv_Prop[iKeuze].invLocation, request->arg("il").c_str(),
           sizeof(Inv_Prop[iKeuze].invLocation));
   strlcpy(Inv_Prop[iKeuze].invSerial, submittedSerial.c_str(),
           sizeof(Inv_Prop[iKeuze].invSerial));
   Inv_Prop[iKeuze].encrypted = apsSerialDefaultsToEncrypted(Inv_Prop[iKeuze].invSerial);
   Inv_Prop[iKeuze].invType = constrain(request->arg("invt").toInt(), 0, 2);
   Inv_Prop[iKeuze].invIdx = constrain(request->arg("mqidx").toInt(), 0, 65535);
   Inv_Prop[iKeuze].calib = constrain(request->arg("cal").toInt(), -15, 15);

// the selectboxes
   char tempChar[1] = "";
   if(request->hasParam("pan1")) { Inv_Prop[iKeuze].conPanels[0] = true;} else { Inv_Prop[iKeuze].conPanels[0] = false;}  // mqselect
   if(request->hasParam("pan2")) { Inv_Prop[iKeuze].conPanels[1] = true;} else { Inv_Prop[iKeuze].conPanels[1] = false; }

   Inv_Prop[iKeuze].conPanels[2] = false;
   Inv_Prop[iKeuze].conPanels[3] = false;
   //we only collect this when type = 1
   if(Inv_Prop[iKeuze].invType == 1) {
   if(request->hasParam("pan3")) { Inv_Prop[iKeuze].conPanels[2] = true;}    
   if(request->hasParam("pan4")) { Inv_Prop[iKeuze].conPanels[3] = true;}    
   }
   //DebugPrintln("checked panels are : " + String(Inv_Prop[iKeuze].conPanels[0])+ String(Inv_Prop[iKeuze].conPanels[2])+ String(Inv_Prop[iKeuze].conPanels[2])+ String(Inv_Prop[iKeuze].conPanels[3]));
   //is this a addition?
   String bestand = "/Inv_Prop" + String(iKeuze) + ".str"; // /Inv_Prop0.str
   consoleOut("going to write " + bestand ); 
   //initial their both 0
   writeStruct(bestand, iKeuze); // alles opslaan in SPIFFS
   if(iKeuze == inverterCount) 
   {
    energyResetInverterState(iKeuze);
    inverterCount += 1;
    consoleOut("we appended, inverterCount now : " + String(inverterCount)); 
    }
   
   basisConfigsave();  // save inverterCount

   consoleOut("\ninverterCount after edit (saved) = " + String(inverterCount));  
   consoleOut("list of the files we have after edit");
   printInverters();
   request->redirect("/inverter/select?welke=" + String(iKeuze) + "&status=saved");
}

//*******************************************************************************************
//             delete an inverter
// *****************************************************************************************
void handleInverterdel(AsyncWebServerRequest *request)
{
  if (pendingPairInverter >= 0) {
    request->send(409, "text/plain", "Wait for pairing to finish before editing inverters");
    return;
  }

  // form action = handleInverterconfig
  // we only collect the data for this specific inverter
  // read the serverargs and copy the values into the variables

   int requestedIndex = -1;
   if (!inverterRequestIndex(request, "inv", false, requestedIndex)) {
     request->send(400, "text/plain", "Missing or invalid inverter index");
     return;
   }
   iKeuze = requestedIndex;
   String bestand = "/Inv_Prop" + String(iKeuze) + ".str"; // /Inv_Prop0.str
   consoleOut("remove file " + bestand ); 
 
   if(SPIFFS.exists(bestand) ) SPIFFS.remove(bestand);
   
   consoleOut("list of the files we have after removed one");
   printInverters();
   inverterCount -= 1;
   basisConfigsave();  // save inverterCount   
//   // now we may have a gap in the file order
//   // check if we have one and remove it
   remove_gaps();
      //Serial.println(F("list of the files after remove gaps"));
    
    printInverters(); 
    
    consoleOut("inverterCount after removal = " + String(inverterCount));

    if (inverterCount > 0) {
      const int nextIndex = min(iKeuze, inverterCount - 1);
      request->redirect("/inverter/select?welke=" + String(nextIndex) + "&status=deleted");
    } else {
      request->redirect("/inverters?status=deleted");
    }
}

bool inverterRequestIndex(AsyncWebServerRequest *request, const char *parameter,
                          bool allowNew, int &result) {
  if (!request || !parameter || !request->hasParam(parameter)) return false;
  const String text = request->getParam(parameter)->value();
  if (text.isEmpty()) return false;
  for (size_t i = 0; i < text.length(); ++i)
    if (!isdigit((unsigned char)text[i])) return false;
  const long parsed = text.toInt();
  const long maximum = allowNew ? inverterCount : inverterCount - 1;
  if (parsed < 0 || parsed > maximum ||
      parsed >= YC600_MAX_NUMBER_OF_INVERTERS) return false;
  result = (int)parsed;
  return true;
}

void printInverters() { 
      if(diagNose == 0 ) return;     
      consoleOut(F(" ****** excisting inverter files ******"));
      for (int x=0; x < inverterCount+1; x++) 
      {
      String bestand = "/Inv_Prop" + String(x) + ".str";
      
      if(SPIFFS.exists(bestand)) 
          {
              consoleOut("filename: " + bestand);
              printStruct(bestand);
          }
         
      }
}

// say we have
// Inv_prop0.str
// Inv_prop1.str

// Inv_prop3.str
// Inv_Prop4.str

// after we found 3 and not 2 we have
// Inv_prop0.str
// Inv_prop1.str
// inv_prop2.str
// Inv_prop3.str we renamed this one so the gap moved
// Inv_Prop4.str

// remove the gaps
void remove_gaps() {
String bestand_1;
String bestand_2;
bool found = false;  

// say we have Inv_Prop0, Inv_Prop1, Inv_Prop3, Inv_Prop4, Inv_Prop5
// this are 5 files 
// there can only a gap of 1 inverter(can only remove 1 at a time) 
// if we know the inverterCount we can search for a gap and put the last file in it
// so if we are missing Inv_Prop1, we know that we have Inv_prop5  
  
  inverterCount = readInverterfiles(); // this should be 5 in the case above
  for(int i=0; i < inverterCount; i++ ) { // 0 1 2 3 4
  bestand_1 = "/Inv_Prop" + String(i) + ".str";
  // if this file not exixts we know that there must be a file "?inv_Prop inverterCount.str
  if( !SPIFFS.exists(bestand_1) ) {
      consoleOut("found a gap" + bestand_1);  
      bestand_2 = "/Inv_Prop" + String(inverterCount) + ".str"; // the last file
      if( !SPIFFS.exists(bestand_2) ) consoleOut("error, " + bestand_2 + " not exists");  
   // if we rename the last file to the gap, it keeps the old content
   // so we just cope the struct and write that to spiffd
      consoleOut("copy the last struct " + bestand_2 + " to " + bestand_1);
      
      structCopy(i, inverterCount);
      writeStruct(bestand_1, i); // write the copied struct
      SPIFFS.remove(bestand_2);
      //SPIFFS.rename(bestand_2, bestand_1); // file 2 becomes file 1
      return;  
      }
  }
  consoleOut("no gaps found");
} 

   
//  for(int i=0; i < 10; i++ ) 
//  {
//      bestand_1 = "/Inv_Prop" + String(i) + ".str";
//      bestand_2 = "/Inv_Prop" + String(i+1) + ".str";
//      //Serial.println("bestand_1 = " + bestand_1);
//      //Serial.println("bestand_2 = " + bestand_2);
//      if(!SPIFFS.exists(bestand_1) && SPIFFS.exists(bestand_2)) 
//      {
//      //Serial.println(bestand_1 + " not exist and " + bestand_2 + " exists"); 
//        found = true;
//        SPIFFS.rename(bestand_2, bestand_1); // file 2 becomes file 1
//      //Serial.println("renamed " + bestand_1);
//        printInverters();    
//      }
//  }
//  // we remove the last file
//  if (found) 
//    {
//    bestand_1 = "/Inv_Prop" + String(inverterCount) + ".str"; 
//    if(!SPIFFS.exists(bestand_1) ) SPIFFS.remove(bestand_1);
//    }
//}
// ********************************************************************
//                     processor
// *********************************************************************
String processor(const String& var)
{
//
  if(var == "LOADBAG") 
  {
    consoleOut(F("found LOADBAG"));
    if(Inv_Prop[iKeuze].invType == 1) 
      {
      return F("showFunction()"); 
      } else {
      return F("hideFunction()");  
      }
  }
  if (var == "INVERTER_NAV") {
    String navigation;
    for (int x = 0; x < inverterCount && x < YC600_MAX_NUMBER_OF_INVERTERS; ++x) {
      navigation += iKeuze == x ? F("<a class=\"button\" aria-current=\"page\" href=\"/inverter/select?welke=")
                               : F("<a class=\"button secondary\" href=\"/inverter/select?welke=");
      navigation += String(x);
      navigation += F("\">");
      const char *name = Inv_Prop[x].invLocation;
      navigation += (name[0] && strcmp(name, "N/A") != 0)
          ? String(name) : "Inverter " + String(x + 1);
      navigation += F("</a>");
    }
    if (inverterCount < YC600_MAX_NUMBER_OF_INVERTERS) {
      navigation += iKeuze == inverterCount
          ? F("<a class=\"button\" aria-current=\"page\" href=\"/inverter/select?welke=99\">Add inverter</a>")
          : F("<a class=\"button secondary\" href=\"/inverter/select?welke=99\">Add inverter</a>");
    }
    return navigation;
  }

  if (var == "INVERTER_INDEX") return String(iKeuze);
  
  if(var == "<FORMPAGE>"){
    consoleOut(F("found FORMPAGE"));
    return(toSend);  
  }

  if(var == "PAIR_ACTION_STYLE") {
    String bestand = "/Inv_Prop" + String(iKeuze) + ".str";
    if(SPIFFS.exists(bestand)) return "flex"; else return "none";
  }

return String(); //return empty when no match
}


// construct the form and write in a file toSend
void inverterForm() {
    if (iKeuze < 0 || iKeuze > inverterCount ||
        iKeuze >= YC600_MAX_NUMBER_OF_INVERTERS) {
      toSend = F("<section class=\"alert\">Invalid inverter selection.</section>");
      return;
    }

    toSend = FPSTR(INVERTER_GENERAL);
    if (iKeuze < inverterCount) {
        // is there a file iKeuze then
        String bestand = "/Inv_Prop" + String(iKeuze) + ".str";
        if(SPIFFS.exists(bestand)) 
       {
        consoleOut("File exists " + bestand);
        //the file exists so we can display the values 
        toSend.replace("{heading}", "INVERTER " + String(iKeuze + 1));
        toSend.replace("000000", String(Inv_Prop[iKeuze].invSerial)); // handled by the script
        toSend.replace("{location}", String(Inv_Prop[iKeuze].invLocation));
        toSend.replace("{idx}", String(Inv_Prop[iKeuze].invIdx));
        toSend.replace("{cal}", String(Inv_Prop[iKeuze].calib));
        
        // the selectboxes
        if (Inv_Prop[iKeuze].conPanels[0]) { toSend.replace("#1check", "checked");}
        if (Inv_Prop[iKeuze].conPanels[1]) { toSend.replace("#2check", "checked");}
                
        if(Inv_Prop[iKeuze].invType != 1 ) { // when the type = yc600 (0) or ds3 (2)
              
            toSend.replace("onload='showFunction()", "onload='hideFunction()" );
            if(Inv_Prop[iKeuze].invType == 0) 
            { 
              toSend.replace("invtype_0", "selected");
            } else {
             toSend.replace("invtype_2", "selected");  
           }
        } else { // inv type == 1 
          
          //Serial.println(" inverter type = 1");
          toSend.replace("invtype_1", "selected");
           if (Inv_Prop[iKeuze].conPanels[2]) { toSend.replace("#3check", "checked");}
           if (Inv_Prop[iKeuze].conPanels[3]) { toSend.replace("#4check", "checked");}
        }
        
        if(String(Inv_Prop[iKeuze].invID) != "0000") 
        {
           toSend.replace("unpaired", String(Inv_Prop[iKeuze].invID) );
        }

        } else {
          toSend = F("<section class=\"alert\">The selected inverter configuration is missing.</section>");
        }
    } else {
      // The slot is not persistent until Save is pressed. Reset it so an
      // abandoned or previously deleted entry cannot leak into a new form.
      Inv_Prop[iKeuze] = inverters{};
      Inv_Data[iKeuze] = inverterdata{};
      Inv_Prop[iKeuze].invType = 2;
      Inv_Prop[iKeuze].conPanels[2] = false;
      Inv_Prop[iKeuze].conPanels[3] = false;
      toSend.replace("invtype_2", "selected");
      toSend.replace("000000", "");
      toSend.replace("{location}", "");
      toSend.replace("{idx}", "0");
      toSend.replace("{cal}", "0");
      toSend.replace("{heading}", "NEW INVERTER");
    }
// now we have toSend ready to include in the inverterpage
}

void structCopy(int a, int b) {

//  char invLocation[13] = "N/A";
//  char invSerial[13]   = "000000000000";
//  char invID[5]        = "0000";
//  int  invType         = 0;
//  int  invIdx          = 0;
//  bool conPanels[4]    = {true,true,true,true}; 

   // Keep persistent settings and the live snapshot aligned when the final
   // inverter is moved into a deleted index.
   Inv_Prop[a] = Inv_Prop[b];
   Inv_Data[a] = Inv_Data[b];
   polled[a] = polled[b];
   desiredThrottle[a] = desiredThrottle[b];
   inverterLastPollSuccess[a] = inverterLastPollSuccess[b];
   energyMoveInverterState(a, b);
   // now write file a and remove file b
}
