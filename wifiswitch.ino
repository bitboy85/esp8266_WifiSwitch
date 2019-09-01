////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Wifi Relay Control v0.1
// This Software is designed to work with the ESP8266 - 01 in combination with the ESP8266 Relay Module
// from LC Technology.
//
// LICENSE
// ===============
//
//
//
//
//
//
//
// WIRING
// ===============
// GPIO02 --- DS18B20 Data Pin
//         |
//        [R] R = 4.7 kOhm
//         | 
//        VCC
//
//
// GPIO0 --- Button --- GND
//
// 
// TROUBLESHOOTING
// ===============
// Relay won't turn on         - Check input voltage, should be 5V
//                             - Check if R4 on the Board is installed, if yes, remove it
//
// Relay clicking during start - Put a SMD capacitor at the place where R4 should be. Reason: the used chip pulls all Pins high during startup. The capacitor will catch the peak.
////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <ESP8266WiFi.h>          // https://github.com/esp8266/Arduino
#include <OneWire.h>              // include with Arduino library manager
#include <DallasTemperature.h>    // include with Arduino library manager
#include <EEPROM.h>
#include <ezTime.h>               // include with Arduino library manager
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          // https://github.com/tzapu/WiFiManager
#include <timer.h>                // include with Arduino library manager: arduino-timer by Michael Contreras
#include "ressources.h"           // contains images and fonts used in html https://unicodepowersymbol.com/


/////////////////////////////////////////////////////////
// Global Definitions
/////////////////////////////////////////////////////////
//////////////////////////////
// Change to your requirements
//////////////////////////////
#define           DEF_HOSTNAME    "wifiswitch"    // Hostname used after wifi setup, max 15 characters
#define           DEF_TIMEZONE    "Europe/Berlin" // Default Timezone to start with
#define           USE_FAHRENHEIT  0               // Use Fahrenheit instead of Celsius
#define           MAX_TIMER       20              // Maximum usable timer entries
#define           RESET_TIME      5000            // press button for 5 sec to factory reset
#define           ONE_WIRE_BUS    2               // Pin for DS18B20 one wire temperature sensor
#define           BTN_PIN         0               // Pin for connected button
#define           BASE_ADDR_TIMER 60              // Start address in EEPROM of timer entries use only if flash is corrupt, this shouldn't happen
#define           LOOP_DELAY      5               // Delay for each loop, shouldn't be changed.
#define           FIRMWARE_VER    "0.6"           // Firmware versionen given by URL/i

//////////////////////////////
// Vars, don't change
//////////////////////////////
ESP8266WebServer  server(80);
OneWire           oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);
Timezone          myTZ;
String            host_name     = DEF_HOSTNAME;
auto              timer         = timer_create_default();
float             temp          = 0;
int               useFahrenheit = USE_FAHRENHEIT;
int               state         = 0;               // 0 = off, 1 = on
int               startupAction = 0;               // Default Startup action 0 = off; 1 = On; 2 = Restore last state
float             dsbOffset     = -2;              // Many DS18B20 showing 2°C (35.6F) too much for unknown reason so just correct.
String            tzDefault     = DEF_TIMEZONE;    // Giving an Example at first start
String            tzDBName      = "";
int               btncounter    = 0;
int               aTimerTask[MAX_TIMER][4];        // byte 0 = Hour, 1 = Minute, 2 = Seconds, 3 = Action and Weekday; Initialize to 255 to mark end <= Not Working :(
char              chost[16];                       // TODO: for an unknown reason, converting hostname is not working if declared in setup part but working in public???? 
    
String            resettrigger;

/////////////////////////////////////////////////////////
// General Functions
/////////////////////////////////////////////////////////

byte getState(){
  return state;
}


/////////////////////////////////
// Note: For some reasons
// the interupt sometimes
// triggers on its own
// resetting the device unwanted.
// This part is a reminder for
// not trying it again.
/////////////////////////////////
/*
void ICACHE_RAM_ATTR handleButtonChange(){

  if (digitalRead(BTN_PIN) == 0){
    //Button pressed
    ms_btn_down = millis();
  }
  else if (digitalRead(BTN_PIN) == 1){
    //Button released
    ms_btn_up = millis();
    btn_timediff = ms_btn_up - ms_btn_down;
  }
}
*/

/////////////////////////////
// Functions to store strings
/////////////////////////////
void StringToEEPROM(int StartAddress, String Data){
  int   datalen = Data.length();
  int   i=0;
  byte  c;
  
  if (datalen > 0){
    for (int x = 0; x < datalen; x++)     //loop through each character and save it to eeprom
    {
      i++;
      char c = Data[x];
      EEPROM.write(StartAddress+x, c);
    }
    EEPROM.write(StartAddress+i, int(0)); //Write 0 Termination to mark end of the string
    EEPROM.commit();  
  }
}

String EEPROMtoString(int StartAddress){
  char    c;
  int     address = StartAddress;
  String  Data = "";

  do                                      //loop through each character until String termination 0 is reached
  {
    c = EEPROM.read(address);
    if (c == 255){continue;};             //No existing value, EEPROM unused
    Data += c;
    address++;
  } while (EEPROM.read(address) != 0);

  return Data;
}

/////////////////////////////
// Read Temperature
/////////////////////////////
void GetTemp(){
  DS18B20.requestTemperatures();
  if (useFahrenheit == 1){
    temp = DS18B20.getTempFByIndex(0) + dsbOffset; 
  }
  else {
    temp = DS18B20.getTempCByIndex(0) + dsbOffset; 
  }
}

bool CheckTemp(void *){
  GetTemp();
  return true;  
}

