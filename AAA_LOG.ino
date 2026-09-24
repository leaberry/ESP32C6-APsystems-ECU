//<script type="text/javascript" src="SECURITY"></script>

const char LOGPAGE [] PROGMEM = R"=====(
<!DOCTYPE html><html><head><meta charset='utf-8'>
<title>ESP32-ECU</title>
<meta http-equiv="refresh" content="60">
<meta name="viewport" content="width=device-width, initial-scale=1">

<link rel="stylesheet" type="text/css" href="/STYLESHEET">
<style>
#lijst {
  font-family: "Trebuchet MS", Arial, Helvetica, sans-serif;
  border-collapse: collapse;
  width: 380px;
  font-size:14px;
  border: 1px solid;
  text-align: left;
}
#lijst td  {
padding-left: 10px;
border: 1px solid;
}

#lijst tr:nth-child(even){background-color: #f2f2f2;
  border: 1px solid #ddd;
  padding: 6px;
}
#lijst th {
  padding-top: 5px;
  padding-bottom: 5px;
  padding-left: 10px;
  text-align: left;
  background-color: #4CAF50;
  color: white;
}
.th1 { width:30%%; }
.th2 { width:20%%; }
.th3 { width:50%%; }
tr {height:20px;}

@media only screen and (max-width: 600px) {
#lijst{ font-size:12px; width: 320px;}
tr {width:94vw;}
.th1 { width:25%%; }
.th2 { width:15%%; }
}
</style>
<script type="text/javascript" src="SECURITY"></script>
<script>function cl() {window.location.href='/menu';}</script>
</head>
<body><center>
<div id='msect'>
<div id='menu'><a href="/menu" class='close'>&times;</a></div>

<kop>ESP-ECU JOURNAL</kop>

<div class='divstijl'><center>
<br>
<table id='lijst'><tr><th class='th1'>Time</th><th class='th2'>Type</th><th class='th3'>Description</th></tr>
%rows%
</table></center></div></body></html>
)=====";

// ************************************************************************************
//                      U P D A T E    L O G
// ************************************************************************************
void Update_Log(int what, const char* message) {
  const time_t current = ecuNow();
  logEvent &event = Log_Events[logNr];
  snprintf(event.date, sizeof(event.date), "%d-%d:%d:%d ", ecuDay(current),
           ecuHour(current), ecuMinute(current), ecuSecond(current));
  event.kind = what;
  snprintf(event.message, sizeof(event.message), "%s", message ? message : "");
  if (++logNr >= Log_MaxEvents) {
    logNr = 0;
    Log_MaxReached = true;
  }
}


//void Clear_Log(AsyncWebServerRequest *request) {
//
//    if(!checkRemote( request->client()->remoteIP().toString()) ) {
//        if(logNr != 0) {
////        for (int i=0; i <= Log_MaxEvents; i++) {
////           Log_date[20][0] = '\0';
////           Log_kind[20][0] = '\0';
////           Log_message[20][0] = '\0'; 
////        }
//        logNr = 0;//start again
//        Log_MaxReached = false;     
//        //Serial.println("log cleared");   
//        }
//    } 
//}



String putList(const String& var) {
  if (var != "rows") return String();
  String content;
  content.reserve(4096);
  const uint8_t count = Log_MaxReached ? Log_MaxEvents : logNr;
  int j = logNr;
  for (uint8_t i = 0; i < count; ++i) {
    if (--j < 0) j = Log_MaxEvents - 1;
    const char *kind = "unknown";
    switch (Log_Events[j].kind) {
      case 1: kind = "system"; break;
      case 2: kind = "zigbee"; break;
      case 3: kind = "mqtt"; break;
      case 4: kind = "pairing"; break;
    }
    content += "<tr><td>" + webEscape(Log_Events[j].date) + "</td><td>";
    content += kind;
    content += "</td><td>" + webEscape(Log_Events[j].message) + "</td></tr>";
  }
  return content;
}
