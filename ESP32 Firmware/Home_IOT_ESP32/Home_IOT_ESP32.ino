/*
*/
#include <WiFi.h>              // Built-in
#include <WiFiMulti.h>         // Built-in
#include <ESP32WebServer.h>    // https://github.com/Pedroalbuquerque/ESP32WebServer download and place in your Libraries folder
#include <ESPmDNS.h>
#include "FS.h"

#include <NTPClient.h>
#include <WiFiUdp.h>


#include "Network.h"
#include "Sys_Variables.h"
#include "CSS.h"
#include "SPIFFS.h"
#include "DHTesp.h"
#include "Ticker.h"
#include "ThingSpeak.h"


/* LED pin and timer*/
int led = 2;


WiFiMulti wifiMulti;
WiFiClient  client;
ESP32WebServer server(80);

//ThingSpeak Stuff
unsigned long myChannelNumber = SECRET_CH_ID;
const char * myWriteAPIKey = SECRET_WRITE_APIKEY;


// Battery measurement
unsigned int batteryPin = 33;
float batteryVoltageADC = 0;
float batteryVoltage();

// Flow Sensor
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
unsigned int flowPin = 34;
unsigned long tickCounter = 0;
unsigned long actualTicks = 0;
float actualFrequency = 0;
float actualFlow = 0;
float actualVolume = 0;

//timer
unsigned long previousMillis = 0;
unsigned long currentMillis = 0;
unsigned long timeMeasure = 20000;
unsigned long timeRunning = 0;

//Hour
// Defined NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

// Variables to save date and time
String formattedDate;
String dayStamp;
String timeStamp;

//******************************** Interrupt ********************************

void IRAM_ATTR handleInterrupt() {
  digitalWrite(led, HIGH);
  addTick();
  digitalWrite(led, LOW);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void setup(void) {
  // WIfi configs
  Serial.begin(115200);
  if (!WiFi.config(local_IP, gateway, subnet, dns)) { //WiFi.config(ip, gateway, subnet, dns1, dns2);

    Serial.println("WiFi STATION Failed to configure Correctly");
  }
  // WiFi.setHostname(“ESP32-IOT”);
  wifiMulti.addAP(ssid_1, password_1);  // add Wi-Fi networks you want to connect to, it connects strongest to weakest
  //wifiMulti.addAP(ssid_2, password_2);  // Adjust the values in the Network tab
  // wifiMulti.addAP(ssid_3, password_3);
  //wifiMulti.addAP(ssid_4, password_4);  // You don't need 4 entries, this is for example!

  Serial.println("Connecting ...");
  while (wifiMulti.run() != WL_CONNECTED) { // Wait for the Wi-Fi to connect: scan for Wi-Fi networks, and connect to the strongest of the networks above
    delay(250); Serial.print('.');
  }
  Serial.println("\nConnected to " + WiFi.SSID() + " Use IP address: " + WiFi.localIP().toString()); // Report which SSID and IP is in use
  // The logical name http://fileserver.local will also access the device if you have 'Bonjour' running or your system supports multicast dns
  if (!MDNS.begin(servername)) {          // Set your preferred server name, if you use "myserver" the address would be http://myserver.local/
    Serial.println(F("Error setting up MDNS responder!"));
    ESP.restart();
  }
  ///////////////////////////// Server Commands
  server.on("/",         HomePage);
  server.on("/download", File_Download);
  server.on("/Erase", File_Erase);
  ///////////////////////////// End of Request commands
  server.begin();
  Serial.println("HTTP server started");

  //Init SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
  listAllFiles();
  File file = SPIFFS.open("/sensor.txt", FILE_APPEND);

  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }
  else {
    file.close();
  }

  //Get hour
  // Initialize a NTPClient to get time
  timeClient.begin();
  // Set offset time in seconds to adjust for your timezone, for example:
  // GMT +1 = 3600
  // GMT +8 = 28800
  // GMT -1 = -3600
  // GMT 0 = 0
  // -18000 for Colombia
  timeClient.setTimeOffset(-18000);

  while (!timeClient.update()) {
    timeClient.forceUpdate();
  }

  formattedDate = timeClient.getFormattedDate();
  Serial.println(formattedDate);


  ThingSpeak.begin(client);  // Initialize ThingSpeak
  batteryVoltageADC = batteryVoltage();

  // Flow sensor
  pinMode(flowPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(flowPin), handleInterrupt, FALLING);

  pinMode(led, OUTPUT);
  digitalWrite(led, HIGH);
  send2ThinkSpeak();
  write2File();
  printData();
  digitalWrite(led, LOW);

  previousMillis = millis();

}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void loop(void) {

  server.handleClient(); // Listen for client connections


  currentMillis = millis();
  timeRunning = currentMillis - previousMillis;
  if (timeRunning >= timeMeasure)
  {
    currentMillis = millis();
    timeRunning = currentMillis - previousMillis;

    actualFrequency = frequencyMeasure(timeRunning);
    actualFlow = frequency2Flow(actualFrequency);
    actualVolume = flow2Volume(actualFlow, timeRunning) + actualVolume;
    actualTicks = tickCounter;
    resetTick();

    //digitalWrite(led, HIGH);
    batteryVoltageADC = batteryVoltage();

    timeClient.update();
    formattedDate = timeClient.getFormattedDate();
    send2ThinkSpeak();
    write2File();
    printData();
    currentMillis = millis();
    previousMillis = currentMillis;
    //digitalWrite(led, LOW);
    timeRunning = currentMillis - previousMillis;
  }




}