/////////////////////////////
// Turn On / Off
/////////////////////////////
void turnOff(){
  byte open[] = {0xA0, 0x01, 0x00, 0xA1};
  Serial.write(open, sizeof(open));
  if (state != 0){                       //Save value if not already off
    EEPROM.write(21, 0);
    EEPROM.commit();  
  }

  state = 0;  
}

void turnOn(){
  byte close[] = {0xA0, 0x01, 0x01, 0xA2};
  Serial.write(close, sizeof(close));
  if (state != 1){                      //Save value if not already on
    EEPROM.write(21, 1);
    EEPROM.commit();
  } 
  state = 1;
}

void WEBturnOn(){
  turnOn();
  server.send(200);
}

void WEBturnOff(){
  turnOff();
  server.send(200);
}

/////////////////////////////
// Timer functions
/////////////////////////////

// Definition of action_day
// Weekday    Bit  Dec
// Sunday     0 =  1
// Monday     1 =  2
// Tuesday    2 =  4
// Wednesday  3 =  8
// Thursday   4 = 16
// Friday     5 = 32
// Saturday   6 = 64
// Action     7 = 128
bool IsBitSet(byte toCheck, int pos)
{
   return (toCheck & (1 << pos)) != 0;
}

bool CheckTimerTask(void *){
  int aTimerCount = MAX_TIMER;  
  for (int z = 0; z < aTimerCount; z++) {
    if (aTimerTask[z][0] == 255){continue;} //Skip unused entries
    if (aTimerTask[z][2] == myTZ.dateTime("s").toInt() ){
      if (aTimerTask[z][1] == myTZ.dateTime("i").toInt() ){
        if (aTimerTask[z][0] == myTZ.dateTime("G").toInt()) {
          if (IsBitSet(aTimerTask[z][3], myTZ.dateTime("w").toInt()-1)){
            if (IsBitSet(aTimerTask[z][3], 7)){
              turnOn();
              break;
            }
            else{
              turnOff();
              break;
            } // Action
          } // Weekday
        } // Hour
      } // Minute
    } // Second
  } // End For
  return true;
}

void DeleteTimer(int EntryID){
  int i;

  //Input validation
  if ((EntryID < 0) || (EntryID > MAX_TIMER - 1)){
    return;
  }

  //Delete from Timer array
  for (i = 0; i<4; i++){
    aTimerTask[EntryID][i] = 255; 
  }
  
  // Delete from EEPROM
  for (i = 0; i<4; i++){
    EEPROM.write(BASE_ADDR_TIMER + (EntryID*4) + i, 255);  
  }
  EEPROM.commit();
}

void DeleteTimerWeb(){
  int i, ID;
  String page;

  for (int i = 0; i <= server.args(); i++) {
    if (server.argName(i) == "id"){
      ID = server.arg(i).toInt();
    }
  }
  DeleteTimer(ID);
  
  //redirects back to timer page
  page = "<html><head>";
  page += "<meta http-equiv=\"refresh\" content=\"0; URL=/Timer\"></head></html>";
  server.send(200, "text/html", page);  
}

/* #######################################################
 * EEEPROM Layout; This is repeated for every entry
 * A position is usable if value for hour is 255 (initial value)
 * Max Entries = 20 = 80 Bytes
 * Address    Size    Usage
 * 60         1       Hour
 * 61         1       Minute
 * 62         1       Seconds
 * 63         1       Action + Days
 * ########################################################
 */

void SaveTimer(){
  int i, TimerTaskID;
  int thour, tmin, tsec;
  int action_day  = 0;
  int counter     = 0;
  String page;

  for (int i = 0; i <= server.args(); i++) {

    if (server.argName(i) == "min"){
      tmin = server.arg(i).toInt();
      //Input validation
      if ((tmin < 0) || (tmin > 59)){
        tmin = 0;
      }
    }
    else if (server.argName(i) == "sec"){
      tsec = server.arg(i).toInt();
      //Input validation
      if ((tsec < 0) || (tsec > 59)){
        tsec = 0;
      }
    }
    else if (server.argName(i) == "hour"){
      thour = server.arg(i).toInt();
      //Input validation
      if ((thour < 0) || (thour > 24)){
        thour = 0;
      }
    }
    else if (server.argName(i) == "mo"){
        if (server.arg(i) == "on") {
            action_day += 2;
        }
    }
    else if (server.argName(i) == "tu"){
        if (server.arg(i) == "on") {
            action_day += 4;
        }
    }
    else if (server.argName(i) == "we"){
        if (server.arg(i) == "on") {
            action_day += 8;
        }
    }
    else if (server.argName(i) == "th"){
        if (server.arg(i) == "on") {
            action_day += 16;
        }
    }
    else if (server.argName(i) == "fr"){
        if (server.arg(i) == "on") {
            action_day += 32;
        }
    }
    else if (server.argName(i) == "sa"){
        if (server.arg(i) == "on") {
            action_day += 64;
        }
    }
    else if (server.argName(i) == "su"){
        if (server.arg(i) == "on") {
            action_day += 1;
        }
    }
    else if (server.argName(i) == "action"){
        if (server.arg(i) == "on") {
            action_day += 128;
        }
    }
  } //End loop

  if ((action_day != 0) && (action_day != 128)) {       //No Days selected => No need to store
    //Find empty place in EEPROM to store the schedule
    for (int i = 0; i < MAX_TIMER; i++) {
      if (EEPROM.read(BASE_ADDR_TIMER + (i*4)) == 255){
        EEPROM.write(BASE_ADDR_TIMER + (i*4), thour);
        EEPROM.write(BASE_ADDR_TIMER + 1 + (i*4), tmin);
        EEPROM.write(BASE_ADDR_TIMER + 2 + (i*4), tsec);
        EEPROM.write(BASE_ADDR_TIMER + 3 + (i*4), action_day);
        TimerTaskID=i;
        break;
      }     
    }
    EEPROM.commit();
  
    aTimerTask[TimerTaskID][0] = thour;                  // Hours
    aTimerTask[TimerTaskID][1] = tmin;                   // Minutes
    aTimerTask[TimerTaskID][2] = tsec;                   // Seconds
    aTimerTask[TimerTaskID][3] = action_day;             // Action + Days
  }

  //redirects back to setup
  page = "<html><head>";
  page += "<meta http-equiv=\"refresh\" content=\"0; URL=/Timer\"></head></html>";
  server.send(200, "text/html", page);
}


