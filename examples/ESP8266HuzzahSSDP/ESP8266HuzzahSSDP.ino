#define BYTES_PER_PACKET 32
#define DEBUG 1
#define MAX_SRV_CLIENTS 2
#define MAX_PACKETS 100

#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266SSDP.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include "SPISlave.h"
#include "WiFiClientPrint.h"

ESP8266WebServer server(80);

volatile uint8_t packetCount = 0;
volatile uint8_t maxPacketsPerWrite = 10;
volatile int position = 0;
volatile uint8_t head = 8;
volatile uint8_t tail = 0;

// int sendToClientRateHz = 25;
unsigned long packetIntervalUs = 25000; //(int)(1.0 / (float)sendToClientRateHz * 1000000.0);
unsigned long lastSendToClient = 0;
int counter = 0;

StaticJsonBuffer<8368> jsonBuffer;

uint8_t ringBuf[MAX_PACKETS][BYTES_PER_PACKET];

void configModeCallback (WiFiManager *myWiFiManager) {
  // Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());

  Serial.println(myWiFiManager->getConfigPortalSSID());
}

bool readRequest(WiFiClient& client) {
  bool currentLineIsBlank = true;
  while (client.connected()) {
    if (client.available()) {
      char c = client.read();
      if (c == '\n' && currentLineIsBlank) {
        return true;
      } else if (c == '\n') {
        currentLineIsBlank = true;
      } else if (c != '\r') {
        currentLineIsBlank = false;
      }
    }
  }
  return false;
}


JsonObject& prepareResponse(JsonBuffer& jsonBuffer) {
  JsonObject& root = jsonBuffer.createObject();
  root["counter"] = counter++;
  root["sensor"] = "cyton";
  root["timestamp"] = millis();
  JsonArray& data = root.createNestedArray("data");
  while (tail != head) {
    if (tail >= MAX_PACKETS) {
      tail = 0;
    }
    JsonArray& nestedArray = data.createNestedArray();

    for (uint8_t i = 0; i < BYTES_PER_PACKET; i++) {
      nestedArray.add(ringBuf[tail][i]);
    }
    tail++;
  }

  return root;
}

// void handleNotFound(){
//   String message = "File Not Found\n\n";
//   message += "URI: ";
//   message += server.uri();
//   message += "\nMethod: ";
//   message += (server.method() == HTTP_GET)?"GET":"POST";
//   message += "\nArguments: ";
//   message += server.args();
//   message += "\n";
//   for (uint8_t i=0; i<server.args(); i++){
//     message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
//   }
//   server.send(404, "text/plain", message);
// }

String getName() {
  // WiFi.mode(WIFI_AP);

  // Do a little work to get a unique-ish name. Append the
  // last two bytes of the MAC (HEX'd) to "Thing-":
  uint8_t mac[WL_MAC_ADDR_LENGTH];
  WiFi.softAPmacAddress(mac);
  String macID = String(mac[WL_MAC_ADDR_LENGTH - 2], HEX) +
                 String(mac[WL_MAC_ADDR_LENGTH - 1], HEX);
  macID.toUpperCase();
  String AP_NameString = "OpenBCI-" + macID;

  char AP_NameChar[AP_NameString.length() + 1];
  memset(AP_NameChar, 0, AP_NameString.length() + 1);

  for (int i=0; i<AP_NameString.length(); i++)
    AP_NameChar[i] = AP_NameString.charAt(i);

  // WiFi.softAP(AP_NameChar, WiFiAPPSK);

  return AP_NameString;
}

void printWifiStatus() {
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your WiFi shield's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);
}

void getData() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
#ifdef DEBUG
  Serial.println("Trying to write some json");
#endif

  JsonObject& object = prepareResponse(jsonBuffer);
  object.printTo(Serial); Serial.println();
#ifdef DEBUG
  Serial.print("\nLength of json: "); Serial.println(object.measureLength());
#endif
  server.setContentLength(object.measureLength());
  server.send(200, "application/json", "");
#ifdef DEBUG
  Serial.println("Will send to client now");
#endif

  Serial.println(server.client().available() ? "client legit" : "client not available");
  WiFiClientPrint<> p(server.client());
  object.printTo(p);
  p.stop();
#ifdef DEBUG
  Serial.println("Json sent");
#endif
}

void setup() {

#ifdef DEBUG
  Serial.begin(115200);
  Serial.setDebugOutput(true);
#endif

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;
  WiFiManagerParameter custom_text("<p>Powered by Push The World</p>");
  wifiManager.addParameter(&custom_text);

  wifiManager.setAPCallback(configModeCallback);

  //fetches ssid and pass from eeprom and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  wifiManager.autoConnect("OpenBCI");

#ifdef DEBUG
  printWifiStatus();
#endif

  Serial.printf("Starting HTTP...\n");
  server.on("/index.html", HTTP_GET, [](){
    server.send(200, "text/plain", "OpenBCI is in Wifi discovery mode");
  });
  server.on("/description.xml", HTTP_GET, [](){
    SSDP.schema(server.client());
  });
  server.on("/you-there", HTTP_GET, [](){
    server.send(200, "text/plain", "Keep going AJ! Push The World!");
  });
  server.on("/data", HTTP_GET, getData);
  // server.onNotFound(handleNotFound);
  server.begin();

  Serial.printf("Starting SSDP...\n");
  SSDP.setSchemaURL("description.xml");
  SSDP.setHTTPPort(80);
  SSDP.setName("PTW - OpenBCI Wifi Shield");
  SSDP.setSerialNumber(getName());
  SSDP.setURL("index.html");
  SSDP.setModelName("PTW - OpenBCI Wifi Shield Bridge 2017");
  SSDP.setModelNumber("929000226503");
  SSDP.setModelURL("http://www.openbci.com");
  SSDP.setManufacturer("Push The World LLC");
  SSDP.setManufacturerURL("http://www.pushtheworldllc.com");
  SSDP.begin();

#ifdef DEBUG
    Serial.printf("Ready!\n");
#endif
  // data has been received from the master. Beware that len is always 32
  // and the buffer is autofilled with zeroes if data is less than 32 bytes long
  // It's up to the user to implement protocol for handling data length
  SPISlave.onData([](uint8_t * data, size_t len) {
    if (head >= MAX_PACKETS) {
      head = 0;
    }
    for (int i = 0; i < len; i++) {
      ringBuf[head][i] = data[i];
      // Serial.print(data[i]);
    }
    head++;
  });

  SPISlave.onDataSent([]() {
#ifdef DEBUG
    Serial.println("Sent ip");
#endif
    IPAddress ip = WiFi.localIP();
    SPISlave.setData(ip.toString().c_str());
  });

  // Setup SPI Slave registers and pins
  SPISlave.begin();

  IPAddress ip = WiFi.localIP();
  // Serial.print("IP Address: ");
  // Serial.println(ip);
  SPISlave.setData(ip.toString().c_str());

#ifdef DEBUG
  Serial.println("SPI Slave ready");
#endif

}

void loop() {
  server.handleClient();
}