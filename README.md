# ESP8266 WifiSwitch

This is my first published project using github. So if anything goes wrong please be patient ;)

The project is designed to work with esp01 in combination with the ESP8266 RELAY Board. Its written with the idea in mind building a wifi switch which can be easily integrated in smart home systems.

It might be useful for beginner just to see different techniques to show an icon using base64encoding or to use javascript in the webpage.

## Features
- Using Wifimanager to setup Wifi
- Define a hostname and timezone
- On / Off over webpage / HTTP Request
- Press and Hold On / Off (only on touch devices)
- On / Off using hardware buitton
- Factory Reset using hardware button
- Weekly timer, up to 20 timer for each day of the week, could also be setup using http request
- Reading Temperature if DS18B20 is installed
- Returns json data for some informations (Relay state and temperature)

## ToDos (hope somebody will help at those points)
- improve HTTP / CSS: I really tried my best but i think html and css is just a mess. Every improvement is welcome.
- input validation: There is no input validation at all so some data might crash the device
- License: Not sure about as i'm using third party libraries in it.

## Known Issues
Sadly for an unknown reason the interrupt for factory reset seems to trigger randomly. That why its code is commented out and giving a time when this occurs. Any idea or bugfix to workaround is welcome :)