/////////////////////////////
// String Format
/////////////////////////////
String GetDays(int ActionDays){
  int i;
  String tmp;
  if (IsBitSet(ActionDays, 1)){
    tmp += "Mo";
  }
  if (IsBitSet(ActionDays, 2)){
    tmp += ",Tu";
  }
  if (IsBitSet(ActionDays, 3)){
    tmp += ",We";
  }
  if (IsBitSet(ActionDays, 4)){
    tmp += ",Th";
  }
  if (IsBitSet(ActionDays, 5)){
    tmp += ",Fr";
  }
  if (IsBitSet(ActionDays, 6)){
    tmp += ",Sa";
  }
  if (IsBitSet(ActionDays, 0)){
    tmp += ",Su";
  }
  if (tmp.startsWith(",")){
    tmp.remove(0,1);
  }
  return tmp;
}

String GetAction(int ActionDays){
  int i;
  String tmp;
  if (IsBitSet(ActionDays, 7)){
    tmp = "ON";
  }
  else{
    tmp = "OFF";
  }
  return tmp;
}

String AddLZero(int Value){
  String tmp;
  if (Value < 10){
    tmp = "0" + String(Value);
  }
  else {
    tmp = String(Value);
  }
  return tmp;
}

/////////////////////////////
// Reset All Settings
/////////////////////////////
void factoryReset(){
/*
  WiFiManager wifiManager;
  wifiManager.resetSettings();
  WiFi.disconnect();
  for (int i = 0 ; i < EEPROM.length() ; i++) {
    EEPROM.write(i, 255);
  }
  EEPROM.commit();
  delay(2000);
  ESP.restart();
*/
  resettrigger = myTZ.dateTime();
}

/////////////////////////////////////////////////////////
// HTML Functions
/////////////////////////////////////////////////////////

void sendState(){
  String json;
  json = "{\"state\":";
  json += state;
  json += "}";
  server.send(200, "application/json", json);
}

void sendTemp(){
  String json;
  json = "{\"temperature\":";
  json += temp;
  json += "}";
  server.send(200, "application/json", json);
}

void sendInfo(){
  String json;
  json = "{\"firmware\":";
  json += FIRMWARE_VER;
  json += "}";
  server.send(200, "application/json", json);
}

/* EEPROM Address Layout
 * Address  Size(B)   Function
 * 0        1         If > 0 Size of the hostnamestring, if = 0 => no hostname set (use generic)
 * 1-15     15        Store Hostname (15 Characters) [Windows does not allow netbios names larger than 15 chars]
 * 16       1         unused
 * 17       1         unused
 * 18       1         Use Fahrenheit true (1) or false (0)
 * 19       1         Temperature correction if dsb is showing too high (around 2C in many cases reported in internet forums) is stored as (cor*10)+50 to save a positive int value:ö.
 * 20       1         Startup Action 0=Off 1=On 2=Restore last state
 * 21       1         Last set relay state 
 * 22       30        String Timezone, See https://en.wikipedia.org/wiki/List_of_tz_database_time_zones
 * 52       8         Unused
 * 60       x         Timer
 */

//Function to save setting -> write to flash
void SetupSave() { 
  String  host;
  String  tmp;
  String  page;
  int     hostlen;
  int     useF;
  int     toffset = 0;


/*
 * TODO: INPUT VALIDATION!
 */

  //loop through all params
  for (int i = 0; i <= server.args(); i++) {

    if (server.argName(i) == "host"){ // handle hostname
      host = server.arg(i);
      hostlen = host.length();
      if (hostlen > 15) {
        host = host.substring(0,14);
        hostlen = 15;  
      }
      if (hostlen > 0){
        EEPROM.write(0, hostlen);
        for (int x = 0; x < hostlen; x++)  // loop through each character and save it to eeprom
        {
          char c = host[x];
          EEPROM.write(x+1, c);
        }
      }
      else {                               // hostname is empty, fall back to default
        host = DEF_HOSTNAME;
        hostlen = host.length();
        for (int x = 0; x < hostlen; x++)  // loop through each character and save it to eeprom
        {
          char c = host[x];
          EEPROM.write(x+1, c);
        } 
      }
    }
    else if (server.argName(i) == "tunit"){
        if (server.arg(i) == "F") {;
          useF = 1; 
          EEPROM.write(18, 1);
        }
        else {
          useF = 0;
          EEPROM.write(18, 0);
        }
    }
    else if (server.argName(i) == "toffset"){   // possible values -3.0, -2.5, -2.0, -1.5, -1.0 , 0.5, 0, 0.5, 1, 1.5, 2.0, 2.5, 3.0 
        toffset = (server.arg(i).toFloat() * 10) + 50; //multiply by ten to allow saving it as byte, Add 400 because its more easy to store positive values
        EEPROM.write(19, toffset);
    }
    else if (server.argName(i) == "stuact"){
        if (server.arg(i) == "stuon") {;
          startupAction = 1; 
          EEPROM.write(20, 1);
        }
        else if (server.arg(i) == "stures") {
          startupAction = 2;
          EEPROM.write(20, 2);
          EEPROM.write(21, state);  
        }
        else {
          startupAction = 0;
          EEPROM.write(20, 0);     
        }
    }
    else if (server.argName(i) == "tzDBName"){
      tzDBName = server.arg(i);
      StringToEEPROM(22, tzDBName);
    }
  } // End loop
  EEPROM.commit();

  // Show a debug and reload page (forward to new hostname possible?)
  page = "<html><head>";
  page += "<meta http-equiv=\"refresh\" content=\"10; URL=http://";
  page += host;
  page += "\">";
  page += "<meta name=\"viewport\" charset=\"utf-8\" content=\"width=device-width, user-scalable=0, initial-scale=1\">";
  page += "</head><body><h1>&#8987; Configuration is applied. Device will reboot. Automatic redirecting in 10 seconds.&#8987;</h1></br>Saved Values:</br>";
  page += "Hostlen: ";
  page += hostlen;  
  page += "<br>Hostname: ";
  page += host;
  page += "</br>Timezone: ";
  page += tzDBName;
  page += "</br>Startup state: ";
  page += startupAction;
  page += "</br>Use Fahrenheit: ";
  page += useF;
  page += "</br>Temperature correction: ";
  page += (toffset-50)/10;
  
  server.send(200, "text/html", page);
  delay(2000);   // Without delay the page is not shown
  ESP.restart(); // restart to make settings work
} // End func SetupSave


