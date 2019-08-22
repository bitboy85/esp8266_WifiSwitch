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

//////////////////////////////
// Vars, don't change
//////////////////////////////
ESP8266WebServer  server(80);
OneWire           oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);
Timezone          myTZ;
auto              timer         = timer_create_default();
float             temp          = 0;
int               useFahrenheit = USE_FAHRENHEIT;
int               state         = 0;               // 0 = off, 1 = on
int               startupAction = 0;               // Default Startup action 0 = off; 1 = On; 2 = Restore last state
float             dsbOffset     = -2;              // Many DS18B20 showing 2°C (35.6F) too much for unknown reason so just correct.
String            tzDefault     = DEF_TIMEZONE;    // Giving an Example at first start
String            tzDBName      = "";
int               ms_btn_down   = 0;
int               ms_btn_up     = 0;
int               btn_timediff  = 0;
int               aTimerTask[MAX_TIMER][4];        // byte 0 = Hour, 1 = Minute, 2 = Seconds, 3 = Action and Weekday; Initialize to 255 to mark end <= Not Working :(
char              chost[16];

String            resettrigger;

/////////////////////////////////////////////////////////
// General Functions
/////////////////////////////////////////////////////////

byte getState(){
  return state;
}

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
  } //Save value if not restoring

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
  //lastcheck = String(myTZ.dateTime());
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
    }
    else if (server.argName(i) == "sec"){
      tsec = server.arg(i).toInt();
    }
    else if (server.argName(i) == "hour"){
      thour = server.arg(i).toInt();
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

/* EEPROM Address Layout
 * Address  Size(B)   Function
 * 0        1         If > 0 Size of the hostnamestring, if = 0 => no hostname set (use generic)
 * 1-17     16        Store Hostname (15 Characters) + 1 String termination \0 [Windows does not allow netbios names larger than 15 chars]
 * 18       1         Use Fahrenheit true (1) or false (0)
 * 19       1         Temperature correction if dsb is showing too high (around 2C in many cases reported in internet forums) is stored as (cor*10)+50 to save a positive int value:ö.
 * 20       1         Restore relay state after power loss true(1) or false (0)
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

    if (server.argName(i) == "host"){ //handle hostname
      host = server.arg(i);
      hostlen = host.length();
      if (hostlen > 0){
        EEPROM.write(0, hostlen);
        for (int x = 0; x < hostlen; x++)  //loop through each character and save it to eeprom
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
    else if (server.argName(i) == "toffset"){   //possible values -3.0, -2.5, -2.0, -1.5, -1.0 , 0.5, 0, 0.5, 1, 1.5, 2.0, 2.5, 3.0 
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
  page += "</head><body>&#8987; Configuration is applied. Automatic redirecting in 10 seconds.&#8987;</br>Saved Values:</br>";
  page += "Hostname: ";
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
// Create HMTL Template
/////////////////////////////
String createBaseHTML(){

    String html = "";
    html += "<!DOCTYPE HTML>";
    html += "<html>";
    html +=    "<head><title>";
    html +=    chost;
    html +=    "</title><link href=\"data:image/png;base64,";
    html +=    favico;
    html +=    "\" rel=\"icon\" type=\"image/png\">";
    html +=    "<meta name=\"viewport\" charset=\"UTF-8\" content=\"width=device-width, user-scalable=0, initial-scale=1\">";
    html +=    "<meta http-equiv=\"Cache-Control\" content=\"no-cache, no-store, must-revalidate\">";
    html +=    "<meta http-equiv=\"Pragma\" content=\"no-cache\">";
    html +=    "<meta http-equiv=\"Expires\" content=\"0\">";
    html +=    "<style>html {display: inline-block; margin: 0px auto; text-align: center; font-family:Arial;background-color: #d6eaf8;}";
    html +=    ".button { background-color: #98FB98; border: 1px solid grey; color: white; padding: 10px 30px; font-variant:small-caps;box-shadow: 2px 2px grey;";
    html +=    "text-decoration: none; font-size: 30px; margin: 20px 10px 20px 10px; cursor: pointer;}";
    html +=    ".button2 {background-color: #F08080;}";
    html +=    "input[type=\"checkbox\"], input[type=\"radio\"]{width:22px;height:22px;}";
    html +=    "label {vertical-align:super; margin-left:0px;}";
    html +=    "a, a:visited, a:hover, a:active {text-decoration: none; color:black}";
    html +=    "table {margin: 0px auto; border-bottom: 3px solid grey;width:100%;max-width:400px;border-collapse: collapse;}";
    html +=    "td {text-align:left;}";
    html +=    "th {text-align:left;}";
    html +=    "#nav {font-variant:small-caps;font-size: 1.2em;}";
    html +=    "#navdiv1 {height:10px; font-size:2em; margin: -14px 0px -8px 0px;}";
    html +=    "#navdiv2 {margin-right:24px; margin-top:8px;}";   
    html +=    ".tdc {text-align:center;}";
    html +=    ".fsl {font-size:1.5em;}";
    html +=    "</style></head>";
    html +=    "<body>";
    html +=    "<table id=\"nav\"><tr><td id=\"state\" style=\"text-align:left\"><a href=\"/\">State ";
    if (state){
      html +=  "&#128161;";
    } else {
      html +=  "&#128308;";
    }
    html +=    "</a></td>";

    if ((temp > -129) && (temp < 80)) { //  DS18B20 giving valid temperature
      html +=     "<td class=\"tdc\">&#127757; ";
      html +=     temp;
      if (useFahrenheit == 1){
        html +=  "°F";
      } else {
        html +=  "°C";
      }
      html +=    "</td>";
    }
    
    html +=    "<td class=\"tdc\"><a href=\"/Timer\">&#9200; Timer</a></td>";
    html +=    "<td style=\"text-align:right;\"><a href=\"/Setup\">Setup &#128295;</a></td></tr></table>";
    html +=    "%HC%";
    html +=    "</body>";
    html += "</html>";
    
    return html;

    /* Not supported UTF8 (android 4):
     *  For later use
     *  Three bars for settings: &#9881; &#9776; 
     *  Thermometer: &#127777; 
     *  Wrench: &#128295;
     */
} // End Func CreateBaseHTML

