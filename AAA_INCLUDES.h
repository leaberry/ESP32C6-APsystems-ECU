#include <ArduinoJson.h>

#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <AsyncEventSource.h>
//#include "esp_heap_caps.h"
/* this was compiled with board version
 * latest mods tosend locally declared for info- and detailspages
 *  edit readZigbee
 *  made processIncomingByte redundant
 *  moved crc and slen to sendZigbee
 *  added a debugmessage to sendzigbee when (diag)
 *  removed the line 336 memset crashed the zigbee
 *  testing sending via sendZB 
 *  changed the notation of the invID, saved as was extracted so inverting not needed anymore
 *  experimenting with extractValue() )AAA-DECODE
 *  used arduinojson to make the jsons
 *  made all inverterdata float()
 *  introduced events. // when do we need to refresh
 *  when new data (after a poll, sleep/wakeup midnight)
 *  compressed webpages and scripts combined them to one file
 *  changed the order of handles in the server, the most popular first
 */ 

#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <esp_coexist.h>
#include <DNSServer.h> 

#include "OTA.h"
#include <Update.h>
#include <esp_ota_ops.h>
//#include <Hash.h>
#include "PSACrypto.h"
#define VERSION  "ESP32C6-ECU_v1_4_4"

#include <TimeLib.h>
#include <time.h>
#include <sunMoon.h>

// Classic ESP32 brownout-register workaround removed: not valid on ESP32-C6.
#include <esp_task_wdt.h> // ha
#include <rtc_wdt.h>
           
#include "SPIFFS.h"
#include "FS.h"
#include <EEPROM.h>
#include <ArduinoJson.h>
//#include "AsyncJson.h"
#include <Arduino.h>

// ESP32-C6 native 802.15.4 radio. APsystems uses proprietary cross-network
// Zigbee framing that a standards-enforcing coordinator stack rejects, so the
// ECU owns the MAC directly and implements only the required NWK/APS subset.
#include "esp_ieee802154.h"
#include "esp_coexist.h"
#include <mbedtls/aes.h>

#include <Preferences.h>
Preferences preferences;
// Forward declaration required by Arduino's generated .ino prototypes.
struct SunSpecValues;
struct EnergyDayRecord;
struct EcuTimeZone;
struct GridProtectionSnapshot;
enum GridScale : uint8_t;
extern const char GRID_PROFILE_FILE[];
extern File gridProfileUploadFile;
extern uint8_t gridProfileTarget;
extern const char ENERGY_PAGE[];
bool gridProfileValidateFile();
//#include "Async_TCP.h" //we include the customized one

//#include <ESPAsyncWebServer.h>
AsyncWebServer server(80);
AsyncEventSource events("/events"); 
AsyncWebSocket ws("/ws");

#include <PubSubClient.h>

#include <NTPClient.h>
#include <WiFiUdp.h>
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");
const byte DNS_PORT = 53;
DNSServer dnsServer;
#include "HTML.h"
#include "FAVICON_SUN.h"
//#include "AAA_CONFIG_INVERTERS.H"
#include "AAA_MENUPAGE.h"
#include "DETAILSPAGE.h"
#include "AAA_HOMEPAGE.h"
#include "AAA_INVERTERS_UI.h"
#include "AA_CONSOLE_UI.h"
#include "eventSource.h"
/*
 * if we define TEST we have a special environment to test the decoding of a string
 * for the different invertertypes. We only test inv0. If we set this up as a DS3
 * a ds3 string will be analyzed.
 * - no automatic polling
 * - no automatic healthcheck
 * - no start of zigbee at boot\
 * - in the console an option to test inv 0 '10;TESTINV'
 * - more debug information in the console
 */

//#define TEST // prevents healthcheck and polling

#ifdef TEST
int testCounter = 0;
#endif

  //bool USB_serial = true;
  char ssid[33] = ""; // was 33 
  char pass[64] = ""; // was 40
  bool tryConnectFlag = false;
// variables concerning time and timers
  WiFiUDP Udp; 
  //time_t daybeginTime = 0;
  time_t switchoffTime = 0;
  time_t switchonTime = 0;
  bool dayTime=true;

//  byte mDay = 0;
//  String  maan = "";
  uint8_t actionFlag = 0;
  uint8_t midnightFlag = 0;
 // variables wificonfig
  char pswd[33] = "";
  char userPwd[33] = "";
  float longi = 0;
  float lati = 0;
  char gmtOffset[5] = "";  //+5.30 GMT
  bool zomerTijd = true;
  char timeZoneId[24] = "UTC";
  bool daylightPolling = true;
  bool locationConfigured = false;
  int16_t currentUtcOffsetMinutes = 0;
  char static_ip[16] = "";
  uint8_t securityLevel = 6;