/////////////////////////////
// Debug Page
// Show all public variables
// and EEPROM values
/////////////////////////////
void DebugWeb() {
  String debug = "";

  debug += "<html><head>";
  debug += "<meta name=\"viewport\" charset=\"utf-8\" content=\"width=device-width, user-scalable=0, initial-scale=1\">";
  debug += "</head><body>";
  debug += "<h1>Debug</h1>";
  debug += "<h2>Defines</h2>";
  debug += "DEF_HOSTNAME: ";
  debug += DEF_HOSTNAME;
  debug += "<br>";
  debug += "DEF_TIMEZONE: ";
  debug += DEF_TIMEZONE;
  debug += "<br>"; 
  debug += "USE_FAHRENHEIT: ";
  debug += USE_FAHRENHEIT;
  debug += "<br>";
  debug += "MAX_TIMER : ";
  debug += MAX_TIMER ;
  debug += "<br>";
  debug += "ONE_WIRE_BUS: ";
  debug += ONE_WIRE_BUS;
  debug += "<br>";
  debug += "BTN_PIN: ";
  debug += BTN_PIN;
  debug += "<br>";
  debug += "BASE_ADDR_TIMER: ";
  debug += BASE_ADDR_TIMER;
  debug += "<br>";
  debug += "LOOP_DELAY: ";
  debug += LOOP_DELAY;
  debug += "<br>";
  debug += "FIRMWARE_VER : ";
  debug += FIRMWARE_VER ;
  debug += "<br>";
  debug += "<h2>Global Var</h2>";
  debug += "host_name: ";
  debug += host_name;  
  debug += "<br>temp: ";
  debug += temp;
  debug += "</br>state: ";
  debug += state;
  debug += "</br>StartupAction: ";
  debug += startupAction;
  debug += "</br>dsbOffset: ";
  debug += dsbOffset;
  debug += "</br>tzDefault: ";
  debug += tzDefault;
  debug += "</br>chost: ";
  debug += chost;  
  debug += "</br>resettrigger: ";
  debug += resettrigger;

  debug += "<h2>EEPROM</h2>";
  debug += "value 255 = unused</br></br>";
  debug += "eeprom 0 (hostlen): ";
  debug += EEPROM.read(0);
  debug += "</br>";

  debug += "eeprom 1-15 (hostname): ";
  for (int x = 1; x < 16; x++) 
  {
    //debug += "eeprom ";
    //debug += x;
    //debug += ": ";
    debug += EEPROM.read(x);
    debug += " ";
  }

  debug += "</br>eeprom 18 (use Fahrenheit): ";
  debug += EEPROM.read(18);
  debug += "</br>";

  debug += "eeprom 19 (temperature correction): ";
  debug += EEPROM.read(19);
  debug += "</br>";

  debug += "eeprom 20 (startupAction): ";
  debug += EEPROM.read(20);
  debug += "</br>";

  debug += "eeprom 21 (Last Relay state): ";
  debug += EEPROM.read(21);
  debug += "</br>";

  debug += "eeprom 22-52 (timezone): ";
  for (int x = 22; x < 53; x++) 
  {
    //debug += "eeprom ";
    //debug += x;
    //debug += ": ";
    debug += EEPROM.read(x);
    debug += " ";
  }

  debug += "</br></br>eeprom 60-x (timer): ";
  debug += "</br>Hour Min Sec Action+Days";
  for (int i = 0; i < MAX_TIMER; i++) {
    debug += "</br>timer ";
    debug += i;
    debug += ": ";
    debug += EEPROM.read(BASE_ADDR_TIMER + (i*4));     // Hours
    debug += " ";
    debug += EEPROM.read(BASE_ADDR_TIMER + 1 + (i*4)); // Minutes
    debug += " ";
    debug += EEPROM.read(BASE_ADDR_TIMER + 2 + (i*4)); // Seconds
    debug += " ";
    debug += EEPROM.read(BASE_ADDR_TIMER + 3 + (i*4)); // Action + Days
  }
  
  debug += "</body></html>";
  server.send(200, "text/html", debug);
}