/////////////////////////////
// Show root page
/////////////////////////////
void handleRoot() {
    String page = createBaseHTML();
    int offset = EEPROM.read(19);
    String root = "<script type=\"text/javascript\">";
    root +=                "function openURL(url){";
    /*root +=                 "alert(code);" */
    root +=                  "var request = new XMLHttpRequest();"; 
    root +=                  "request.open(\"GET\",\"/\"+url, true);";
    root +=                  "request.addEventListener('load', function(event) {";
    root +=                     "if (request.status == 200){";
    root +=                       "if (url == 'H') {";
    root +=                         "document.getElementById(\"state\").innerHTML=\"State &#128161;\";";
    root +=                       "} else {";
    root +=                         "document.getElementById(\"state\").innerHTML=\"State &#128308;\";";
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
    root +=              ".buttonOnOff {font-family: 'iec_symbols_unicoderegular';";
    root +=                  "padding: 15px 40px;font-size:40px;}</style>";
    root +=              "<button class=\"button buttonOnOff\" onClick=\"openURL('H')\">&#x23FB;</button>" ;
    root +=              "<button class=\"button buttonOnOff button2\" onClick=\"openURL('L')\">&#x2B58;</button></br>";
    root +=              "<button class=\"button buttonOnOff\" ontouchstart=\"openURL('H')\" ontouchend=\"openURL('L')\" style=\"background-color:#FF9966\">&#x23FC;</button>";
    root +=              "<button class=\"button buttonOnOff\" ontouchstart=\"openURL('L')\" ontouchend=\"openURL('H')\" style=\"background-color:#66CCFF\">&#x23FC;</button>";
    root += "<br>DEBUG reset:";
    root += resettrigger;

    page.replace("%HC%", root );
    server.send(200, "text/html", page);
}


