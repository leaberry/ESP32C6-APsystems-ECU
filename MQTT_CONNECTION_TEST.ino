#include <new>
// A temporary worker keeps DNS/TCP/MQTT waits out of the web and polling loops.
// Only one test may run at a time. No settings or retained messages are written.
struct MqttConnectionJob {
  char broker[30];
  char user[26];
  char password[26];
  uint16_t port;
};
portMUX_TYPE mqttConnectionMux = portMUX_INITIALIZER_UNLOCKED;
bool mqttConnectionRunning = false;
uint32_t mqttConnectionId = 0;
int mqttConnectionResult = -1;

const char *mqttConnectionMessage(int state) {
  switch (state) {
    case 0: return "Connection successful. The broker accepted the MQTT login. Topic permissions were not tested.";
    case 1: return "Connection failed: broker rejected the MQTT protocol version.";
    case 2: return "Connection failed: broker rejected the temporary client ID.";
    case 3: return "Connection failed: broker is unavailable.";
    case 4: return "Connection failed: broker rejected the username or password.";
    case 5: return "Connection failed: broker denied authorization.";
    case -4: return "Connection failed: timed out waiting for the broker's MQTT reply.";
    case -3: return "Connection failed: broker closed the connection.";
    default: return "Connection failed: could not reach the broker. Check its address, port, network and plain MQTT/TCP support.";
  }
}

void mqttConnectionWorker(void *argument) {
  auto job=static_cast<MqttConnectionJob *>(argument);
  int result;
  {
    WiFiClient transport;
    transport.setConnectionTimeout(3000);
    PubSubClient probe(transport);
    probe.setServer(job->broker,job->port);
    probe.setSocketTimeout(3);
    char clientId[64];
    snprintf(clientId,sizeof(clientId),"aps-test-%s-%08lx",ECU_ID,(unsigned long)esp_random());
    bool connected=job->user[0] ? probe.connect(clientId,job->user,job->password) : probe.connect(clientId);
    result=connected ? 0 : probe.state();
    probe.disconnect();
    transport.stop();
  }
  memset(job,0,sizeof(*job));
  delete job;
  portENTER_CRITICAL(&mqttConnectionMux);
  mqttConnectionResult=result;
  mqttConnectionRunning=false;
  portEXIT_CRITICAL(&mqttConnectionMux);
  vTaskDelete(nullptr);
}

void mqttConnectionStart(AsyncWebServerRequest *request) {
  if (!settingsAuthorized(request)) return;
  const char *names[]={"mqtAdres","mqtPort","mqtUser","mqtPas"};
  const size_t limits[]={29,4,25,25};
  String values[4];
  for(size_t i=0;i<4;++i) {
    if(!request->hasParam(names[i],true)) {request->send(400,"text/plain","Missing broker settings");return;}
    values[i]=request->getParam(names[i],true)->value();
    if(values[i].length()>limits[i]) {request->send(400,"text/plain","Broker setting too long");return;}
  }
  char *end=nullptr;
  long port=strtol(values[1].c_str(),&end,10);
  if(values[0].isEmpty() || values[1].isEmpty() || *end || port<1 || port>9999) {
    request->send(400,"text/plain","Enter a broker address and port from 1 to 9999");return;
  }
  auto job=new(std::nothrow) MqttConnectionJob{};
  if(!job) {request->send(503,"text/plain","Not enough memory for a connection test");return;}
  strlcpy(job->broker,values[0].c_str(),sizeof(job->broker));
  strlcpy(job->user,values[2].c_str(),sizeof(job->user));
  strlcpy(job->password,values[3].isEmpty()?Mqtt_Password:values[3].c_str(),sizeof(job->password));
  job->port=(uint16_t)port;
  portENTER_CRITICAL(&mqttConnectionMux);
  bool busy=mqttConnectionRunning;
  uint32_t id=mqttConnectionId;
  if(!busy) {mqttConnectionRunning=true;mqttConnectionResult=-1;id=++mqttConnectionId;}
  portEXIT_CRITICAL(&mqttConnectionMux);
  if(busy) {memset(job,0,sizeof(*job));delete job;request->send(409,"text/plain","Another connection test is running. Try again shortly.");return;}
  if(xTaskCreate(mqttConnectionWorker,"mqtt-test",4096,job,1,nullptr)!=pdPASS) {
    memset(job,0,sizeof(*job));delete job;
    portENTER_CRITICAL(&mqttConnectionMux);mqttConnectionRunning=false;portEXIT_CRITICAL(&mqttConnectionMux);
    request->send(503,"text/plain","Could not start connection test");return;
  }
  request->send(202,"application/json",String("{\"id\":")+String(id)+"}");
}

void mqttConnectionStatus(AsyncWebServerRequest *request) {
  if(!settingsAuthorized(request)) return;
  portENTER_CRITICAL(&mqttConnectionMux);
  uint32_t id=mqttConnectionId;bool running=mqttConnectionRunning;int result=mqttConnectionResult;
  portEXIT_CRITICAL(&mqttConnectionMux);
  if(!request->hasParam("id") || request->getParam("id")->value()!=String(id) || !id) {
    request->send(409,"text/plain","This connection test is no longer available. Run a new test.");return;
  }
  JsonDocument doc;doc["running"]=running;doc["success"]=!running && result==0;
  doc["message"]=running ? "Testing broker connection..." : mqttConnectionMessage(result);
  String body;serializeJson(doc,body);request->send(200,"application/json",body);
}