/////////////////////////////
// Create HMTL Template
/////////////////////////////
String createBaseHTML(){
    String css = "";
    css += "html {background: #d6eaf8; font-family: Arial, Helvetica, sans-serif; text-align: center;}";
    css += ".clear:after {clear:both; display:table; content:\"\";}";
    css += ".nav {max-width: 400px; margin:0 auto; padding: 5px 0 10px 0; font-variant: small-caps; font-size: 1.3em;}";
    css += ".sensors {max-width: 400px; margin:0 auto; padding: 3px 0 3px 0; font-size: 1em;}";
    css += "span.one {width: 33.33%;}"; //* if (isset(.celsius) {width: 25%} else {33.33%}"; //     ----- class (.one/.two) anschließend entfernen 
    css += "span.two {width: 25%;}";
    css += "span + a { display: inline-block; width: 100%}";
    css += "a {text-decoration: none; color:black;}";
    css += ".green {background: #98fb98;}";
    css += ".red {background: #F08080;}";
    css += ".orange {background: #FF9966;}";
    css += ".blue {background: #66CCFF;}";
    css += ".headerleft {float:left;}";
    css += ".right {float:right;}";
    css += ".button {border: 1px solid grey; color: white; box-shadow: 2px 2px grey; padding: 30px 50px; margin: 20px 20px;}"; //------ bei buttongröße das padding ändern -----
    css += ".button2 {border: 1px solid grey; color: white; box-shadow: 2px 2px grey; padding: 30px 40px; margin: 0 5px;}"; //------ bei buttongröße das padding ändern ----- 
    css += ".button:active {box-shadow: inset 2px 2px grey;}";
    css += ".buttonOnOff {font-family: 'iec_symbols_unicoderegular';padding: 20px 40px;font-size:40px;}";
    css += ".buttonText {font-variant:small-caps;padding: 15px 30px;font-size:30px;text-decoration: none; margin:0 10px;}";
    css += ".debug {margin:20px 0;}";
    css += ".content {max-width: 400px; margin:10px auto;}";
    css += ".wrapper {padding-bottom: 10px; border-bottom: 3px solid grey; margin-bottom: 20px;}";
    css += ".line {max-width:400px;border-bottom:2px solid grey; margin: 0 auto}";
    css += ".button-bottom{margin:0 auto; margin-top:15px; max-width:400px;}";
    
    css += "@media screen and (max-width: 400px)";
    css += "{";
    css +=   "input[type=\"checkbox\"], input[type=\"radio\"] {margin-left:0px; margin-right:2px; height:22px;width:22px; vertical-align:bottom;}";
    css +=   "input[type=\"text\"]{margin-top:10px; width:140px; padding:10px;}";
    css +=   ".button2{padding:20px 25px;}";
    css += "}";
    
    String html = "";
    html += "<!DOCTYPE HTML>";
    html += "<html>";
    html +=    "<head><title>";
    html +=    host_name;
    html +=    "</title><link href=\"data:image/png;base64,";
    html +=    favico;
    html +=    "\" rel=\"icon\" type=\"image/png\">";
    html +=    "<meta name=\"viewport\" charset=\"UTF-8\" content=\"width=device-width, user-scalable=0, initial-scale=1\">";
    html +=    "<meta http-equiv=\"Cache-Control\" content=\"no-cache, no-store, must-revalidate\">";
    html +=    "<meta http-equiv=\"Pragma\" content=\"no-cache\">";
    html +=    "<meta http-equiv=\"Expires\" content=\"0\">";
    html +=    "<style>";
    html +=    css;
    html +=    "</style></head>";
    html +=    "<body>";

    html +=    "<div class=\"nav clear\">";
    html +=    "<span class=\"one headerleft\" id=\"state\"><a href=\"/\" title=\"State\">";
    if (state){
      html +=  "&#128161;";
    } else {
      html +=  "&#128308;";
    }
    html +=    "<br>State</a></span>";
    html +=    "<span class=\"one right\"><a href=\"/Setup\" title=\"Setup\">&#128295;<br>Setup</a></span>";
    html +=    "<span class=\"one right\"><a href=\"/Timer\" title=\"Timer\">&#9200;<br>Timer</a></span>";
    html +=    "</div>";
    html +=    "<div class=\"line\"></div>";

    html +=    "<div class=\"sensors clear\">";
    html +=    "<span class=\"one headerleft\" id=\"temp\">&#127757; ";
    if (temp < -120) {
      html +=    "n/a";
    }
    else {
      html +=     temp;
      if (useFahrenheit == 1){
        html +=  "°F";
      } else {
        html +=  "°C";
      }
    }
    html +=    "</span>";
    html +=    "<span class=\"one right\">&#128167; n/a</span>";
    html +=    "<span class=\"one right\">&#127774; n/a</span>";
    html +=    "</div>";
    html +=    "<div class=\"line\"></div>";

    html +=    "%HC%";
    html +=    "</body>";
    html += "</html>";
    
    return html;

    /* Not supported UTF8 (android 4):
     *  For later use
     *  Three bars for settings: &#9881; &#9776; 
     *  Thermometer: &#127777; 
     */
} // End Func CreateBaseHTML