/////////////////////////////
// Show Timer page
/////////////////////////////
void WebTimer(){
    String  page = createBaseHTML();
    int     SchedHour = 0, SchedMin = 0, SchedSec = 0, DoMo = 0, DoTu = 0, DoWe = 0, DoTh = 0, DoFr = 0, DoSa = 0, DoSu = 0;
    int     actionOn = 1;
    int     TimerCounter = 0;
    String  root = "";
    int     row;

    // Show Current Timer
    root += "<style>";
    root += ".tmr0 {background-color:lightyellow;}";
    root += ".tmr1 {background-color:lightgrey;}";
    root += "</style>";      
    root += "<div style=\"width:400px; text-align:left; margin: 0 auto;\"><h3>Current Timer</h3></div>";
    root += "<table>";
    root += "<tr><th>Time</th><th>Days</th><th>Action</th><th class=\"tdc\">Delete</th></tr>";
    for (int i = 0; i < MAX_TIMER; i++) {
      if (aTimerTask[i][0] == 255){continue;}   // skip unused
        row = i % 2;
        root += "<tr class=\"tmr";
        root += row;
        root += "\"><td>";
        root += AddLZero(aTimerTask[i][0]);
        root += ":";
        root += AddLZero(aTimerTask[i][1]);
        root += ":";
        root += AddLZero(aTimerTask[i][2]);
        root += "</td><td>";
        root += GetDays(aTimerTask[i][3]);
        root += "</td><td>";
        root += GetAction(aTimerTask[i][3]);
        root += "</td><td class=\"tdc fsl\">";
        root += "<a href= \"/DeleteTimer?id=";
        root += i;
        root += "\">&#10060;</a>";
        root += "</td></tr>";
        TimerCounter += 1;    
    }
    root += "</table>";

    //Hide form is maximum Timer is reached
    if (TimerCounter < MAX_TIMER){
      root += "<form action=\"/SaveTimer\">";
      root += "<table>";
  
      root += "<tr>";
      root += "<td>Time ";
      root += "<input style=\"width:40px;height:30px;\" autocomplete=\"off\" min=\"0\" max=\"24\" type=\"number\" name=\"hour\" value=\"";
      root +=   SchedHour;
      root +=   "\">";
      root += "<input style=\"width:40px;height:30px;\" autocomplete=\"off\" min=\"0\" max=\"59\" type=\"number\" name=\"min\" value=\"";
      root +=   SchedMin;
      root +=   "\">";
      root += "<input style=\"width:40px;height:30px;\" autocomplete=\"off\" min=\"0\" max=\"59\" type=\"number\" name=\"sec\" value=\"";
      root +=   SchedSec;
      root +=   "\">";
      root += "</td>";
      root += "<td><label>Action </label>";
      root += "<input type=\"radio\" id=\"on\" name=\"action\" value=\"on\"";
        if (actionOn == 1){root += " checked";}  
      root += ">";
      root += "<label for=\"on\">On</label>";
      root += "<input type=\"radio\" id=\"off\" name=\"action\" value=\"off\"";
        if (actionOn != 1){root += " checked";}  
      root += ">";
      root += "<label for=\"off\">Off</label>"; 
      root += "</td></tr>";
      root += "<tr><td colspan=\"2\">";    
      root += "<input type=\"checkbox\" id=\"mo\" name=\"mo\"";
        if (DoMo == 1){root += " checked";}  
      root += ">";
      root += "<label for=\"mo\">Mo</label>";
      root += "<input type=\"checkbox\" id=\"tu\" name=\"tu\"";
        if (DoTu == 1){root += " checked";}  
      root += ">";
      root += "<label for=\"tu\">Tu</label>";    
      root += "<input type=\"checkbox\" id=\"we\" name=\"we\"";
        if (DoWe == 1){root += " checked";}  
      root += ">";
      root += "<label for=\"we\">We</label>";   
      root += "<input type=\"checkbox\" id=\"th\" name=\"th\"";
        if (DoTh == 1){root += " checked";}  
      root += ">";
      root += "<label for=\"th\">Th</label>";   
      root += "<input type=\"checkbox\" id=\"fr\" name=\"fr\"";
        if (DoFr == 1){root += " checked";}  
      root += ">";
      root += "<label for=\"fr\">Fr</label>";
      root += "<input type=\"checkbox\" id=\"sa\" name=\"sa\"";
        if (DoSa == 1){root += " checked";}  
      root += ">";
      root += "<label for=\"sa\">Sa</label>";    
      root += "<input type=\"checkbox\" id=\"su\" name=\"su\"";
        if (DoSu == 1){root += " checked";}  
      root += ">";
      root += "<label for=\"su\">Su</label>";   
      root += "</td></tr>";
      
      root += "</table>";
      root += "<input class=\"button\" type=\"submit\" value=\"Create schedule\">";
      root += "</form>";
    }

    page.replace("%HC%", root );
    server.send(200, "text/html", page);
}

