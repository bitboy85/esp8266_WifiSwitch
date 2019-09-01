# ESP8266 WifiSwitch

This is my first published project using github. So if anything goes wrong please be patient ;)

The project is designed to work with esp01 in combination with the ESP8266 RELAY Board. Its written with the idea in mind building a wifi switch which can be easily integrated in smart home systems.

It might be useful for beginner just to see different techniques to show an icon using base64encoding or to use javascript in the webpage.

## Features
- Using Wifimanager to setup Wifi
- Define a hostname and timezone
- On / Off over webpage / HTTP Request
- Press and Hold On / Off ~~(only on touch devices)~~
- On / Off using hardware button
- Factory Reset using hardware button
- Weekly timer, up to 20 timer for each day of the week
- Reading Temperature if DS18B20 is installed
- Returns json data for some informations (Relay state, temperature, firmware)
- Every setting is done via http get request (except factory reset), could be done by other devices

## Usage
On first startup or after factory reset, the device creates its own wifi ssid called "wifi_switch". Just connect to it, you should be provided with a page where you can enter your current wifi credentials. After that, the device reboots and connects to your wifi. The default hostname is "wifiswitch" (but it can be changed in the code before compiling).
So just open http://wifiswitch in your browser. Every menu should be self explanatory so i skip this part here.

**Special urls:**
<pre>
- http://wifiswitch/H       Turns relay on
- http://wifiswitch/L       Turns relay off
- http://wifiswitch/S       Returns relay state in json format
- http://wifiswitch/T       Returns temperature in json format (if a ds18b20 is connected of course)
- http://wifiswitch/I       Returns current firmware in json format
- http://wifiswitch/Debug   Shows a page with all variables and EEPROM values
</pre>

## ToDos (hope somebody will help at those points)
- improve HTTP / CSS: Every improvement is welcome. I'm not good at design.
- input validation: There is no input validation at all so some data might crash the device
- License: Not sure about as i'm using third party libraries in it.
- find a way to add more sensors

## Known Issues
~~Sadly for an unknown reason the interrupt for factory reset seems to trigger randomly. That why its code is commented out and giving a time when this occurs.~~
Any idea or bugfix to workaround is welcome :)