/////////////////////////////
// Show root page
/////////////////////////////
void handleRoot() {
    String page = createBaseHTML();
    String root = "<script type=\"text/javascript\">";
    root +=                "function openURL(url){";
    root +=                  "var request = new XMLHttpRequest();"; 
    root +=                  "request.open(\"GET\",\"/\"+url, true);";
    root +=                  "request.addEventListener('load', function(event) {";
    root +=                     "if (request.status == 200){";
    root +=                       "if (url == 'H') {";
    root +=                         "document.getElementById(\"state\").innerHTML=\"&#128161;<br>State\";";
    root +=                       "} else {";
    root +=                         "document.getElementById(\"state\").innerHTML=\"&#128308;<br>State\";";
    root +=                       "}";
    root +=                     "}";
    root +=                  "});";
    root +=                  "request.send(null);";
    root +=                "}" ;
    root +=              "</script>" ;
    
    root +=              "<style>";              
    root +=              "@font-face {";
    root +=                 "font-family: 'iec_symbols_unicoderegular';";                           // Source: https://unicodepowersymbol.com/
    root +=                 "src: url(data:application/font-woff;charset=utf-8;base64,";
    root +=                  IEC_Power_Font;
    root +=                  ") format('woff');font-weight: normal;font-style: normal;}";
    root +=              "</style>";

    root +=                "<div class=\"content\">";
    root +=                  "<button class=\"green button buttonOnOff\" onClick=\"openURL('H')\">&#x23FB;</button>";
    root +=                  "<button class=\"red button buttonOnOff\" onClick=\"openURL('L')\">&#x2B58;</button>";
    //root +=                "</div>";
    //root +=                "<div class=\"button-bottom\">";
    root +=                  "<button class=\"orange button buttonOnOff\" ontouchstart=\"openURL('H')\" ontouchend=\"openURL('L')\" onmousedown=\"openURL('H')\" onmouseup=\"openURL('L')\">&#x23FC;</button>";
    root +=                  "<button class=\"blue button buttonOnOff\" ontouchstart=\"openURL('L')\" ontouchend=\"openURL('H')\" onmousedown=\"openURL('L')\" onmouseup=\"openURL('H')\">&#x23FC;</button>";
    root +=                "</div>";

    page.replace("%HC%", root );
    server.send(200, "text/html", page);
}


/////////////////////////////
// Show Timer page
/////////////////////////////
void WebTimer(){
    String  page = createBaseHTML();
    String  timer = "";
    int     SchedHour = 22, SchedMin = 15, SchedSec = 36;
    int     DoMo = 0, DoTu = 0, DoWe = 0, DoTh = 0, DoFr = 0, DoSa = 0, DoSu = 0;
    int     actionOn = 1;
    int     TimerCounter = 0;
    int     row;

    // Show Current Timer
    timer += "<style>";
    timer += ".tmr0 {background-color:lightyellow;}";
    timer += ".tmr1 {background-color:lightgrey;}";
    timer += "table{border-collapse: collapse;width:100%;}";
    timer += "th {background-color:  bisque}";
    timer += "h3 {margin-bottom: 5px; text-align:left;}";
    timer += "label{margin-right:6px;vertical-align: middle;}";
    timer += "#tmrtime input[type=\"number\"]{width:36px;height:30px;}";
    timer += "#tmrtime {margin-left:5px;text-align:left;margin-top:10px;width:55%; float:left;}";
    timer += "#tmraction{text-align:right;margin-top:10px;height:30px; width:42%; display:inline-block; padding:4px 0px 0px 0px;}";
    timer += "#tmrdays {clear:both; padding-top:5px;}";
    timer += "@media screen and (max-width: 400px)";
    timer +=    "{#tmrtime input[type=\"number\"]{width:26px;height:26px;padding:5px; text-align:center;}";
    timer +=    "#tmrtime {width:50%}";
    timer +=    "#tmraction{width:45%;}";
    timer += "}";
    timer += "</style>";      
    timer += "<div class=\"content\">";
    timer += "<h3>Current Timer</h3>";
    timer += "<table>";
    timer += "<tr><th>Time</th><th>Days</th><th>Action</th><th class=\"tdc\">Delete</th></tr>";
    for (int i = 0; i < MAX_TIMER; i++) {
      if (aTimerTask[i][0] == 255){continue;}   // skip unused
        row = i % 2;
        timer += "<tr class=\"tmr";
        timer += row;
        timer += "\"><td>";
        timer += AddLZero(aTimerTask[i][0]);
        timer += ":";
        timer += AddLZero(aTimerTask[i][1]);
        timer += ":";
        timer += AddLZero(aTimerTask[i][2]);
        timer += "</td><td>";
        timer += GetDays(aTimerTask[i][3]);
        timer += "</td><td>";
        timer += GetAction(aTimerTask[i][3]);
        timer += "</td><td class=\"tdc fsl\">";
        timer += "<a href= \"/DeleteTimer?id=";
        timer += i;
        timer += "\">&#10060;</a>";
        timer += "</td></tr>";
        TimerCounter += 1;    
    }
    timer += "</table>";
    timer += "<div class=\"line\"></div>";

    //Hide form is maximum Timer is reached
    if (TimerCounter < MAX_TIMER){
      timer += "<form action=\"/SaveTimer\">";
      timer += "<div id=\"tmrtime\"><label>Time</label>";
      timer +=    "<input required autocomplete=\"off\" min=\"0\" max=\"24\" type=\"number\" name=\"hour\" placeholder=\"";
      timer +=    SchedHour;
      timer +=    "\">:";
      timer +=    "<input required autocomplete=\"off\" min=\"0\" max=\"59\" type=\"number\" name=\"min\" placeholder=\"";
      timer +=    SchedMin;
      timer +=    "\">:";
      timer +=    "<input required autocomplete=\"off\" min=\"0\" max=\"59\" type=\"number\" name=\"sec\" placeholder=\"";
      timer +=    SchedSec;
      timer += "\"></div>";

      timer += "<div id=\"tmraction\"><label>Action</label>";
      timer +=    "<input type=\"radio\" id=\"on\" name=\"action\" value=\"on\"";
      //  if (actionOn == 1){root += " checked";}  
      timer +=    ">";
      timer +=    "<label for=\"on\">On</label>";
      timer +=    "<input type=\"radio\" id=\"off\" name=\"action\" value=\"off\"";
      //  if (actionOn != 1){root += " checked";}  
      timer +=    ">";
      timer +=    "<label for=\"off\">Off</label>";
      timer += "</div>";

      timer += "<div id=\"tmrdays\">";
      timer += "<input type=\"checkbox\" id=\"mo\" name=\"mo\"";
      //  if (DoMo == 1){timer += " checked";}  
      timer += ">";
      timer += "<label for=\"mo\">Mo</label>";
      timer += "<input type=\"checkbox\" id=\"tu\" name=\"tu\"";
      //  if (DoTu == 1){timer += " checked";}  
      timer += ">";
      timer += "<label for=\"tu\">Tu</label>";    
      timer += "<input type=\"checkbox\" id=\"we\" name=\"we\"";
      //  if (DoWe == 1){timer += " checked";}  
      timer += ">";
      timer += "<label for=\"we\">We</label>";   
      timer += "<input type=\"checkbox\" id=\"th\" name=\"th\"";
      //  if (DoTh == 1){timer += " checked";}  
      timer += ">";
      timer += "<label for=\"th\">Th</label>";   
      timer += "<input type=\"checkbox\" id=\"fr\" name=\"fr\"";
      //  if (DoFr == 1){timer += " checked";}  
      timer += ">";
      timer += "<label for=\"fr\">Fr</label>";
      timer += "<input type=\"checkbox\" id=\"sa\" name=\"sa\"";
      //  if (DoSa == 1){timer += " checked";}  
      timer += ">";
      timer += "<label for=\"sa\">Sa</label>";    
      timer += "<input type=\"checkbox\" id=\"su\" name=\"su\"";
      //  if (DoSu == 1){timer += " checked";}  
      timer += ">";
      timer += "<label for=\"su\">Su</label>";
      timer += "</div>";

      timer += "<div class=\"line\"></div>";
      timer += "<div class=\"button-bottom\">";
      timer += "<input class=\"green button buttonText\" type=\"submit\" value=\"Create schedule\">";
      timer += "</div>";
      timer += "</form>";
    }
    timer += "</div>";
    
    page.replace("%HC%", timer );
    server.send(200, "text/html", page);
}