/////////////////////////////
// Show setup page
/////////////////////////////
void handleSetup() {
    String page = createBaseHTML();
    String root = "";
    root += "<style>.tdl {padding-right:0px;}";
    root += ".tdr {text-align:right;}</style>";
    root += "<form action=\"/Save\">";
    root += "<table>";
    root +=   "<tr><td class=\"tdl\">Hostname</td><td><input style=\"margin-top:6px;\" autocomplete=\"off\" maxlength=\"15\" type=\"text\" name=\"host\" value=\"";
    root +=   chost;
    root +=   "\"></td></tr>";
    root +=   "<tr><td class=\"tdl\">Timezone (<a style=\"color:blue;\" href=\"https://en.wikipedia.org/wiki/List_of_tz_database_time_zones\">Wiki</a>)</td><td><input style=\"margin-top:6px;\" maxlength=\"20\" type=\"text\" name=\"tzDBName\" value=\"";
    root +=   tzDBName;
    root +=   "\"></td></tr>";
    root +=   "<tr><td class=\"tdl\">Startup state</td><td class=\"tdl\">";

    root += "<input style=\"margin-left:0px;\" type=\"radio\" id=\"stuon\" name=\"stuact\" value=\"stuon\"";
    if (startupAction == 1){root += " checked";}  
    root += ">";
    root += "<label for=\"stuon\">On</label>";
    root += "<input type=\"radio\" id=\"stuoff\" name=\"stuact\" value=\"stuoff\"";
    if (startupAction == 0){root += " checked";}  
    root += ">";
    root += "<label for=\"stuoff\">Off</label>";
    root += "<input type=\"radio\" id=\"stures\" name=\"stuact\" value=\"stures\"";
    if (startupAction == 2){root += " checked";}  
    root += ">";
    root += "<label for=\"stures\">Restore</label>";
     
    root +=   "</td></tr>"; 
    root +=   "<tr><td class=\"tdl\">Temperature correction</td><td><input autocomplete=\"off\" maxlength=\"4\" type=\"text\" name=\"toffset\" value=\"";
    root +=   dsbOffset;
    root +=   "\"></td></tr>";
    root +=   "<tr><td class=\"tdl\">Temperature unit</td>";
    root +=       "<td class=\"tdl\"> <select name=\"tunit\">"; 
    root +=            "<option value=\"C\">C</option>";
    root +=            "<option ";
    if (useFahrenheit == 1){root += "selected ";}
    root +=   "value=\"F\">F</option>";
    root +=        "</select></td></tr>";
    root +=   "<tr><td class=\"tdl\"></td><td class=\"tdl\"></td></tr>";
    root += "</table>";
    root += "<input class=\"button\" style=\"margin-left:5px;\" type=\"submit\" value=\"Save\"><a href= \"/\"><input style=\"margin-right:5px;\" class=\"button button2\" type=\"button\" value=\"Cancel\"></a>";
    root += "</form>";          
    page.replace("%HC%", root );
    server.send(200, "text/html", page);
}


/////////////////////////////////////////////////////////
// START
/////////////////////////////////////////////////////////

void setup() {
    int     hostlen = 0;
    char    host_name[16];
    String  shost_name = "";
    int     x, i;
    int     laststate;
    int     toffset;
    int     useF;

    // Setup Button Pin
    pinMode(BTN_PIN, INPUT_PULLUP);
    attachInterrupt(BTN_PIN, handleButtonChange, CHANGE);
    
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
      shost_name = String(chost);
      WiFi.hostname(shost_name);    
    }
    else {
      WiFi.hostname(DEF_HOSTNAME);
    }

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
    if (temp > 80){
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
    server.on("/Save", SetupSave);
    server.on("/Setup", handleSetup);
    server.on("/Timer", WebTimer);
    server.on("/SaveTimer", SaveTimer);
    server.on("/DeleteTimer", DeleteTimerWeb);      
    server.begin();

    wifi_set_sleep_type(MODEM_SLEEP_T); //Enable Modem Sleep to save some Energy,its not working by default(?)
}

void loop(void) {
  // Handle Web Access
  server.handleClient();

  if (btn_timediff > 60) {
    if (btn_timediff < RESET_TIME){
      if (state == 1){
        turnOff();
      }
      else {
        turnOn();
      }
    }
    else if (btn_timediff > RESET_TIME){
      factoryReset();
    }
    ms_btn_down = 0;
    ms_btn_up = 0;
    btn_timediff = 0;
  }

  timer.tick();
  
  //Handle Time Events
  //events();
  delay(1); // Allow to go into sleep, seems not working without delay command
}
