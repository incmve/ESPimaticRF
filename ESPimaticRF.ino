String LastReceived;
int BWmode;
String bwlistjson;
String SerialString;
String PrevHash = "";
int UDPrepeat;
String receiverPin;
String transmitterPin;

String protocolsjson;
String PrevRcv;
int RcvTime;
String apikey;
String pimaticIP;
long int pimaticPort;
int receiveAction;
int transmitAction;

#include <SerialCommand.h>
#include <RFControl.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <FS.h>
#include <ArduinoJson.h>
#include <WiFiUdp.h>

WiFiUDP Udp;
unsigned int localUdpPort;  // local port to listen on
char incomingPacket[355];  // buffer for incoming packets
char  replyPacket[] = "ACK";  // a reply string to send back

// Multicast declarations
IPAddress ipMulti(239, 0, 0, 57);
unsigned int portMulti = 12345;      // local port to listen on

ESP8266WebServer server(80);
WiFiClient client;

void argument_error();

SerialCommand sCmd;

#include "rfcontrol_command.h"

String ssidStored;
String passStored;
const char* APssid = "ESPimaticRF";
const char* APpassword = "espimaticrf";
String WMode = "";
int SendDone = 0;
File UploadFile;
String fileName;
String sep = "____";
String Mode;




void digital_read_command();
void digital_write_command();
void analog_read_command();
void analog_write_command();
void reset_command();
void pin_mode_command();
void ping_command();
void unrecognized(const char *command);


