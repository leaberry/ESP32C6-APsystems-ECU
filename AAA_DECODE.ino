// ******************************************************************
//                    decode polling answer
// ******************************************************************
int decodePollAnswer(int which)
{
    char messageToDecode[CC2530_MAX_SERIAL_BUFFER_SIZE] = {0};
  
    char s_d[CC2530_MAX_SERIAL_BUFFER_SIZE] = {0};
    uint8_t Message_begin_offset = 0;    
  
    int t_old = 0;
   
    float en_old[4] = {0}; // energy old
    float energy_old_total = 0;
    int t_extr  = 0;
    int ts = 0;
    bool establishBaseline = false;
    float total_pwr = 0;
  
    //retrieve the poll answer
    strcpy(messageToDecode, readZB(s_d));
    if (readCounter == 0) {
        consoleOut(F("no answer on poll request"));  
        return 50; //no answer
      }

    consoleOut("decodePollAnswer inverter " + String(which) );
 
    char *tail;
    int fault=0;               //FE0164010064  FE034480001400D3


    if(strstr(messageToDecode,  "FE01640100") == NULL) // answer to AF_DATA_REQUEST 00=success
    {
     consoleOut( "AF_DATA_REQUEST failed" );
     fault = 10;    
    } else
    if (strstr(messageToDecode, "FE03448000") == NULL) //  AF_DATA_CONFIRM the 00 byte = success
    {
      consoleOut("no AF_DATA_CONFIRM");
      fault = 11;
    } else
    if (strstr(messageToDecode, "FE0345C4") == NULL) //  ZDO_SRC_RTG_IND
    {
      consoleOut("no route receipt");
      //return 12; // this command seems facultative
    } else 
    if (strstr(messageToDecode, "4481") == NULL)
    {
      consoleOut("no  AF_INCOMING_MSG");
      fault=13;
    }
    if(fault > 9 ) {
       memset(&messageToDecode, 0, sizeof(messageToDecode)); //zero out 
       delayMicroseconds(250); 
      return fault;
    }
   
    if (strlen(messageToDecode) < 223) // this message is not long enough to be valid inverter data
    {
       consoleOut("ignoring, received " + String(messageToDecode) );
       return 15;
    }    
        
    //shorten the message by removing everything before 4481

    tail = split(messageToDecode, "44810000"); // remove the 0000 as well
    //tail = after removing the 1st part
    // in tail at offset 14, 2 bytes with signalQuality reside   

    //sigQ = roundoff( (float) (extractValue(14, 2, 1, 0, tail) * 100 / 254 ), 1);
    //dtostrf((float)(extractValue(14, 2, 1, 0, tail) * 100 / 255 ), 0, 1, Inv_Data[which].sigQ);
    Inv_Data[which].sigQ = (extractValue(14, 2, 1, 0, tail) * 100 / 255 );
    consoleOut( "extracted sigQ = " + String(Inv_Data[which].sigQ) );
    //    dtostrf((float)(extractValue(68, 4, 1, 0, s_d) / 3.8 ), 0, 1, Inv_Data[which].acv);
    //    DebugPrintln( "extracted ACV = " + String(Inv_Data[which].acv) );
// a YC600 message
// tail 06 01 3A 10 14 14 00 71 00 B5 7C FA 00 00 5E | 40 80 00 15 82 15 | FB FB 51 | B1 03 D4 0F 41 17 00 00 74 CF 00 | 00 00 76 70 | 6A 73 D0 6B 04 96 |
//      0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 | 15 16 17 18 19 20 | 21 22 23 | 24 25 26 27 28 29 30 31 32 33 34 | 35 36 37 38 | 39 40 41 42 43 44 |
// np                                                   0  1  2  3  4  5 | 6  7  8  | 9  10 11 12 13 14 15 16 17 18 19 | 20 21 22 23 | 24 25 26 27 28 29 |
//                                                          serial                                                      heath           frequency

// 00 00 | 00 | 00 00 | 00 00 01 | 72 | 07 2D | 88 01 78 62 | E8 20 1F 00 03 05 55 07 3F 03 03 03 01 00 | 00 01 00 
// 45 46 | 47 | 48 49 | 50 51 52 | 53 | 54 55 | 56 57 58 59 | 60 61 62 63 64 65 66 67 68 69 70 71 72 73 | 74 75 76 77 78 79 |
// 30 31 | 32 | 33 34 | 35 36 37 | 38 | 39 40 | 41 42 43 44 | 45 46 47 48 49 50 51 52 53 54 55 56 57 58 | 59
//         C0 |  DC1  |          | C1 |  DC2  |     ACV                                                 | EN0

//000000000000000000000000000000000000000000000000000000000000FEFE3A100E76",300);
//   break;


// a DS3 message
//FE0164010064FE034480001401D2FE0345C43A1000A8FE724481000006013A101414007100B57CFA00005E703000021300fbfb5cbbbb20000200e6ffff000000000000000006f506f9002e00340360138a17a70024001fffff054206900016f62b0018e451ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff3969fefe",300);
// tail starts with serial
// tail  70 30 00 02 13 00 | fb fb 5c bb bb 20 00 02 00   e6 ff ff 00 00   00 00 00 00 00 00 | 06 f5 | 06 f9 | 00 2e | 00 34 | 03 60 | 13 8a | 17 a7 00 24
//       0  1  2  3  4  5  | 6  7  8  9  10 11 12 13 14 | 15 16 17 18 19 | 20 21 22 23 24 25 | 26 27 | 28 29 | 30 31 | 32 33 | 34 35 | 36 37 | 38 39 40
//             serial                                                                          dcv2  |  dcv1 |  dcv3 |       |  ACV  | freq  | time

// | 00 1f ff ff 05 42 | 06  90 | 00 16 f6 2b | 00 18 e4 51 | ff ff ff
// | 42 43 44 45 46 47 | 48  49 | 50 51 52 53 | 54 55 56 57 | 50 59 60 61 62 | 63 64 | 65 66 67 68 | 69 70 71 72 | 73 74 75 
// |                   |  temp  |      en2    |       en1

// ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff3969fefe",300);

// new string
// 703000021300fbfb5cbbbb2000fc0001ffff000000000000000006e506ee015901da036e13882bbb01480026 ff ff 05 25 | 08 43 | 00 3a 40 b2 | 00 35 38 52 | 00 ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff3896fefe
        // attention s_d starts with the serial at offset 30 !!                             44            48temp  50   EN2       54     EN1

//       QT2 Data
//      90 10 00 01 08 17 | FB FB 5C BB BB 30 00 01 00 | 10 FF FF 00 00 | 00 00 00 00 80 00 | 04 6E | 00 C6 | 00 B3 | 00 08 | 00 0C | 00 10 | 02 49 | 09 24 | 09 34 | 09 3F | 08 9F | 07 9F | 13 8B | FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF | 00 06 9A 24 | 00 00 4D 76 | 00 00 15 50 | 00 00 17 4B | FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF | 2C  B5 | FE FE 8B 1D 00 1D powerd on
//      90 10 00 01 08 17 | FB FB 5C BB BB 30 00 01 00 | 10 FF FF 00 00 | 00 00 00 00 80 00 | 04 79 | 00 C6 | 00 C8 | 00 09 | 00 0E | 00 10 | 02 85 | 09 27 | 09 35 | 09 20 | 08 A0 | 07 A9 | 13 88 | FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF | 00 07 68 68 | 00 00 56 55 | 00 00 17 C2 | 00 00 1A 01 | FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF | 2D  29 | FE FE 8B 1D 00 9A powerd on
//      90 10 00 01 08 17 | FB FB 5C BB BB 30 00 01 00 | 10 FF FF 00 00 | 00 00 00 00 80 00 | 04 76 | 00 C7 | 00 C1 | 00 0B | 00 0F | 00 10 | 09 E7 | 09 38 | 09 12 | 09 21 | 08 9F | 08 84 | 13 8A | FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF | 00 20 D2 45 | 00 01 6D BE | 00 00 60 F1 | 00 00 6C 54 | FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF | 2F  56 | FE FE 8B 1D 00 4D powerd on
//      90 10 00 01 08 17 | FB FB 5C BB BB 30 00 01 00 | 10 FF FF 00 00 | 00 08 08 40 82 02 | 04 7B | 00 C5 | 00 05 | 00 01 | 00 02 | 00 02 | 0C 5D | 00 11 | 00 11 | 00 12 | 07 B0 | 08 AE | 00 00 | FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF | 00 28 34 DC | 00 01 BF 54 | 00 00 75 3B | 00 00 83 6A | FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF | 2D  05 | FE FE 8B 1D 00 96 Mains 0V 0A panel 1
//      90 10 00 01 08 17 | FB FB 5C BB BB 30 00 01 00 | 10 FF FF 00 00 | 00 08 08 40 82 02 | 04 7B | 00 C5 | 00 04 | 00 01 | 00 02 | 00 03 | 0C D5 | 00 12 | 00 11 | 00 12 | 06 8E | 08 A9 | 00 00 | FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF | 00 28 3D 2A | 00 01 C1 05 | 00 00 75 69 | 00 00 83 D3 | FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF | 2C  F7 | FE FE 8B 1D 00 3C Mains 0V 0A panel 1
//      90 10 00 01 08 17 | FB FB 5C BB BB 30 00 01 00 | 10 FF FF 00 00 | 00 00 00 00 80 00 | 04 7B | 00 C5 | 00 00 | 00 02 | 00 03 | 00 04 | 27 36 | 09 31 | 09 25 | 09 13 | 05 A8 | 08 08 | 13 85 | FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF | 00 2A 0C E6 | 00 02 10 6E | 00 00 7F 6D | 00 00 9D F8 | FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF | 2D  10 | FE FE 8B 1D 00 8F Mains 400V start up
//      90 10 00 01 08 17 | FB FB 5C BB BB 30 00 01 00 | 10 FF FF 00 00 | 00 00 00 00 A0 00 | 02 7E | 00 C4 | 00 05 | 00 01 | 00 00 | 00 01 | 03 A7 | 09 03 | 09 11 | 09 24 | 00 2C | 07 C2 | 13 8A | FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF | 00 00 2B 75 | 00 00 00 5B | 00 00 01 17 | 00 00 04 B4 | FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF | 2B  35 | FE FE 8B 1D D8 AB Mains 400V 0A pannels
//      90 10 00 01 08 17 | FB FB 5C BB BB 30 00 01 00 | 10 FF FF 00 00 | 00 00 00 00 00 00 | 04 71 | 04 12 | 03 18 | 00 0D | 02 05 | 00 12 | 18 28 | 09 20 | 09 30 | 09 35 | 08 A0 | 09 14 | 13 88 | FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF | 00 4C 2E 4A | 00 02 93 4C | 00 0D 64 2E | 00 03 66 BE | FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF | 2B  67 | FE FE 8B 1D 8B 43 Mains 400V Pannel1 43V 10A Pannel2 39V 6A
//      90 10 00 01 08 17 | FB FB 5C DD DE 01 FF 23 E3 | 00 13 92 14 B4 | 13 EC 00 0A 03 20 | 00 50 | 00 02 | 03 01 | F6 03 | 20 00 | 03 E8 | 00 00 | 00 00 | 00 64 | 00 09 | 60 09 | 24 08 | CA 08 | 98 01 2C 0A 4B 08 02 0B B8 0E 6A 14 0E 42 00 06 22 02 | 0D 02 0D 0E | 57 08 A0 FF | 00 8B FF FF | FF FF FF FF | FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF | 25  6F | FE FE 8B 1D D8 66 Awnser on throtle command
//      90 10 00 01 08 17 | FB FB 5C BB BB 30 00 01 00 | 10 FF FF 00 00 | 00 00 00 00 00 00 | 04 4A | 04 51 | 00 45 | 00 40 | 00 41 | 00 3F | 57 9D | 09 22 | 09 35 | 09 21 | 08 9F | 06 AD | 13 87 | FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF | 00 23 3C 82 | 00 22 51 AB | 00 22 77 E3 | 00 23 36 C1 | FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF | 2E  A8 | FE FE 8B 1D 00 9E   status after throttle command
//      90 10 00 01 08 17 | FB FB 5C DD DE 01 FF 23 E3 | 00 13 92 14 B4 | 13 EC 00 0A 03 20 | 00 50 | 00 02 | 03 01 | F6 03 | 20 00 | 03 E8 | 00 00 | 00 00 | 00 64 | 00 09 | 60 09 | 24 08 | CA 08 | 98 01 2C 0A 4B 08 02 0B B8 0E 6A 14 0E 42 00 06 22 02 | 0D 02 0D 0E | 57 08 A0 FF | 00 8B FF FF | FF FF FF FF | FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF | 25  6F | FE FE 8B 1D 00 4F  Awnser throttle command 0

//      0  1  2  3  4  5  | 6  7  8  9  10 11 12 13 14 | 15 16 17 18 19 | 20 21 22 23 24 25 | 26 27 | 28 29 | 30 31 | 32 33 | 34 35 | 36 37 | 38 39 | 40 41 | 42 43 | 44 45 | 46 47 | 48 49 | 50 51 | 52 53 54 55 56 57 50 59 60 61 62 63 64 65 66 67 68 69 | 70 71 72 73 | 74 75 76 77 | 78 79 80 81 | 82 83 84 85 | 86 87 88 89 90 91 92 93 94 95 96 97 98 99 100| 101 102| 
//             serial                                                   |      status       | dcv1  |  dcv2 |  dci1 |  dci2 |  dci3 |  dci4 |  time |  acv1 |  acv2 |  acv3 |       |  temp |  freq |                                                       |  energy 1   |   energy 2  |  energy 3   |   energy 4  |                                              |        | 


        memset(&s_d[0], 0, sizeof(s_d)); //zero out 
        delayMicroseconds(250);   
        strncpy(s_d, tail + 30, strlen(tail));
        delayMicroseconds(250); //give memset a little bit of time

      if( Inv_Prop[which].invType == 2 ) 
      { // is this a DS3
        consoleOut( "decoding a DS3 inverter");
        // ACV offset 34
        Inv_Data[which].acv[0] = extractValue(68, 4, 1, 0, s_d) / 3.8 ;
        consoleOut( "extracted ACV = " + String(Inv_Data[which].acv[0]) );
        
        // FREQ offset 36
        Inv_Data[which].freq = extractValue(72, 4, 1, 0, s_d)/100;
        consoleOut( "extracted FREQ = " + String(Inv_Data[which].freq) );
        
        // HEATH offset 48        
        Inv_Data[which].heath = extractValue(96, 4, 1, 0, s_d)*0.0198 - 23.84; //18
        consoleOut( "extracted HEATH = " + String(Inv_Data[which].heath) );
        
                // ******************  dc voltage   *****************************************
         //voltage ch1 offset 28
         Inv_Data[which].dcv[0] = extractValue( 52, 4, 1, 0, s_d ) * (float)1 / (float)48;
         // voltage ch2 offset 26
         Inv_Data[which].dcv[1] = extractValue( 56, 4, 1, 0, s_d ) * (float)1 / (float)48;
         // ******************  current   *****************************************
         //current ch1 offset 30
         Inv_Data[which].dcc[0] =  extractValue(60, 4, 1, 0, s_d ) * 0.0125;
         // current ch1 offset 34
         Inv_Data[which].dcc[1] =  extractValue(64, 4, 1, 0, s_d ) * 0.0125;

      } else if(Inv_Prop[which].invType == 3){

        consoleOut( "decoding a QT2 inverter");
                // ACV offset 40
        Inv_Data[which].acv[0] = extractValue(80, 4, 1, 0, s_d) / 10.0 ;
        consoleOut( "extracted ACV0 = " + String(Inv_Data[which].acv[0]) + "V");

        // ACV offset 42
        Inv_Data[which].acv[1] = extractValue(84, 4, 1, 0, s_d) / 10.0 ;
        consoleOut( "extracted ACV1 = " + String(Inv_Data[which].acv[1]) + "V");

        // ACV offset 44
        Inv_Data[which].acv[2] = extractValue(88, 4, 1, 0, s_d) / 10.0 ;
        consoleOut( "extracted ACV2 = " + String(Inv_Data[which].acv[2]) + "V");

        // FREQ offset 50
        Inv_Data[which].freq = extractValue(100, 4, 1, 0, s_d)/100.0;
        consoleOut( "extracted FREQ = " + String(Inv_Data[which].freq) + "Hz");

        // HEATH offset 48        
        Inv_Data[which].heath = extractValue(96, 4, 1, 0, s_d)/100.0; 
        consoleOut( "extracted HEATH = " + String(Inv_Data[which].heath) + "degC");
        
                // ******************  dc voltage   *****************************************
         //voltage ch1 offset 26
         Inv_Data[which].dcv[0] = extractValue( 52, 4, 1, 0, s_d ) * (float)1 / (float)26.3;
         Inv_Data[which].dcv[1] = Inv_Data[which].dcv[0];
         // voltage ch2 offset 28
         Inv_Data[which].dcv[2] = extractValue( 56, 4, 1, 0, s_d ) * (float)1 / (float)26.3;
         Inv_Data[which].dcv[3] = Inv_Data[which].dcv[2];
         // ******************  current   *****************************************
         //current ch1 offset 30
         Inv_Data[which].dcc[0] =  extractValue(60, 4, 1, 0, s_d ) / 89.0;
         // current ch2 offset 32
         Inv_Data[which].dcc[1] =  extractValue(64, 4, 1, 0, s_d ) / 89.0;
         //current ch3 offset 34
         Inv_Data[which].dcc[2] =  extractValue(68, 4, 1, 0, s_d ) / 89.0;
         // current ch4 offset 36
         Inv_Data[which].dcc[3] =  extractValue(72, 4, 1, 0, s_d ) / 89.0;
      } else {
         
        //yc600 or QS1
        //frequency ac voltage and temperature
        Inv_Data[which].acv[0] = extractValue(56, 4, 1, 0, s_d) * ((float)1 / (float)1.3277) / 4 ;
        
        //frquency offset 48        
        Inv_Data[which].freq = 50000000 / extractValue(24, 6, 1, 0, s_d) ;
         
        // temperature offset 10
        Inv_Data[which].heath = extractValue(20, 4, 0.2752F, -258.7F, s_d);

         // ******************  dc voltage   *****************************************
         //voltage ch1 offset 24
         Inv_Data[which].dcv[1] = (extractValue( 48, 2, (float)16, 0, s_d ) + extractValue(46, 1, 1, 0, s_d)) * (float)82.5 / (float)4096;

         //voltage ch2 offset 27
         Inv_Data[which].dcv[0] = (extractValue( 54, 2, (float)16, 0, s_d ) + extractValue(52, 1, 1, 0, s_d)) * (float)82.5 / (float)4096;

         // ******************  current   *****************************************
         //current ch1 offset 22
         Inv_Data[which].dcc[1] = (extractValue(47, 1, (float)256, 0, s_d) + extractValue(44, 2, 1, 0, s_d)) * (float)27.5 / (float)4096; //[A]

         //current ch1 offset 25
         Inv_Data[which].dcc[0] = (extractValue(53, 1, (float)256, 0, s_d) + extractValue(50, 2, 1, 0, s_d)) * (float)27.5 / (float)4096; 


        //********************************************************************************************
        //                                     SQ1
        //********************************************************************************************
        if(Inv_Prop[which].invType == 1) //SQ1 inverter
        {
          //offset 21 -> byte for voltage ch3
          Inv_Data[which].dcv[2] = (extractValue( 42, 2, (float)16, 0, s_d ) + extractValue(40, 1, 1, 0, s_d)) * (float)82.5 / (float)4096;
          //offset 18 -> byte for voltage ch4
          Inv_Data[which].dcv[3] =  ( extractValue( 36, 2, (float)16, 0, s_d ) + extractValue(34, 1, 1, 0, s_d) ) * (float)82.5 / (float)4096;         // ***************************  current  *****************************************
          //offset 19 and 20 for current and status ch3
          Inv_Data[which].dcc[2] = ( extractValue( 41, 1, (float)256, 0, s_d ) + extractValue(38, 2, 1, 0, s_d) ) * (float)27.5 / (float)4096; 
          //offset 16 and 17 for current and status ch4
           Inv_Data[which].dcc[3] = ( extractValue( 35, 1, (float)256, 0, s_d ) + extractValue(32, 2, 1, 0, s_d)) * (float)27.5 / (float)4096;
        }  
      }      
            yield();

/* 
we extract a value out of the inverter answer: en_extr
We have a value from the last poll: en_saved --> en_old
save the new enerrgy value en_extr to en_saved
We subtract these to get the increase en_saved -/- en_old
So en_saved is the value of total energy from the inverter
We keep stacking the increases so we have also en_inc_total
*/
// **********************************************************************
//               calculation of the power per panel
// **********************************************************************
    consoleOut("* * * * polled inverter " + String(which) + " * * * *");

    // 1st the time period 
    // at the start of this we have a value of the t_new[which] of the former poll
    // if this is 0 there was no former poll 
    switch (Inv_Prop[which].invType) {    
      case 0: // yc600
         t_extr = extractValue(34, 4, 1, 0, s_d); // dataframe timestamp
         break;
      case 1: // qs1
         t_extr = extractValue(60, 4, 1, 0, s_d); // dataframe timestamp
         break;
       case 2: //ds3 offset 38
         t_extr = (int)extractValue(76, 4, 1, 0, s_d); // dataframe timestamp ds3
         break;
      case 3: //qt2 offset 38
         t_extr = (int)extractValue(76, 4, 1, 0, s_d); // dataframe timestamp qt2
         break;
    }
    


    // A fresh ECU has no matching energy baseline even though the inverter's
    // uptime and energy counters are already non-zero. Treating those totals as
    // one new interval produced a false power spike and double-counted energy
    // after every ECU reboot. A backwards/non-advancing inverter clock also
    // requires a fresh baseline.
    establishBaseline = t_saved[which] == 0 || t_extr <= t_saved[which];
    ts = establishBaseline ? 0 : t_extr - t_saved[which];
    //whatever happened we remember t_extr as the new time value
    t_saved[which] = t_extr;

    consoleOut("extracted time = " + String(t_extr) + "  the timespan = " + String(ts));

    // now for each channel 
    int increment = 10; // offset to the next energy value
    int btc = 6; // amount of bytes
    int offst = 74; // this is incremented with 10
    if(Inv_Prop[which].invType == 2) { offst = 100; increment = 8; btc = 8; } // for the DS3 we have different offset/increment
    if(Inv_Prop[which].invType == 3) { offst = 140; increment = 8; btc = 8; } // for the QT2 we have different offset/increment
    
    //float total = 0;
    float en_extr = 0;
    float en_incr = 0;
    float en_incr_total = 0;
    float power = 0;
    // for every panel of inverter which we go through this loop


    for(int x = 0; x < 4; x++ ) 
    {   
         if(Inv_Prop[which].conPanels[x]) { // is this panel connected ? otherwise skip

         consoleOut(" * * * decoding panel " + String(x) + " * * * ");           

            // first store the last value of energy_new temporary in energy_old
            // after the calulation we dont need it anymore
            en_old[x] = en_saved[which][x]; // en_new (per inverter per panel needs to be global
            
            consoleOut(" * decoding panel " + String(x) + " * en_old " + String(en_old[x]) );

            // now we extract a new energy_new[which][x] 
            en_extr = extractValue(offst+x*increment, btc, 1, 0, s_d); // offset 74 todays module energy channel 0

            //we calculate a new energy value for this panel and remember it
            if ( Inv_Prop[which].invType == 2) {
              en_saved[which][x] = (en_extr / (float)1000 /100) * 1.66; //[Wh]
            }else if ( Inv_Prop[which].invType == 3) {
              en_saved[which][x] = (en_extr / (float)31600); //[Wh]
            } else {
              en_saved[which][x] = (en_extr * 8.311F / (float)3600); //[Wh]
            }

           
            consoleOut("en_extr " + String(en_extr) + "  en_saved " + String(en_saved[which][x]) );
           

            // calculate the energy increase with or without reset and totalize it
            en_incr = establishBaseline ? 0 : en_saved[which][x] - en_old[x];
            // Counter discontinuities establish a new panel baseline rather
            // than subtracting energy or inventing negative power.
            if (en_incr < 0) en_incr = 0;

            en_incr_total += en_incr; //totalize the energy increase for this poll
            //add en_incr to Inv_Data[which].en_total 
            //Inv_Data[which].en_total += en_incr; // stack the increase
            
            //calculate the power for this panel and remember
            power = ts > 0 ? en_incr / ts * (float)3600 : 0; //[W]
//            if ( Inv_Prop[which].invType == 2) {
//              power = en_incr / ts * (float)3600; //[W]
//            } else {
//              power = en_incr / ts * (float)3600; //[W]
//            }
            Inv_Data[which].power[x] = round1(power);
            total_pwr += power;
            
            yield();
                if (establishBaseline) consoleOut("established first energy/time baseline");
                consoleOut("en_incr " + String(en_incr) + "  power " + String(power) );
     

        } else {
            consoleOut("no panel connected " + String(x));
        }
    }           
     // now we extracted all values per panel and added the increased energy
     // we have invData[which].power[x] and invData[which].en_total
    Inv_Data[which].en_total += en_incr_total; // stack the increase
    energyRecordDelta(which, en_incr_total);
    //dtostrf( total_pwr, 0, 1, Inv_Data[which].power[4]); // write the total value in the struct   
    Inv_Data[which].pw_total = total_pwr;
     yield();
  
        //delay(100);
       consoleOut("total energy increase: " + String(en_incr_total) + "\n");
        //delay(100);
       consoleOut("total energy stacked in Inv_Data[which].en_total: " + String(Inv_Data[which].en_total) + "\n");
  
    return 0;
}       

      