/////////////////////////////
// Show setup page
/////////////////////////////
void handleSetup() {
    String page = createBaseHTML();
    String settings = "";

    settings += "<style>";
    settings += "#save .left {";
    settings +=   "text-align:right;";
    settings +=   "vertical-align: middle;";
    settings +=   "padding-right:10px;";
    settings +=   "width: 170px;";
    settings +=   "display:inline-block;";
    settings += "}";
    settings += "input[type=\"text\"]{margin-top:5px; width:140px; padding:5px;}";
    settings += "input[type=\"checkbox\"], input[type=\"radio\"] {margin-left:0px; margin-right:2px;}";
    settings += "select {margin-top:5px; padding:5px; }";
    settings += ".settings {max-width:400px; margin:10px auto;}";
    settings += ".radiogroup {display:inline-block; padding-top: 8px;}";
    settings += ".radiogroup label{margin-right:8px;}";
    settings += ".lblopt {margin-bottom:5px;}";
    settings += ".extlink {color:blue;}";
    settings += "#formbox {margin:0 auto; max-width:360px; text-align: initial;}";
    settings += "ul {list-style:none; margin:0px; padding-left:0px;}";
    settings += "div label:last-child {margin-right: 0px;}";

    settings += "@media screen and (max-width: 400px)";
    settings += "{";
    settings +=   "#save .left {width: 130px;}";
    settings += "}";   
    settings += "</style>";

    settings += "<div class=\"settings\">";
    settings += "<form id=\"save\" action=\"/Save\">";
    settings +=   "<ul><div id=\"formbox\">";
    settings +=     "<li><label class=\"left\" for=\"host\">Hostname</label><input type=\"text\" autocomplete=\"off\" maxlength=\"15\" name=\"host\" value=\"";
    settings +=     host_name;
    settings +=     "\"/></li>";
    settings +=     "<li><label class=\"left\" for=\"timezone\">Timezone (<a class=\"extlink\" href=\"https://en.wikipedia.org/wiki/List_of_tz_database_time_zones\" title=\"Timezone Wiki\">Wiki</a>)</label>";
    settings +=     "<input type=\"text\" name=\"timezone\" maxlength=\"20\" value=\"";
    settings +=     tzDBName;
    settings +=     "\"/></li>";
    settings +=     "<li><label class=\"left lblopt\" for=\"optstart\">Startup state</label><div id=\"optstart\" class=\"radiogroup\">";
    settings +=       "<input name=\"stuact\" id=\"stuon\" value=\"stuon\" type=\"radio\"";
    if (startupAction == 1){settings += " checked";}
    settings +=       "><label for=\"stuon\">On</label>";
    settings +=       "<input name=\"stuact\" id=\"stuoff\" value=\"stuoff\" type=\"radio\"";
    if (startupAction == 0){settings += " checked";} 
    settings +=       "><label for=\"stuoff\">Off</label>";
    settings +=       "<input name=\"stuact\" id=\"stures\" value=\"stures\" type=\"radio\"";
    if (startupAction == 2){settings += " checked";}
    settings +=       "><label for=\"stures\">Restore</label></div></li>";
    settings +=     "<li><label class=\"left\" for=\"toffset\">Temperature correction</label><input type=\"text\" autocomplete=\"off\" maxlength=\"4\" name=\"toffset\" value=\"";
    settings +=     dsbOffset;   
    settings +=     "\"/></li>";
    settings +=     "<li><label class=\"left\" for=\"tunit\">Unit</label><select name=\"tunit\"><option value=\"C\">C</option><option value=\"F\">F</option></select></li>";
    settings += "</div></ul>";
    settings += "</div>";
    settings += "<div class=\"line\"></div>";
    settings += "<div class=\"button-bottom\">";
    settings +=   "<button class=\"green button2 buttonText\" type=\"submit\">Save</button>";
    settings +=   "<a href= \"/\"><button class=\"red button2 buttonText\">Cancel</button></a>";
    settings += "</div>";
    settings += "</form>";
         
    page.replace("%HC%", settings );
    server.send(200, "text/html", page);
}