void setup() {
  Serial.begin(115200);

  // Check if SPIFFS is OK
  if (!SPIFFS.begin())
  {
    Serial.println("SPIFFS failed, needs formatting");
    handleFormat();
    delay(500);
    ESP.restart();
  }
  else
  {
    FSInfo fs_info;
    if (!SPIFFS.info(fs_info))
    {
      Serial.println("fs_info failed\n");
    }
  }

  CheckParseConfigJson();

  if (ssidStored == "" || passStored == "")
  {
    Serial.println("No wifi configuration found, starting in AP mode");
    Serial.println("SSID: ");
    Serial.println(APssid);
    Serial.println("password: ");
    Serial.println(APpassword);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(APssid, APpassword);
    Serial.println("Connected to ");
    Serial.println(APssid);
    Serial.println("IP address: ");
    Serial.println(WiFi.softAPIP().toString());
    WMode = "AP";
  }
  else
  {
    int i = 0;
    Serial.println("Connecting to :");
    Serial.println(ssidStored);

    if (WiFi.status() != WL_CONNECTED)
    {
      WiFi.mode(WIFI_STA);
      WiFi.begin(ssidStored.c_str(), passStored.c_str());
    }


    while (WiFi.status() != WL_CONNECTED && i < 31)
    {
      delay(1000);
      Serial.println(".");
      ++i;
    }
    if (WiFi.status() != WL_CONNECTED && i >= 30)
    {
      Serial.println("Couldn't connect to network :( ");
      WiFi.disconnect();
      delay(1000);
      Serial.println("Setting up access point");
      Serial.println("SSID: ");
      Serial.println(APssid);
      Serial.println("password: ");
      Serial.println(APpassword);
      WiFi.mode(WIFI_AP);
      WiFi.softAP(APssid, APpassword);
      Serial.println("Connected to ");
      Serial.println(APssid);
      IPAddress myIP = WiFi.softAPIP();
      Serial.println("IP address: ");
      Serial.println(WiFi.softAPIP().toString());
      WMode = "AP";
    }
    else
    {
      Serial.println("");
      Serial.println("Connected to ");
      Serial.println(ssidStored);
      Serial.println("IP address: ");
      Serial.println(WiFi.localIP().toString());
    }
  }

  if (Mode == "homeduino")
  {
    // Setup callbacks for SerialCommand commands
    sCmd.addCommand("DR", digital_read_command);
    sCmd.addCommand("DW", digital_write_command);
    sCmd.addCommand("AR", analog_read_command);
    sCmd.addCommand("AW", analog_write_command);
    sCmd.addCommand("PM", pin_mode_command);
    sCmd.addCommand("RF", rfcontrol_command);
    sCmd.addCommand("PING", ping_command);
    sCmd.addCommand("RESET", reset_command);
    sCmd.setDefaultHandler(unrecognized);
  }

  // If no root page && AP mode, set root to simple upload page
  if ( !SPIFFS.exists("/root.html") && !SPIFFS.exists("/root.html.gz") && WMode != "AP")
  {
    server.on("/", handle_fupload_html);
  }

  // If connected to AP mode, set root to simple wifi settings page
  if (WMode == "AP")
  {
    server.on("/", handle_wifim_html);
  }

  // Format Flash ROM - dangerous! Remove if you dont't want this option!
  server.on ( "/format", handleFormat );

  server.on("/fupload", handle_fupload_html);
  server.on("/wifi_ajax", handle_wifi_ajax);
  server.on("/bwlist_ajax", handle_bwlist_ajax);
  server.on("/list", HTTP_GET, printDirectory);
  server.on("/updatefwm", handle_updatefwm_html);
  server.on("/wifim", handle_wifim_html);
  server.on("/filemanager_ajax", handle_filemanager_ajax);
  server.on("/delete", handleFileDelete);
  server.on("/api", handle_api);
  server.on("/ping", handle_ping);
  server.on("/config_ajax", handle_config_ajax);

  // Upload firmware:
  server.on("/updatefw2", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START)
    {
      fileName = upload.filename;
      Serial.setDebugOutput(true);
      Serial.printf("Update: %s\n", upload.filename.c_str());
      uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
      if (!Update.begin(maxSketchSpace)) { //start with max available size
        Update.printError(Serial);
      }
    }
    else if (upload.status == UPLOAD_FILE_WRITE)
    {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
      {
        Update.printError(Serial);
      }
    }
    else if (upload.status == UPLOAD_FILE_END)
    {
      if (Update.end(true)) //true to set the size to the current progress
      {
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      }
      else
      {
        Update.printError(Serial);
      }
      Serial.setDebugOutput(false);

    }
    yield();
  });

  // upload file to SPIFFS
  server.on("/fupload2", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START)
    {
      noInterrupts();
      fileName = upload.filename;
      Serial.setDebugOutput(true);
      //fileName = upload.filename;
      Serial.println("Upload Name: " + fileName);
      String path;
      if (fileName.indexOf(".css") >= 0)
      {
        path = "/css/" + fileName;
      }
      else if (fileName.indexOf(".js") >= 0 && !fileName.endsWith(".json"))
      {
        path = "/js/" + fileName;
      }
      else if (fileName.indexOf(".otf") >= 0 || fileName.indexOf(".eot") >= 0 || fileName.indexOf(".svg") >= 0 || fileName.indexOf(".ttf") >= 0 || fileName.indexOf(".woff") >= 0 || fileName.indexOf(".woff2") >= 0)
      {
        path = "/fonts/" + fileName;
      }
      else
      {
        path = "/" + fileName;
      }
      UploadFile = SPIFFS.open(path, "w");
      // already existing file will be overwritten!
    }
    else if (upload.status == UPLOAD_FILE_WRITE)
    {
      if (UploadFile)
        UploadFile.write(upload.buf, upload.currentSize);
      Serial.println(fileName + " size: " + upload.currentSize);
    }
    else if (upload.status == UPLOAD_FILE_END)
    {
      Serial.println("Upload Size: ");
      Serial.println(String(upload.totalSize));  // need 2 commands to work!
      if (UploadFile)
        UploadFile.close();
    }
    yield();
  });

  //called when the url is not defined here
  //use it to load content from SPIFFS
  server.onNotFound([]() {
    if (!handleFileRead(server.uri()))
      server.send(404, "text/plain", "FileNotFound");
  });

  server.begin();

  if (Mode == "homeduino")
  {
    // read protocols into global String
    protocolsjson = ReadJson("/protocols.json");
    localUdpPort = 4210;  // local port to listen on
    Udp.begin(localUdpPort);
  }
  if (Mode == "node")
  {
    // read black and whitelist into global String
    bwlistjson = ReadJson("/bwlist.json");
    Serial.println(portMulti);
    Udp.beginMulticast(WiFi.localIP(),  ipMulti, portMulti);
  }

  if (receiveAction >= 1)
  {
    pinMode(receiverPin.toInt(), INPUT);
    RFControl::startReceiving(receiverPin.toInt());
    Serial.println("Receiving op pin " + String(receiverPin));
  }


}

