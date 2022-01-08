# wakeywakeylamp

Alexa controlled daylight lamp for ESP32/ESP8266

2022 Dan Dooré

**Libraries used:**

Fauxmo ESP - https://github.com/vintlabs/fauxmoESP

Dimmable Light - https://github.com/fabianoriccardi/dimmable-light

**Edit libraries:**

Dimmable Light - FILTER_INT_PERIOD - uncomment FILTER_INT_PERIOD define at the begin of thyristor.cpp 

Dimmable Light - MONITOR_FREQUENCY - uncomment MONITOR_FREQUENCY define at the begin of thyristor.h

**Changes needed:**

Rename 'credentials.sample.h' to 'credentials.h' and edit to put in WiFi details

**Edit variables:**

Edit the values in the #### section only. 