// *******************************************************************************************************************
//                             extract values
// *******************************************************************************************************************
float extractValue(uint8_t startPosition, uint8_t valueLength, float valueSlope, float valueOffset, char toDecode[CC2530_MAX_SERIAL_BUFFER_SIZE])
    {
    char tempMsgBuffer[64] = {0}; // was 254
    yield();
    
    strncpy(tempMsgBuffer, toDecode + startPosition, valueLength);
    // now we have the part of the string "startposition - number of bytes"
    // we calculate the value it is representing with strtol and correct it with valueSlope and offset
    yield();
    return (valueSlope * (float)strtol(tempMsgBuffer, 0, 16)) + valueOffset;
}

// ************************************************************************************
//                mqtt send polled data
// ************************************************************************************
void mqttPoll(int which) {

if(Mqtt_Format == 0) return;  

  char Mqtt_send[40]={0};
  strlcpy(Mqtt_send, Mqtt_outTopic, sizeof(Mqtt_send));
  size_t mqttTopicLength = strlen(Mqtt_send);
  if(mqttTopicLength && Mqtt_send[mqttTopicLength-1] == '/' ) {
    strlcat(Mqtt_send, String(Inv_Prop[which].invIdx).c_str(), sizeof(Mqtt_send));
  }
  bool reTain = false;
  char pan[96]={0};
  char tail[64]={0};
  char toMQTT[300]={0};

// the json to domoticz must be something like {"idx" : 7, "nvalue" : 0,"svalue" : "90;2975.00"}
 
   switch( Mqtt_Format)  { 
    case 1: 
       snprintf(toMQTT, sizeof(toMQTT), "{\"idx\":%d,\"nvalue\":0,\"svalue\":\"%.1f;%.2f\"}" , Inv_Prop[which].invIdx , Inv_Data[which].pw_total, Inv_Data[which].en_total);
       break;
       // length 46

     case 2: // for not domoticz we have a different mqtt string how does this look?
       snprintf(toMQTT, sizeof(toMQTT), "{\"inv\":\"%d\",\"temp\":\"%.1f\",\"p0\":\"%.1f\",\"p1\":\"%.1f\",\"p2\":\"%.1f\",\"p3\":\"%.1f\",\"energy\":\"%.2f\"}" ,which, Inv_Data[which].heath, Inv_Data[which].power[0],Inv_Data[which].power[1], Inv_Data[which].power[2], Inv_Data[which].power[3], Inv_Data[which].en_total);
       break;  
       
   case 3:
       snprintf(toMQTT, sizeof(toMQTT), "{\"inv_serial\":\"%s\",\"freq\":%.1f,\"temp\":%.1f,\"acv\":%.1f,\"signal\":%.1f,\"polled\":%d" , Inv_Prop[which].invSerial, Inv_Data[which].freq, Inv_Data[which].heath, Inv_Data[which].acv[0], Inv_Data[which].sigQ, polled[which]);
       //char pan[50]={0};
       if( Inv_Prop[which].invType == 1 || Inv_Prop[which].invType == 3) { // qs1 or qt2
           snprintf(pan, sizeof(pan), ",\"dcv\":[%.1f,%.1f,%.1f,%.1f]", Inv_Data[which].dcv[0], Inv_Data[which].dcv[1],Inv_Data[which].dcv[2],Inv_Data[which].dcv[3]);
           strlcat(toMQTT, pan, sizeof(toMQTT));
           snprintf(pan, sizeof(pan), ",\"dcc\":[%.1f,%.1f,%.1f,%.1f]", Inv_Data[which].dcc[0], Inv_Data[which].dcc[1],Inv_Data[which].dcc[2],Inv_Data[which].dcc[3]);
           strlcat(toMQTT, pan, sizeof(toMQTT));
           snprintf(pan, sizeof(pan), ",\"pwr\":[%.1f,%.1f,%.1f,%.1f]", Inv_Data[which].power[0], Inv_Data[which].power[1],Inv_Data[which].power[2],Inv_Data[which].power[3]);
           strlcat(toMQTT, pan, sizeof(toMQTT));
           snprintf(pan, sizeof(pan), ",\"pwr_total\":%.2f", Inv_Data[which].pw_total);
           strlcat(toMQTT, pan, sizeof(toMQTT));
           snprintf(pan, sizeof(pan), ",\"en\":[%.2f,%.2f,%.2f,%.2f]", en_saved[which][0], en_saved[which][1], en_saved[which][2], en_saved[which][3]);
           strlcat(toMQTT, pan, sizeof(toMQTT));
           snprintf(pan, sizeof(pan), ",\"energy_total\":%.2f}", Inv_Data[which].en_total);
           strlcat(toMQTT, pan, sizeof(toMQTT));
       } else {
           snprintf(pan, sizeof(pan), ",\"dcv\":[%.1f,%.1f]", Inv_Data[which].dcv[0], Inv_Data[which].dcv[1]);
           strlcat(toMQTT, pan, sizeof(toMQTT));
           snprintf(pan, sizeof(pan), ",\"dcc\":[%.1f,%.1f]", Inv_Data[which].dcc[0], Inv_Data[which].dcc[1]);
           strlcat(toMQTT, pan, sizeof(toMQTT));
           snprintf(pan, sizeof(pan), ",\"pwr\":[%.1f,%.1f]", Inv_Data[which].power[0], Inv_Data[which].power[1]);
           strlcat(toMQTT, pan, sizeof(toMQTT));
           snprintf(pan, sizeof(pan), ",\"pwr_total\":%.2f", Inv_Data[which].pw_total);
           strlcat(toMQTT, pan, sizeof(toMQTT));
           snprintf(pan, sizeof(pan), ",\"en\":[%.2f,%.2f]", en_saved[which][0], en_saved[which][1]);
           strlcat(toMQTT, pan, sizeof(toMQTT));
           snprintf(pan, sizeof(pan), ",\"energy_total\":%.2f}", Inv_Data[which].en_total);
           strlcat(toMQTT, pan, sizeof(toMQTT));
       }
       reTain=true;
       break;
    case 4:
        snprintf(toMQTT, sizeof(toMQTT), "{\"inv_serial\":\"%s\",\"freq\":%.1f,\"temp\":%.1f,\"acv\":%.1f" , Inv_Prop[which].invSerial, Inv_Data[which].freq, Inv_Data[which].heath, Inv_Data[which].acv[0]);      
        snprintf(pan, sizeof(pan), ",\"ch0\":[%.1f,%.1f,%.1f,%.2f]", Inv_Data[which].dcv[0], Inv_Data[which].dcc[0], Inv_Data[which].power[0], en_saved[which][0]);
        strlcat(toMQTT, pan, sizeof(toMQTT));
        snprintf(pan, sizeof(pan), ",\"ch1\":[%.1f,%.1f,%.1f,%.2f]", Inv_Data[which].dcv[1], Inv_Data[which].dcc[1], Inv_Data[which].power[1], en_saved[which][1]);
        strlcat(toMQTT, pan, sizeof(toMQTT));

        if( Inv_Prop[which].invType == 1 || Inv_Prop[which].invType == 3 ) { // add ch2 and ch3
            snprintf(pan, sizeof(pan), ",\"ch2\":[%.1f,%.1f,%.1f,%.2f]", Inv_Data[which].dcv[2], Inv_Data[which].dcc[2], Inv_Data[which].power[2], en_saved[which][2]);
            strlcat(toMQTT, pan, sizeof(toMQTT));
            snprintf(pan, sizeof(pan), ",\"ch3\":[%.1f,%.1f,%.1f,%.2f]", Inv_Data[which].dcv[3], Inv_Data[which].dcc[3], Inv_Data[which].power[3], en_saved[which][3]);
            strlcat(toMQTT, pan, sizeof(toMQTT));
        }
        snprintf(tail, sizeof(tail), ",\"totals\":[%.1f,%.2f]}", Inv_Data[which].pw_total, Inv_Data[which].en_total);
        strlcat(toMQTT, tail, sizeof(toMQTT));
        reTain=true;
        break;
     
     case 5: // for thingspeak we have a different format
       snprintf(toMQTT, sizeof(toMQTT), "field1=%d&field2=%.1f&field3=%.1f&field4=%.1f&field5=%.1f&field6=%.1f&field7=%.2f&status=MQTTPUBLISH" ,which, Inv_Data[which].heath, Inv_Data[which].power[0],Inv_Data[which].power[1], Inv_Data[which].power[2], Inv_Data[which].power[3], Inv_Data[which].en_total);
       reTain=false;
       break;
    }


   // mqttConnect() checks first if we are connected, if not we connect anyway
   if(mqttConnect() ) MQTT_Client.publish ( Mqtt_send, toMQTT, reTain );
 }

// not domoticz: {"inv_serial":"123456789012","temp":"12,3","p0":"123",p1":"123",p2":"123",p3":"123","energy":"345"}
//format 2 {"inv_serial":"408000158211","temp":"18.0","p0":"88.4","p1":"88.0","energy":"174.4"}
//format3 {"inv_serial":"408000158211","acv":68.2,"freq":50.0,"temp":18.0,"dcv":[36.8,37.0],"dcc":[4.3,3.0],"pwr":[nan,nan],"totalen":174.35}
//format3 {"inv_serial":"408000158211","acv":68.2,"freq":50.0,"temp":18.0,"ch0":[dcv0,dcc0,power0,enSaved0],"ch2":[dcv1,dcc1,power1,en_saved1] etc