void loop() {
  // Check the webserver for updates
  server.handleClient();

  if (Mode == "node" && transmitAction == 1)
  {
    int packetSize = Udp.parsePacket();
    if (packetSize)
    {
      // receive incoming UDP packets
      //Serial.println("Received " + String(packetSize) + " bytes from " + String(Udp.remoteIP().toString().c_str()) + " , port " + String(Udp.remotePort()));
      int len = Udp.read(incomingPacket, 355);
      if (len > 0)
      {
        incomingPacket[len] = 0;
      }
      handle_udp(String(incomingPacket), packetSize);
    }
  }

  if (Mode == "homeduino")
  {
    // handle serial command
    sCmd.readSerial();
    // handle rf control receiving
    rfcontrol_loop();
  }

  if (Mode == "node" && receiveAction == 1)
  {
    if (RFControl::hasData())
    {
      rfcontrolNode_loop();
    }
  }
}

void digital_read_command() {
  char* arg = sCmd.next();
  if (arg == NULL) {
    argument_error();
    return;
  }
  int pin = atoi(arg);
  int val = digitalRead(pin);
  Serial.print("ACK ");
  Serial.write('0' + val);
  Serial.print("\r\n");
}

void analog_read_command() {
  char* arg = sCmd.next();
  if (arg == NULL) {
    argument_error();
    return;
  }
  int pin = atoi(arg);
  int val = analogRead(pin);
  Serial.print("ACK ");
  Serial.print(val);
  Serial.print("\r\n");
}

void digital_write_command() {
  char* arg = sCmd.next();
  if (arg == NULL) {
    argument_error();
    return;
  }
  int pin = atoi(arg);
  arg = sCmd.next();
  if (arg == NULL) {
    argument_error();
    return;
  }
  int val = atoi(arg);
  digitalWrite(pin, val);
  Serial.print("ACK\r\n");
}

void analog_write_command() {
  char* arg = sCmd.next();
  if (arg == NULL) {
    argument_error();
    return;
  }
  int pin = atoi(arg);
  arg = sCmd.next();
  if (arg == NULL) {
    argument_error();
    return;
  }
  int val = atoi(arg);
  analogWrite(pin, val);
  Serial.print("ACK\r\n");
}

void pin_mode_command() {
  char* arg = sCmd.next();
  if (arg == NULL) {
    argument_error();
    return;
  }
  int pin = atoi(arg);
  arg = sCmd.next();
  if (arg == NULL) {
    argument_error();
    return;
  }
  // INPUT 0x0
  // OUTPUT 0x1
  int mode = atoi(arg);

  //pinMode(pin, mode);
  pinMode(5, mode);



  Serial.print("ACK\r\n");
}


void ping_command() {
  char *arg;
  Serial.print("PING");
  arg = sCmd.next();
  if (arg != NULL) {
    Serial.write(' ');
    Serial.print(arg);
  }
  Serial.print("\r\n");
}


void reset_command() {
  RFControl::stopReceiving();
  Serial.print("ready\r\n");
}

void argument_error() {
  Serial.print("ERR argument_error\r\n");
}
// This gets set as the default handler, and gets called when no other command matches.
void unrecognized(const char *command) {
  Serial.print("ERR unknown_command\r\n");
}