// All supporting functions from here...
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void HomePage() {
  SendHTML_Header();
  webpage += F("<a href='/download'><button>Ir directo</button></a>");
  append_page_footer();
  SendHTML_Content();
  SendHTML_Stop(); // Stop is needed because no content length was sent
}
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void File_Download() { // This gets called twice, the first pass selects the input, the second pass then processes the command line arguments
  if (server.args() > 0 ) { // Arguments were received
    if (server.hasArg("download")) Internal_file_download(server.arg(0));
  }
  else SelectInput("File Download", "Introduzca el nombre para descargar", "download", "download");
}
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void Internal_file_download(String filename) {

  File download = SPIFFS.open("/" + filename);
  if (download) {
    server.sendHeader("Content-Type", "text/text");
    server.sendHeader("Content-Disposition", "attachment; filename=" + filename);
    server.sendHeader("Connection", "close");
    server.streamFile(download, "application/octet-stream");
    download.close();
  } else ReportFileNotPresent("download");

}
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

void File_Erase() { // This gets called twice, the first pass selects the input, the second pass then processes the command line arguments
  if (server.args() > 0 ) { // Arguments were received
    if (server.hasArg("Erase")) Internal_file_erase(server.arg(0));
  }
  else SelectInput("File erase", "Introduzca el nombre para eliminar", "Erase", "Erase");
}
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void Internal_file_erase(String filename) {

  File erase = SPIFFS.open("/" + filename);
  if (erase) {
    erase.close();
    SPIFFS.remove("/" + filename);
    server.sendHeader("Content-Type", "text/text");
    server.sendHeader("Content-Disposition", "attachment; filename=" + filename);
    server.sendHeader("Connection", "close");
    server.streamFile(erase, "application/octet-stream");

  } else ReportFileNotPresent("Erase");

}
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void SendHTML_Header() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", ""); // Empty content inhibits Content-length header so we have to close the socket ourselves.
  append_page_header();
  server.sendContent(webpage);
  webpage = "";
}
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void SendHTML_Content() {
  server.sendContent(webpage);
  webpage = "";
}
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void SendHTML_Stop() {
  server.sendContent("");
  server.client().stop(); // Stop is needed because no content length was sent
}
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void SelectInput(String heading1, String heading2, String command, String arg_calling_name) {
  SendHTML_Header();
  webpage += F("<h3 class='rcorners_m'>"); webpage += heading1 + "</h3><br>";
  webpage += F("<h3>"); webpage += heading2 + "</h3>";
  webpage += F("<FORM action='/"); webpage += command + "' method='post'>"; // Must match the calling argument e.g. '/chart' calls '/chart' after selection but with arguments!
  webpage += F("<input type='text' name='"); webpage += arg_calling_name; webpage += F("' value=''><br>");
  webpage += F("<type='submit' name='"); webpage += arg_calling_name; webpage += F("' value=''><br><br>");
  append_page_footer();
  SendHTML_Content();
  SendHTML_Stop();
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void ReportFileNotPresent(String target) {
  SendHTML_Header();
  webpage += F("<h3>File does not exist</h3>");
  webpage += F("<a href='/"); webpage += target + "'>[Back]</a><br><br>";
  append_page_footer();
  SendHTML_Content();
  SendHTML_Stop();
}
//************************************ Sensors!!

float batteryVoltage()
{
  float voltage_aux = (float)analogRead(33);
  voltage_aux = (float)voltage_aux * 4.17 / 1420;
  //Serial.println(voltage_aux);
  return voltage_aux;
}

void send2ThinkSpeak()
{
  // set the fields with the values

  ThingSpeak.setField(5, batteryVoltageADC);


  //myStatus = comfortStatus;

  // set the status
  //ThingSpeak.setStatus(myStatus);

  // write to the ThingSpeak channel
  int x = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
  if (x == 200) {
    Serial.println("Channel update successful.");
  }
  else {
    Serial.println("Problem updating channel. HTTP error code " + String(x));
  }
}

void write2File()
{
  File file = SPIFFS.open("/sensor.txt", FILE_APPEND);

  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }
  else {
    file.println();
    file.print("| ");
    file.print(formattedDate);
    file.print(" |# ");
    file.print(actualTicks);
    file.print("| ");
    file.print(actualFrequency);
    file.print("Hz | ");
    file.print(actualFlow);
    file.print("L/min | ");
    file.print(actualVolume);
    file.print("L | ");
    file.print("Batt ADC: ");
    file.print(batteryVoltageADC);
    file.print("V |");
    file.close();
  }

}

void listAllFiles() {

  File root = SPIFFS.open("/");

  File file = root.openNextFile();

  while (file) {
    Serial.print("FILE: ");
    Serial.println(file.name());

    file = root.openNextFile();
  }

}

void printData()
{
  Serial.println();
  Serial.print("| ");
  Serial.print(formattedDate);
  Serial.print(" |# ");
  Serial.print(actualTicks);
  Serial.print("| ");
  Serial.print(actualFrequency);
  Serial.print("Hz | ");
  Serial.print(actualFlow);
  Serial.print("L/min | ");
  Serial.print(actualVolume);
  Serial.print("L | ");
  Serial.print("Batt ADC: ");
  Serial.print(batteryVoltageADC);
  Serial.print("V |");
}

//Frequency measurement and flow sensor

float frequencyMeasure(unsigned long timeRunningAux)
{
  return (float)tickCounter / ((float)timeRunningAux / 1000.0);
}

void addTick() {
  tickCounter++;
}

void resetTick() {
  tickCounter = 0;
}

float frequency2Flow(float frequencyAux) { //Returns L/min

  return 0.122 * frequencyAux;
}

float flow2Volume(float flowAux , unsigned long timeRunningAux) {
  return flowAux * ((float)timeRunningAux / 60000);
}
