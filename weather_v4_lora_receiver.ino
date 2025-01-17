//LoRa baseed weather station - receiver node
//Hardware design by Debasish Dutta - opengreenenergy@gmail.com
//Software design by James Hughes - jhughes1010@gmail.com

/* History
   0.9.0 10-02-22 Initial development for Heltec ESP32 LoRa v2 devkit

   1.0.2 11-17-22 Remapping all mqtt topics

   1.1.0 11-24-22 sensor structure expanded to receive max wind speed also
                  LED display online for Heltec board

   1.1.1 12-13-22 Error in code where non-Heltec build would not compile properly
                  Missing #ifdef statements were added to correct this 

   1.1.2 01-27-23 Packet type details added to display.
                  Explicitly checking both packet sizes before memcpy
                  Added WDT of 30 sec in case of hang up in loop() 
                  Set sender and receiver sync word to filter correct transmissions
                  Add 10 sec heartbeat LED for Heltec PCB                
*/

//Hardware build target: ESP32
#define VERSION "1.1.2"


//#include "heltec.h"
#include <LoRa.h>
#include <spi.h>

#include "config.h"
#include <esp_wifi.h>
#include <esp_task_wdt.h>
//#include <time.h>
#include <BlynkSimpleEsp32.h>
#include <PubSubClient.h>
#ifdef DEV_HELTEC_RECEIVER
#include <Wire.h>
#include <U8g2lib.h>
#endif

String rssi = "RSSI --";
String packSize = "--";
String packet;
byte packetBinary[512];

float rssi_wifi;
float rssi_lora;

#ifdef DEV_HELTEC_RECEIVER
U8G2_SSD1306_128X64_NONAME_F_SW_I2C led(U8G2_R0, /* clock=*/15, /* data=*/4, /* reset=*/16);
#endif

//===========================================
// Weather-environment structure
//===========================================
struct sensorData {
  int deviceID;
  int windDirectionADC;
  int rainTicks24h;
  int rainTicks60m;
  float temperatureC;
  float windSpeed;
  float windSpeedMax;
  float barometricPressure;
  float humidity;
  float UVIndex;
  float lux;
};

struct diagnostics {
  int deviceID;
  float BMEtemperature;
  int batteryADC;
  int solarADC;
  int coreC;
  int bootCount;
  bool chargeStatusB;
};

struct derived {
  char cardinalDirection[5];
  float degrees;
};

struct sensorData environment;
struct diagnostics hardware;
struct derived wind;

//===========================================
// LoRaData: acknowledge LoRa packet received on OLED
//===========================================
void LoRaData() {
  static int count = 1;
  char buffer[20];
  sprintf(buffer, "Count: %i", count);
  MonPrintf("%s\n", buffer);
  count++;
}

//===========================================
// cbk: retreive contents of the received packet
//===========================================
void cbk(int packetSize) {
  //struct sensorData environment;
  packet = "";
  packSize = String(packetSize, DEC);
  for (int i = 0; i < packetSize; i++) {
    packetBinary[i] = (char)LoRa.read();
  }
  //LoRa.receive
  rssi_lora = LoRa.packetRssi();
  rssi = "RSSI " + String(rssi_lora, DEC);
  if (packetSize == sizeof(environment)) {
    memcpy(&environment, &packetBinary, packetSize);
  } else if (packetSize == sizeof(hardware)) {
    memcpy(&hardware, &packetBinary, packetSize);
  }
  LoRaData();
}