void CheckParseConfigJson()
{
  // Function to check the config.json form some mandatory settings (ore create them with defaults)
  // and set some values into global variables

  String configjsn = ReadJson("/config.json");
  DynamicJsonBuffer BufferSetup;
  JsonObject& systm = BufferSetup.parseObject(const_cast<char*>(configjsn.c_str()));

  if (systm.containsKey("settings"))
  {
    JsonObject& settings = systm["settings"];

    // Read Wifi settings into global variables
    if (settings.containsKey("wifi"))
    {
      JsonObject& wifi = settings["wifi"];
      ssidStored = wifi["ssid"].asString();
      passStored = wifi["password"].asString();
    }

    if (!settings.containsKey("ESPimaticRF"))
    {
      settings.createNestedObject("ESPimaticRF");
    }
    JsonObject& ESPimaticRF = settings["ESPimaticRF"];

    Mode = ESPimaticRF["mode"].asString();

    UDPrepeat = ESPimaticRF["UDPrepeat"];
    if (UDPrepeat == 0)
    {
      UDPrepeat = 3;
    }
    receiverPin = ESPimaticRF["receiverPin"].asString();
    transmitterPin = ESPimaticRF["transmitterPin"].asString();
    receiveAction = ESPimaticRF["receiveAction"];
    transmitAction = ESPimaticRF["transmitAction"];
    apikey = ESPimaticRF["apikey"].asString();
    pimaticIP = ESPimaticRF["pimaticIP"].asString();
    pimaticPort = ESPimaticRF["pimaticPort"];
    BWmode = ESPimaticRF["BWmode"];
  }



  int len = systm.measurePrettyLength() + 1;
  char ch[len];
  size_t n = systm.prettyPrintTo(ch, sizeof(ch));
  String tt(ch);
  WriteJson(tt, "/config.json");

}

String ReadJson(String filename)
{
  String json_string;
  File configFile = SPIFFS.open(filename, "r");
  if (!configFile)
  {
    Serial.println("error?");
    if (!SPIFFS.exists(filename))
    {
      Serial.println("File '" + String(filename) + "' not found, create empty json");
      json_string = "{}";
      File jsonFile = SPIFFS.open(filename, "w");
      if (!jsonFile)
      {
        Serial.println("Failed to open '" + filename + "' for writing");
      }
      else
      {
        jsonFile.print(json_string);
        jsonFile.flush();
        jsonFile.close();
      }
      return json_string;
    }
    else
    {
      Serial.println("Error accessing SPIFFS?!");
    }
  }
  else
  {
    while (configFile.available())
    {
      String line = configFile.readStringUntil('\n');
      json_string += line;
    }
    return json_string;
  }
}


void WriteJson(String json, String file)
{
  File jsonFile = SPIFFS.open(file, "w");
  if (!jsonFile)
  {
    Serial.println("Failed to open '" + file + "' for writing");
    return;
  }
  else
  {
    jsonFile.print(json);
    jsonFile.flush();
    jsonFile.close();
  }
}

// An empty ESP8266 Flash ROM must be formatted before using it, actual a problem
void handleFormat()
{
  server.send ( 200, "text/html", "OK");
  Serial.println("Format SPIFFS");
  if (SPIFFS.format())
  {
    if (!SPIFFS.begin())
    {
      Serial.println("Format SPIFFS failed");
    }
  }
  else
  {
    Serial.println("Format SPIFFS failed");
  }
  if (!SPIFFS.begin())
  {
    Serial.println("SPIFFS failed, needs formatting");
  }
  else
  {
    Serial.println("SPIFFS mounted");
  }
}

void handleFileDelete()
{
  if (server.args() == 0) return server.send(500, "text/plain", "BAD ARGS");
  String path = server.arg(0);
  if (!path.startsWith("/")) path = "/" + path;
  Serial.println("handleFileDelete: " + path);
  if (path == "/")
    return server.send(500, "text/plain", "BAD PATH");
  if (!SPIFFS.exists(path))
    return server.send(404, "text/plain", "FileNotFound");
  SPIFFS.remove(path);
  server.send(200, "text/plain", "");
  path = String();
}

void handle_filemanager_ajax()
{
  String form = server.arg("form");
  if (form != "filemanager")
  {
    String HTML;
    Dir dir = SPIFFS.openDir("/");
    while (dir.next())
    {
      fileName = dir.fileName();
      size_t fileSize = dir.fileSize();
      HTML += String("<option>") + fileName + String("</option>");
    }

    // Glue everything together and send to client
    server.send(200, "text/html", HTML);
  }
}

