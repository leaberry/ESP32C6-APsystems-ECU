void querying(int which) {
    //polled[which]=false; //nothing is displayed on webpage

    consoleOut("query inverter " + String(which));
    char queryCommand[65] = {0};
    char ecu_id_reverse[13];
    ECU_REVERSE().toCharArray(ecu_id_reverse, 13);    
    //                                            2401  1414060000100F13  FBFB06DE000000000000E4FEFE
    snprintf(queryCommand, sizeof(queryCommand), "2401%s1414060001000F13%sFBFB06DE000000000000E4FEFE", Inv_Prop[which].invID, ecu_id_reverse);
    // put in the CRC at the end of the command done in sendZigbee
    consoleOut("raw queryCommand :" + String(queryCommand));
    
    if(zigbeeUp != 1) 
    {
      consoleOut(F("skipping query, coordinator down!")); //
      return;
    }
    
    empty_serial2();
    if (!sendZB(queryCommand)) return;

    // decodeQueryAnswer will read and analyze the answer   
    errorCode = decodeQueryAnswer(which);     
    switch( errorCode )
    {
        case 0:
                 //polled[which] = true;
                 //yield();
                 //mqttPoll(which); //
                 yield();
                 break;
        default:
              consoleOut("query failed with errorcode " + String(errorCode)); 
    }
}


// ******************************************************************
//                    decode query zigbee answer
// ******************************************************************
int decodeQueryAnswer(int welke)
{
    char messageToDecode[CC2530_MAX_SERIAL_BUFFER_SIZE] = {0};
      char s_d[CC2530_MAX_SERIAL_BUFFER_SIZE] = {0};
    uint8_t Message_begin_offset = 0;    
    char *payload;
    int fault=0; 
    consoleOut("decoding inverter " + String(welke));
    // Control acknowledgments may arrive before the fragmented limit readback.
    // Bound both time and frame count, and never interpret an ACK as a limit.
    const uint32_t started = millis();
    for (unsigned attempt = 0; attempt < 8 && millis() - started < 4000; ++attempt) {
      strcpy(messageToDecode, readZB(s_d));
      if (readCounter == 0) return 50;
      if (!strstr(messageToDecode, "4481")) continue;
      payload = split(messageToDecode, "FBFB");
      if (!payload) continue;
      const bool ds3 = Inv_Prop[welke].invType == 2;
      if (strncmp(payload, ds3 ? "5CDDDE0104" : "4DDE", ds3 ? 10 : 4)) {
        consoleOut("ignoring non-readback power-control response");
        continue;
      }
      if (strlen(payload) < 14 || !strstr(payload, "FEFE")) return 15;
      consoleOut("payload " + String(payload));

    // we must handle the DS3 and YC600 differently 
    
    if(Inv_Prop[welke].invType != 2) {
    // for test we give payload a content
    consoleOut("decoding a YC600 / QS1 ");
    // we know the tail is
    //FBFB4DDE041105440FE5020F32B003CF05440FE5020F32B0066604CC0EA3A804D70214050C100FD80ED07A0F32B0056A054F0>
    //FE480068ACE8ACE000130103030190604001D1B153B6600000000FEFE3A100E5D
    // the powervalue is right behind 2B66
    char target[5] = {"3B66"};

    char *ptr = strstr(payload, target); // find "3B66"
    if (ptr != NULL && ptr - payload >= 4) {
        char before[5];
        strncpy(before, ptr - 4, 4);
        before[4] = '\0';
        for (int i = 0; i < 4; ++i) if (!isxdigit((unsigned char)before[i])) return 15;
        int decimalValue = (int)strtol(before, NULL, 16) / 28.89; // convert from hex string to int
        //we must compare decimalValue with maxPower
        // so we have calculate it back with the calibrateFactor
        int programmedVal = decimalValue - Inv_Prop[welke].calib;
        //double result = decimalValue / 28.89;
        consoleOut("power value YC600 = " + String(decimalValue));
        // this should match with the set throttle value which is
        consoleOut("desiredThrottle[welke] = " + String(desiredThrottle[welke]));
        
        if(abs(desiredThrottle[welke] - programmedVal) > 2) return 16;
        } else {
        consoleOut("0x3B66 not found or not enough characters before it.");
        return 15;
       }
    }  else {// end if(invType != 2)
    
    // this is ds3
    consoleOut("decoding a DS3 ");
    //the payload looks like FBFB5CDDDE010426E20013BA14B413EC000A032000500003DD03A403200003E80000000000640003DD03A503350304012C060D03FF045F0E93140E3204890258001374136F125C0014032007D023A6031401BF03D9FFFFFFFFFFFF23A6C8FF1C01FEFEA2F6734E  
    // for test we give payload a conten
    //strcpy(payload, "FBFB5CDDDE0104 26E2  0013BA14B413EC000A032000500003DD03A403200003E80000000000640003DD03A503350304012C060D03FF045F0E93140E3204890258001374136F125C0014032007D023A6031401BF03D9FFFFFFFFFFFF23A6C8FF1C01FEFEA2F6734E");
     
     char powval[5] = {0}; // 4 chars + null terminator
     for (int i = 10; i < 14; ++i) if (!isxdigit((unsigned char)payload[i])) return 15;
     memcpy(powval, payload + 10, 4); // copy "26E2"
     int decimalValue = (int)strtol(powval, NULL, 16) / 16.59;
     String term="power value DS3 = " + String(powval) + " this is dec. " + String(decimalValue);
     
     //we must compare decimalValue with maxPower
     // so we have calculate it back with the calibrateFactor
     int programmedVal = decimalValue - Inv_Prop[welke].calib;

     consoleOut(term);
     consoleOut("desiredThrottle[welke] = " + String(desiredThrottle[welke]));
     if(abs(desiredThrottle[welke] - programmedVal) > 2) return 15;
    }
    
    return 0;
    }
    return 50; // No matching readback before the bounded receive window ended.
} 