//===========================================
// setup:
//===========================================
void setup() {
  Serial.begin(115200);

  //Enable WDT for any lock-up events
  esp_task_wdt_init(WDT_TIMEOUT, true);
  esp_task_wdt_add(NULL);

#ifdef DEV_HELTEC_RECEIVER
  led.begin();
  LEDTitle();
#endif

  Serial.println("LoRa Receiver");
  Serial.println(VERSION);
#ifdef DEV_HELTEC_RECEIVER
  LoRa.setPins(18, 14, 26);
  pinMode(LED, OUTPUT);
#else
  LoRa.setPins(15, 17, 13);
#endif
  if (!LoRa.begin(BAND)) {
    Serial.println("Starting LoRa failed!");
    while (1);
  }
  LoRa.receive();
  wifi_connect();
#ifdef DEV_HELTEC_RECEIVER
  OLEDConnectWiFi();
#endif
  //configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  LoRa.enableCrc();
  LoRa.setSyncWord(SYNC);
  Serial.printf("LoRa receiver is online\n");
}

//===========================================
// loop:
//===========================================
void loop() {
  esp_task_wdt_reset();
  static int count = 0, Scount = 0, Hcount = 0, Xcount = 0;
  int packetSize = LoRa.parsePacket();

  environment.deviceID = 0;
  hardware.deviceID = 0;

  if (packetSize) {
    count++;
    cbk(packetSize);
    Serial.printf("Packet size: %i\n", packetSize);

    MonPrintf("Environment deviceID %x\n", environment.deviceID);
    MonPrintf("Hardware deviceID %x\n", hardware.deviceID);
    //check for weather data packet
    if (packetSize == sizeof(environment) && environment.deviceID == DEVID) {
      PrintEnvironment(environment);
      SendDataMQTT(environment);
      Scount++;
    }
    //check for hardware data packet
    else if (packetSize == sizeof(hardware) && hardware.deviceID == DEVID) {
      PrintHardware(hardware);
      SendDataMQTT(hardware);
      Hcount++;
    } else {
      Xcount++;
    }
#ifdef DEV_HELTEC_RECEIVER
    LEDStatus(count, Scount, Hcount, Xcount);
#endif
  }
  delay(10);
}

//===========================================
// HexDump: output hex data of the environment structure - going away
//===========================================
void HexDump(int size) {
  //int size = 28;
  int x;
  char ch;
  char* p = (char*)&environment;

  for (x = 0; x < size; x++) {
    //ch = *(p+x);
    Serial.printf("%02X ", p[x]);
  }
  Serial.println();
}

//===========================================
// PrintEnvironment: Dump environment structure to console
//===========================================
void PrintEnvironment(struct sensorData environment) {
  MonPrintf("Rain Ticks 24h: %i\n", environment.rainTicks24h);
  MonPrintf("Rain Ticks 60m: %i\n", environment.rainTicks60m);
  MonPrintf("Temperature: %f\n", environment.temperatureC);
  MonPrintf("Wind speed: %f\n", environment.windSpeed);
  //TODO:  Serial.printf("Wind direction: %f\n", environment.windDirection);
  MonPrintf("barometer: %f\n", environment.barometricPressure);
  MonPrintf("Humidity: %f\n", environment.humidity);
  MonPrintf("UV Index: %f\n", environment.UVIndex);
  MonPrintf("Lux: %f\n", environment.lux);
}

//===========================================
// PrintEnvironment: Dump hardware structure to console
//===========================================
void PrintHardware(struct diagnostics hardware) {
  MonPrintf("Boot count: %i\n", hardware.bootCount);
  MonPrintf("Case Temperature: %f\n", hardware.BMEtemperature);
  //Serial.printf("Battery voltage: %f\n", hardware.batteryVoltage);
  MonPrintf("Battery ADC: %i\n", hardware.batteryADC);
  MonPrintf("Solar ADC: %i\n", hardware.solarADC);
  MonPrintf("ESP32 core temp C: %i\n", hardware.coreC);
}

//===========================================
// MonPrintf: diagnostic printf to terminal
//===========================================
void MonPrintf(const char* format, ...) {
  char buffer[200];
  va_list args;
  va_start(args, format);
  vsprintf(buffer, format, args);
  va_end(args);
#ifdef SerialMonitor
  Serial.printf("%s", buffer);
#endif
}