bool handleFileRead(String path)
{
  /*
  // Collect SPIFFS info
  FSInfo fsinfo;
  SPIFFS.info(fsinfo);
  int FSTotal = fsinfo.totalBytes;
  int FSUsed = fsinfo.usedBytes;
  Serial.println(String(FSTotal));
  Serial.println(String(FSUsed));
  */

  Serial.println("handleFileRead: " + path);
  if (path.endsWith("/")) path += "root.html";

  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path))
  {
    if (SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
    if ( (path.startsWith("/css/") || path.startsWith("/js/") || path.startsWith("/fonts/")) && !path.startsWith("/js/insert") )
    {
      server.sendHeader("Cache-Control", " max-age=31104000");
    }
    server.sendHeader("Connection", "close");

    size_t sent = server.streamFile(file, contentType);
    size_t contentLength = file.size();
    file.close();
    return true;
  }
  return false;
}

String getContentType(String filename) {
  if (server.hasArg("download")) return "application/octet-stream";
  else if (filename.endsWith(".htm")) return "text/html";
  else if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".png")) return "image/png";
  else if (filename.endsWith(".gif")) return "image/gif";
  else if (filename.endsWith(".jpg")) return "image/jpeg";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  else if (filename.endsWith(".xml")) return "text/xml";
  else if (filename.endsWith(".pdf")) return "application/x-pdf";
  else if (filename.endsWith(".zip")) return "application/x-zip";
  else if (filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

void handle_fupload_html()
{
  String HTML = "<br>Files on flash:<br>";
  Dir dir = SPIFFS.openDir("/");
  while (dir.next())
  {
    fileName = dir.fileName();
    size_t fileSize = dir.fileSize();
    HTML += fileName.c_str();
    HTML += " ";
    HTML += formatBytes(fileSize).c_str();
    HTML += " , ";
    HTML += fileSize;
    HTML += "<br>";
    //Serial.printf("FS File: %s, size: %s\n", fileName.c_str(), formatBytes(fileSize).c_str());
  }

  server.send ( 200, "text/html", "<form method='POST' action='/fupload2' enctype='multipart/form-data'><input type='file' name='update' multiple><input type='submit' value='Update'></form><br<b>For webfiles only!!</b>Multiple files possible<br>" + HTML);
}

//-------------- FSBrowser application -----------
//format bytes
String formatBytes(size_t bytes) {
  if (bytes < 1024) {
    return String(bytes) + "B";
  } else if (bytes < (1024 * 1024)) {
    return String(bytes / 1024.0) + "KB";
  } else if (bytes < (1024 * 1024 * 1024)) {
    return String(bytes / 1024.0 / 1024.0) + "MB";
  } else {
    return String(bytes / 1024.0 / 1024.0 / 1024.0) + "GB";
  }
}

void handle_wifi_ajax()
{
  String form = server.arg("form");
  if (form != "wifi")
  {
    String systmjsn = ReadJson("/config.json");
    DynamicJsonBuffer BufferWifi;
    JsonObject& systm = BufferWifi.parseObject(const_cast<char*>(systmjsn.c_str()));
    String ssidStored;
    String passStored;
    if (systm.containsKey("settings"))
    {
      JsonObject& settings = systm["settings"];

      if (settings.containsKey("wifi"))
      {
        JsonObject& wifi = settings["wifi"];

        ssidStored = wifi["ssid"].asString();
        passStored = wifi["password"].asString();
      }
    }

    // Glue everything together and send to client
    server.send(200, "text/html", ssidStored + sep + passStored);
  }
  if (form == "wifi")
  {
    String ssidArg = server.arg("ssid");
    String passArg = server.arg("password");

    String systmjsn = ReadJson("/config.json");
    DynamicJsonBuffer BufferWifi;
    JsonObject& systm = BufferWifi.parseObject(const_cast<char*>(systmjsn.c_str()));
    if (!systm.containsKey("settings"))
    {
      systm.createNestedObject("settings");
    }
    JsonObject& settings = systm["settings"];
    if (!settings.containsKey("wifi"))
    {
      settings.createNestedObject("wifi");
    }
    JsonObject& wifi = settings["wifi"];
    wifi["ssid"] = ssidArg;
    wifi["password"] = passArg;

    int len = systm.measurePrettyLength() + 1;
    char ch[len];
    size_t n = systm.prettyPrintTo(ch, sizeof(ch));
    String tt = (ch);
    WriteJson(tt, "/config.json");

    server.send ( 200, "text/html", "OK");
    delay(500);
    ESP.restart();
  }
}

void handle_wifim_html()
{
  server.send ( 200, "text/html", "<form method='POST' action='/wifi_ajax'><input type='hidden' name='form' value='wifi'><input type='text' name='ssid'><input type='password' name='password'><input type='submit' value='Submit'></form><br<b>Enter WiFi credentials</b>");
}

void handle_info()
{
  // Collect SPIFFS info
  FSInfo fsinfo;
  SPIFFS.info(fsinfo);
  int FSTotal = fsinfo.totalBytes;
  int FSUsed = fsinfo.usedBytes;
  Serial.println(FSTotal);
  Serial.println(FSUsed);
  server.send(200, "text/html", FSTotal + "____" + FSUsed);
}

void printDirectory()
{
  String jsnOutput = "[";
  int i = 0;

  String output;
  Dir dir = SPIFFS.openDir("/");
  while (dir.next())
  {
    fileName = dir.fileName();
    fileName.remove(0, 1);
    size_t fileSize = dir.fileSize();

    if (i != 0)
    {
      jsnOutput += ',';
    }
    jsnOutput += "{\"type\":\"";
    jsnOutput +=  "file";
    jsnOutput += "\",\"name\":\"";
    jsnOutput += fileName;
    jsnOutput += "\"";
    jsnOutput += "}";
    i++;
  }
  jsnOutput += "]";
  server.send ( 200, "text/html", jsnOutput);
}

void handle_updatefwm_html()
{
  server.send ( 200, "text/html", "<form method='POST' action='/updatefw2' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form><br<b>For firmware only!!</b>");
}

void send_udp(String data)
{
  SendDone = 0;
  SerialString = data;
  char pls[data.length() + 1];
  data.toCharArray(pls, data.length() + 1);

  RFControl::stopReceiving();

  for (int i = 0; i < UDPrepeat; i++)
  {
    Udp.beginPacketMulticast(ipMulti, portMulti, WiFi.localIP());
    Udp.write(pls);
    Udp.endPacket();
    Serial.println("UDP Packet " + String(i) + " done");
    delay(50);
  }
  RFControl::startReceiving(receiverPin.toInt());
}

void send_data(String data)
{
  String PostData = "action=rf&api=" + apikey + "&value=" + data;

  const char* Hostchar = "192.168.2.198";
  const char* Portchar = "80";
  if (!client.connect(Hostchar, 80))
  {
    Serial.println("connection to node failed");
    return;
  }

  client.println("POST /api HTTP/1.1");
  client.println("Host: jsonplaceholder.typicode.com");
  client.println("Cache-Control: no-cache");
  client.println("Content-Type: application/x-www-form-urlencoded");
  client.print("Content-Length: ");
  client.println(PostData.length());
  client.println();
  client.println(PostData);

  const char* status = "true";

  //  delay(500);

  while (client.available())
  {
    String line = client.readStringUntil('\r');
  }
}

void handle_api()
{
  // Get vars for all commands
  String action = server.arg("action");
  String value = server.arg("value");
  String apiurl = server.arg("api");

  int ValidCall = 0;

  if (action == "rf" && Mode == "node")
  {
    ValidCall = 1;
    if (apiurl == apikey)
    {
      Serial.println("Receiving RF data: " + String(value));
      server.send ( 200, "text/html", "OK");

      DynamicJsonBuffer BufferSetup;
      JsonObject& rf = BufferSetup.parseObject(value);
      JsonObject& bckets = rf["buckets"];

      // read pulse lengths
      unsigned long buckets[8];
      for (unsigned int i = 0; i < 8; i++)
      {
        buckets[i] = strtoul(bckets[String(i)], NULL, 10);
      }

      int repeats = rf["repeats"];
      //char pulse = rf["pulse"];

      String pulse = rf["pulse"].asString();


      char pls[pulse.length() + 1];
      pulse.toCharArray(pls, pulse.length() + 1);

      RFControl::sendByCompressedTimings(4, buckets, pls, 7);
    }
    else
    {
      Serial.println("Wrong API key: " + apiurl);
      server.send ( 200, "text/html", "ERROR: Invalide API key");
    }
  }

  if (action == "GET")
  {
    if (value == "lastreceived")
    {
      server.send(200, "text/html", LastReceived);
    }
  }

  if (action == "reboot" && value == "true")
  {
    ValidCall = 1;
    Serial.println("Reboot on user request");
    server.send ( 200, "text/html", "OK");
    delay(500);
    ESP.restart();
  }

  if (ValidCall == 0)
  {
    server.send ( 200, "text/html", "ERROR: Unknown API command");
  }
}

void handle_ping()
{
  server.send ( 200, "text/html", "pong");
}

void handle_bwlist_ajax()
{
  String action = server.arg("action");
  
  if (action == "RemoveBWDevice")
  {
    String protocol = server.arg("protocol");
    String id = server.arg("id");
    String unit = server.arg("unit");

    server.send(200, "text/html", LastReceived);

    Serial.println("Remove from bwlist : " + protocol + "," + unit + "," + id);

    bwlistjson = ReadJson("/bwlist.json");
    DynamicJsonBuffer bwbuffer;
    JsonObject& list = bwbuffer.parseObject(const_cast<char*>(bwlistjson.c_str()));

    if (list.containsKey(protocol))
    {
      JsonObject& bwprotocol = list[protocol];
      if (bwprotocol.containsKey(id))
      {
        JsonObject& bwid = bwprotocol[id];
        bwid.remove(unit);

        int len = list.measurePrettyLength() + 1;
        char ch[len];
        size_t n = list.prettyPrintTo(ch, sizeof(ch));
        String tt = (ch);
        WriteJson(tt, "/bwlist.json");
        bwlistjson = tt;  // update the bwlist in memory
      }
    }
  }

  if (action == "AddBWDevice")
  {

    String jsondata = server.arg("device");
    String type = server.arg("type");

    DynamicJsonBuffer incomingbuffer;
    JsonObject& root = incomingbuffer.parseObject(const_cast<char*>(jsondata.c_str()));

    String protocol = root["protocol"].asString();
    String unit = root["unit"].asString();
    String id = root["id"].asString();

    bwlistjson = ReadJson("/bwlist.json");
    DynamicJsonBuffer bwbuffer;
    JsonObject& list = bwbuffer.parseObject(const_cast<char*>(bwlistjson.c_str()));

    if (!list.containsKey(protocol))
    {
      list.createNestedObject(protocol);
    }
    JsonObject& bwprotocol = list[protocol];

    if (!bwprotocol.containsKey(id))
    {
      bwprotocol.createNestedObject(id);
    }
    JsonObject& bwid = bwprotocol[id];

    bwid[unit] = type;

    int len = list.measurePrettyLength() + 1;
    char ch[len];
    size_t n = list.prettyPrintTo(ch, sizeof(ch));
    String tt = (ch);
    WriteJson(tt, "/bwlist.json");
    bwlistjson = tt;  // update the bwlist in memory

    server.send ( 200, "text/html", "OK");
  }
}

void handle_config_ajax()
{
  String form = server.arg("form");

  if (form == "system")
  {
    String Mode = server.arg("mode");
    String receiver_pin = server.arg("receiver_pin");
    String transmitter_pin = server.arg("transmitter_pin");
    String receive_action = server.arg("receive_action");
    String transmit_action = server.arg("transmit_action");
    String udprepeat = server.arg("udprepeat");
    String pimaticIP = server.arg("pimaticIP");
    String pimaticPort = server.arg("pimaticPort");
    String apikey = server.arg("apikey");
    String bwmode = server.arg("bwmode");
    String ssid = server.arg("ssid");
    String password = server.arg("password");

    String systmjsn = ReadJson("/config.json");
    DynamicJsonBuffer BufferWifi;
    JsonObject& systm = BufferWifi.parseObject(const_cast<char*>(systmjsn.c_str()));
    if (!systm.containsKey("settings"))
    {
      systm.createNestedObject("settings");
    }
    JsonObject& settings = systm["settings"];
    if (!settings.containsKey("wifi"))
    {
      settings.createNestedObject("wifi");
    }
    JsonObject& wifi = settings["wifi"];
    wifi["ssid"] = ssid;
    wifi["password"] = password;

    if (!settings.containsKey("ESPimaticRF"))
    {
      settings.createNestedObject("ESPimaticRF");
    }
    JsonObject& ESPimaticRF = settings["ESPimaticRF"];

    ESPimaticRF["mode"] = Mode;
    ESPimaticRF["receiverPin"] = receiver_pin;
    ESPimaticRF["transmitterPin"] = transmitter_pin;
    ESPimaticRF["receiveAction"] = receive_action;
    ESPimaticRF["transmitAction"] = transmit_action;
    ESPimaticRF["UDPrepeat"] = udprepeat;
    ESPimaticRF["pimaticIP"] = pimaticIP;
    ESPimaticRF["pimaticPort"] = pimaticPort;
    ESPimaticRF["apikey"] = apikey;
    ESPimaticRF["BWmode"] = bwmode;

    server.send ( 200, "text/html", "OK");

    int len = systm.measurePrettyLength() + 1;
    char ch[len];
    size_t n = systm.prettyPrintTo(ch, sizeof(ch));
    String tt = (ch);
    WriteJson(tt, "/config.json");
  }
}

void handle_udp(String incomingPacket, int packetSize)
{
  if (String(incomingPacket).substring(0, 8) != PrevHash )
  {
    String strippedJson = String(incomingPacket).substring(8, packetSize);
    PrevHash = String(incomingPacket).substring(0, 8);
    DynamicJsonBuffer BufferSetup;
    JsonObject& rf = BufferSetup.parseObject(strippedJson);

    if (rf.success())
    {
      JsonObject& bckets = rf["buckets"];

      // read pulse lengths
      unsigned long buckets[8];
      for (unsigned int i = 0; i < 8; i++)
      {
        buckets[i] = strtoul(bckets[String(i)], NULL, 10);
      }

      int repeats = rf["repeats"];
      String pulse = rf["pulse"].asString();
      String protocol = rf["protocol"].asString();
      String unit = rf["unit"].asString();
      String id = rf["id"].asString();

      Serial.println("Receive message from homeduino, protocol:" + protocol + " , unit:" + unit + " , id:" + id);

      if (BWmode == 1)
      {
        // BWmode 1 = Allow everything except blacklist
        String BWCopy = bwlistjson;
        DynamicJsonBuffer bwbuffer;
        JsonObject& bw = bwbuffer.parseObject(const_cast<char*>(BWCopy.c_str()));

        if ( String(bw[protocol][id][unit].asString()) != "blacklisted")
        {
          char pls[pulse.length() + 1];
          pulse.toCharArray(pls, pulse.length() + 1);
          RFControl::sendByCompressedTimings(transmitterPin.toInt(), buckets, pls, repeats);
          LastReceived = String(incomingPacket).substring(8, packetSize);
          //LastReceived = incomingPacket;
        }
        else
        {
          Serial.println("This protocol/ID/Unit is blacklisted, do nothing");
        }
      }

      if (BWmode == 2)
      {
        // BWmode 2 = Allow nothing except whitelist
        String BWCopy = bwlistjson;
        DynamicJsonBuffer bwbuffer;
        JsonObject& bw = bwbuffer.parseObject(const_cast<char*>(BWCopy.c_str()));

        if ( String(bw[protocol][id][unit].asString()) == "whitelisted")
        {
          char pls[pulse.length() + 1];
          pulse.toCharArray(pls, pulse.length() + 1);
          RFControl::sendByCompressedTimings(transmitterPin.toInt(), buckets, pls, repeats);
        }
        else
        {
          Serial.println("This protocol/ID/Unit is not in the whitelist, do nothing");
          LastReceived = String(incomingPacket).substring(8, packetSize);
          //LastReceived = incomingPacket;
        }
      }

      if (BWmode == 0)
      {
        // BWmode 0 = Allow everything
        char pls[pulse.length() + 1];
        pulse.toCharArray(pls, pulse.length() + 1);
        RFControl::sendByCompressedTimings(transmitterPin.toInt(), buckets, pls, repeats);
        LastReceived = String(incomingPacket).substring(8, packetSize);
        //LastReceived = incomingPacket;
      }
    }
    else
    {
      Serial.println("problem with incoming json?");
    }
  }
  else
  {
    Serial.println("Ignore duplicate message");
  }
}