// ***************************************************************************
//                             variables ECU
// ***************************************************************************
// ZIGBEE_globals
#define CC2530_MAX_SERIAL_BUFFER_SIZE 512
#define YC600_MAX_NUMBER_OF_INVERTERS 9  // 0 1 2 3 4 5 6 7 8
char inMessage[CC2530_MAX_SERIAL_BUFFER_SIZE] = {0};
int readCounter = 0;
//char messageHead[5];
int diagNose = 0; // initial true but after a successful healthcheck false
bool Polling = true; // automatic polling is enabled for new and migrated installs
uint32_t pollIntervalSeconds = 300; // complete fleet round cadence; web configurable
int errorCode=10;
//int recovered = 0;
  char txBuffer[90];

  int t_saved[YC600_MAX_NUMBER_OF_INVERTERS] = {0};
  float en_saved[YC600_MAX_NUMBER_OF_INVERTERS][4] = {0};
  
  char InputBuffer_Serial[50]; // need to be global

typedef struct{
  char invLocation[13] = "N/A";
  char invSerial[13]   = "000000000000";
  char invID[5]        = "0000";
  int  invType         = 0;
  int  invIdx          = 0;
  int  calib           = 0;
  bool conPanels[4]    = {true,true,true,true};
  //int  maxPower        = 500;
  bool throttled       = false;
  // APsystems' proprietary L1 transport encryption (not Zigbee NWK security).
  // In auto mode this is selected by serial[1] == '2' and confirmed on RX.
  bool encrypted       = false;
} inverters;
inverters Inv_Prop[9]; 
time_t inverterLastPollSuccess[YC600_MAX_NUMBER_OF_INVERTERS] = {};

typedef struct{ 
float freq = 0.0;
float sigQ = 0.0;
float heath = 0.0;
float acv = 0.0;
float dcc[4] = {0.0, 0.0, 0.0, 0.0};              // ampere <100 
float dcv[4] = {0.0, 0.0, 0.0, 0.0};              // volt <100
float power[4] = {0.0, 0.0, 0.0, 0.0};       //watt < 1000
float pw_total = 0.0;
float en_total = 0;
char firmwareVersion[20] = "unknown";
uint8_t modelCode = 0;
} inverterdata;
inverterdata Inv_Data[9];
  
 //byte throtTled = 0b00000000;
 int desiredThrottle[10] = {0,0,0,0,0,0,0,0,0,0};
 bool polled[9]={false,false,false,false,false,false,false,false,false};
 uint8_t zigbeeUp = 11; // initial allways initializing, this changes to 1 = up or 0 not up after initial healthcheck
 int pollOffset = 0;
 int inverterKeuze=0;
 int inverterCount=0;
 char ECU_ID[13] = "";

char requestUrl[32] = {"/"}; // return path for asynchronous web actions

// variables mqtt ********************************************
  char  Mqtt_Broker[30]=    {""};
//  char  Mqtt_inTopic[16] =  {"ESP-ECU/in"};
  char  Mqtt_outTopic[40] = {""};
  char  Mqtt_Username[26] = {""};
  char  Mqtt_Password[26] = {""};
  //char  Mqtt_Clientid[26] = {""};
  char  Mqtt_Port[5] =  {""};
  int   Mqtt_Format = 0; 
int   event = 0;
long  mqtt_lastConnect = 0;

  int dst;
  int iKeuze;
//  int inverterTopoll = 0;
  bool timeRetrieved = false;
  int networksFound = 0; // used in the portal
  int datum = 0; //

  unsigned long previousMillis = 0;        // will store last temp was read
  static unsigned long laatsteMeting = 0; //wordt ook bij OTA gebruikt en bij wifiportal
  static unsigned long lastCheck = 0; //wordt ook bij OTA gebruikt en bij wifiportal

#define LED_AAN    HIGH   //sinc
#define LED_UIT    LOW
#ifndef APS_BUTTON_PIN
#define APS_BUTTON_PIN    0
#endif
#ifndef APS_LED_PIN
  #ifdef LED_BUILTIN
    #define APS_LED_PIN LED_BUILTIN
  #else
    #define APS_LED_PIN 2
  #endif
#endif
#define knop              APS_BUTTON_PIN
#define led_onb           APS_LED_PIN
// No external Zigbee reset pin: the radio is integrated in the ESP32-C6.
//#define ZB_TX             17 // D8

// we use this string only to send webpages
String toSend = "";
 
int value = 0; 

//int aantal = 0;
int resetCounter=0;
bool apFlag=false;
// *******************************  log *************************************
//// variables To record and display last events on the home page.
//struct logEvent {
//  String    Log_date ;
//  String    Log_kind ;
//  String    Log_message;
//};
// *******************************  log *************************************
// variables To record and display last events on the home page.
#define Log_MaxEvents 18 
 
 typedef struct {
  char date[14] ;
  int  kind ; // zigbee, system, mqtt, pairing
  char  message[13] ;
} logEvent;
logEvent Log_Events[Log_MaxEvents];
bool Log_MaxReached = false;
byte logNr = 0;

WiFiClient espClient ;
PubSubClient MQTT_Client ( espClient ) ;
int Mqtt_stateIDX = 123;
//bool getallTrigger = false;
//bool reloadTrigger = false;

#include <Ticker.h>
Ticker resetTicker;