/////////////////////////////////////////////////////////
// START
/////////////////////////////////////////////////////////

void setup() {
    int     hostlen = 0;

    int     x, i;
    int     laststate;
    int     toffset;
    int     useF;

    // Setup Button Pin
    pinMode(BTN_PIN, INPUT_PULLUP);
    // attachInterrupt(BTN_PIN, handleButtonChange, CHANGE); <= Not working stable
    
    Serial.begin(9600);
    delay(3000);                         //wait for relay chip to initialize, otherwise restoring state wont work.

    // Restore Settings
    EEPROM.begin(150);
    
    hostlen = EEPROM.read(0);
    if ((hostlen != 0) && (hostlen != 255)) {
      for (int x = 0; x < hostlen; x++)  //loop through each character and read it from eeprom
      {
        chost[x] += EEPROM.read(x+1);
      }
      host_name = String(chost);
    }
    WiFi.hostname(host_name);    


    toffset = EEPROM.read(19);
    if (toffset != 255) {
      dsbOffset = (toffset - 50) / 10;    
    }

    useF = EEPROM.read(18); 
    if (useF != 255) {
      useFahrenheit = useF;
    }

    if (EEPROM.read(22) != 255){
      tzDBName = EEPROMtoString(22);
    }
    else {
      tzDBName = tzDefault;
    }

    startupAction = EEPROM.read(20);
    if (startupAction != 255) {
      if (startupAction == 0) {
        turnOff();
      }
      else if (startupAction == 1) {
        turnOn();
      }
      else if (startupAction == 2) {
        laststate = EEPROM.read(21);
        if (laststate == 1){
          turnOn();
        }
        else {
          turnOff();
        }
      }      
    }
    else {
      startupAction = 0;
    }
      
    //WiFiManager
    //Local intialization. Once its business is done, there is no need to keep it around
    WiFiManager wifiManager;

    //set custom ip for portal
    //wifiManager.setAPStaticIPConfig(IPAddress(10,0,1,1), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

    //fetches ssid and pass from eeprom and tries to connect
    //if it does not connect it starts an access point with the specified name
    //here  "AutoConnectAP"
    //and goes into a blocking loop awaiting configuration
    wifiManager.autoConnect("Wifi_Switch");
    //or use this for auto generated name ESP + ChipID
    //wifiManager.autoConnect();
    //if you get here you have connected to the WiFi       
    //Serial.println("Wifi connected.)");

    //Sync Time
    waitForSync();
    myTZ.setLocation(tzDBName); //https://en.wikipedia.org/wiki/List_of_tz_database_time_zones
    
    //Read Temperature
    GetTemp();
    if (temp > 80){                                 // In many cases a ds18b20 returns 85 on the first reading, so just do a second
      delay(2000);
      GetTemp();
    }

    // Initialize Timer Array
    for (int i = 0; i < MAX_TIMER; i++) { 
      aTimerTask[i][0] = 255;
      aTimerTask[i][1] = 255;
      aTimerTask[i][2] = 255;
      aTimerTask[i][3] = 255;
    }

    //for (int i = 0; i < MAX_TIMER; i++) {
    //  EEPROM.write(BASE_ADDR_TIMER + (i*4),255);      
    //}
    //EEPROM.commit();
    
    //Read time schedule to ram
    for (int i = 0; i < MAX_TIMER; i++) {
      if (EEPROM.read(BASE_ADDR_TIMER + (i*4)) == 255){continue;}  // skip unused addresses
      
      aTimerTask[i][0] = EEPROM.read(BASE_ADDR_TIMER + (i*4));     // Hours
      aTimerTask[i][1] = EEPROM.read(BASE_ADDR_TIMER + 1 + (i*4)); // Minutes
      aTimerTask[i][2] = EEPROM.read(BASE_ADDR_TIMER + 2 + (i*4)); // Seconds
      aTimerTask[i][3] = EEPROM.read(BASE_ADDR_TIMER + 3 + (i*4)); // Action + Days
    }

    //Register timer functions
    if (temp > -120){timer.every(90000, CheckTemp);} // No need to use Timer if DS18B20 is not connected
    timer.every(1000, CheckTimerTask);

    //Register functions for different pages
    server.on("/", handleRoot);
    server.on("/H", WEBturnOn);
    server.on("/L", WEBturnOff);
    server.on("/S", sendState);
    server.on("/T", sendTemp);
    server.on("/I", sendInfo);    
    server.on("/Save", SetupSave);
    server.on("/Setup", handleSetup);
    server.on("/Timer", WebTimer);
    server.on("/SaveTimer", SaveTimer);
    server.on("/DeleteTimer", DeleteTimerWeb);
    server.on("/Debug", DebugWeb);      
    server.begin();

    wifi_set_sleep_type(MODEM_SLEEP_T); //Enable Modem Sleep to save some energy, its not working by default(?)
}

void loop(void) {
  // Handle Web Access
  server.handleClient();

  if (digitalRead(BTN_PIN) == 0){
    //Button pressed
    btncounter = btncounter + 1;
  }
  
  if ((digitalRead(BTN_PIN) == 1) && (btncounter > 0)){
    //Button not pressed, but was pressed before
    if (btncounter * LOOP_DELAY > 60) {
      if (btncounter * LOOP_DELAY < RESET_TIME){
        if (state == 1){
          turnOff();
        }
        else {
          turnOn();
        }
      }
      else if (btncounter * LOOP_DELAY > RESET_TIME){
        factoryReset();
      }
    }
    btncounter = 0;
  }

  timer.tick();
  
  //Handle Time Events
  //events();
  delay(LOOP_DELAY); // Allow to go into sleep, seems not working without delay command
}
