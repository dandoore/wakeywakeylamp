// WakeyWakeyLamp - Alexa controlled daylight lamp for ESP32/ESP8266
//
// 2022 Dan Dooré
//
// Libraries used:
//
//   Fauxmo ESP - https://github.com/vintlabs/fauxmoESP
//   Dimmable Light - https://github.com/fabianoriccardi/dimmable-light
//
// Changes needed:
//
// Rename 'credentials.sample.h' to 'credentials.h' and edit to put in WiFi details
//
// Edit libraries:
//
// Dimmable Light - FILTER_INT_PERIOD - uncomment FILTER_INT_PERIOD define at the begin of thyristor.cpp 
// Dimmable Light - MONITOR_FREQUENCY - uncomment MONITOR_FREQUENCY define at the begin of thyristor.h
//
// Edit the values in the #### section only. 

// Includes

#include <Arduino.h>
#include <Ticker.h>

#ifdef ESP32
  #include <WiFi.h>
#else
  #include <ESP8266WiFi.h>
#endif

#include <fauxmoESP.h>
#include <dimmable_light_manager.h>
#include "credentials.h"  // Wifi Details 

// ################################################################################################################

// Edit the values in this #### section only. 

// Defines

#define ALEXA_NAME "daylight lamp" // Name used by Alexa for voice commands e.g. 'Alexa, turn on daylight lamp.'
#define LAMP_NAME "daylight" // Name for lamp used by dimmable_light_manager

#define FADETIME 10.00 // Fade up timer in minutes
#define SLEEPVAL 30.00 // Sleep off timer in minutes

#define ONOFFRAMP 5 // Speed for on/off fading ramp period (ms)

#define MINBRIGHT 70 // Minimum brightness (min 0)
#define MAXBRIGHT 255 // Maxiumum brightness (max 255)

#define ALEXA_PORT 80 // Fauxmo Alexa Web Sever port - this is required to be 80 for gen3 devices

// Arduino pin assignments

const int powertoggle = 13; // D7 on ESP8266
const int syncpin = 4; // D2 on ESP8266
const int psmpin = 5; // D1 on ESP8266
const int embeddedLEDpin = 2; // ESP-12 LED

// ################################################################################################################

// Globals

float fadetime; // Total fade time in minutes - this will be a parameter sent to light (maybe sometime soon...)
float period; // Time period between brightness steps in seconds
float sleepvalue; // Time for lamp to stay on once manually lit or dimmer completed

static uint8_t brightnessStep = 0;
static uint8_t count = 0;

// Status flags

bool dimmingup = false;
bool manualonoff = false;
bool alexacommand = false;
bool alexaonoff = false;
bool sleepcounter = false;

// Classes

Ticker dim;
DimmableLightManager dlm;
fauxmoESP fauxmo;

// Procedures

void wifiSetup() {

  // Set WIFI module to STAtion mode
  WiFi.mode(WIFI_STA);

  // Connect
  Serial.printf("[WIFI] Connecting to %s ", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(100);
  }
  Serial.println();

  // Connected!
  digitalWrite(embeddedLEDpin, LOW);
  Serial.printf("[WIFI] STATION Mode, SSID: %s, IP address: %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
}

void fauxmoSetup() {

  // Set up Alexa Fauxmo service

  fauxmo.createServer(true); // not needed, this is the default value
  fauxmo.setPort(ALEXA_PORT);
  fauxmo.enable(true);
  fauxmo.addDevice(ALEXA_NAME);

  // Callback procedure when Alexa is invoked

  fauxmo.onSetState([](unsigned char device_id,
    const char * device_name, bool state, unsigned char value) {

    // Callback when a command from Alexa is received. 
    // You can use device_id or device_name to choose the element to perform an action onto (relay, LED,...)
    // State is a boolean (ON/OFF) and value a number from 0 to 255 (if you say "set kitchen light to 50%" you will receive a 128 here).
    // Just remember not to delay too much here, this is a callback, exit as soon as possible.
    // If you have to do something more involved here set a flag and process it in your main loop.

    Serial.printf("Alexa Command - Device #%d (%s) state: %s value: %d\n", device_id, device_name, state ? "ON" : "OFF", value);

    // Checking for device_id is simpler if you are certain about the order they are loaded and it does not change.
    // Otherwise comparing the device_name is safer.

    if (strcmp(device_name, ALEXA_NAME) == 0) {
      Serial.println("Valid command recieved");
      alexacommand = true;
      alexaonoff = state;
    }
  });
}

void manual_on() {

  DimmableLight * dimLight = dlm.get(LAMP_NAME);

  manualonoff = true;
  if (dimmingup) dimming_cancel(); // If called from fadeup, scrap dimming and make manual

  // Fast fade up from current value to MAXBRIGHT

  for (int count = dimLight -> getBrightness(); count < (MAXBRIGHT + 1); count++) {
    dimLight -> setBrightness(count);
    delay(ONOFFRAMP);
  }

  // Tell Alexa state
  fauxmo.setState(ALEXA_NAME, true, dimLight -> getBrightness());
  
  Serial.println("Manual On: Setting sleep counter");
  sleepcounter = true;
  sleepvalue = SLEEPVAL * 60;
  dim.once(sleepvalue, manual_off); // Call dim sleep counter routine for single instance
}

void manual_off() {

  DimmableLight * dimLight = dlm.get(LAMP_NAME);
  
  manualonoff = false;
  
  // Fast Fade down from current value but only to MINBRIGHT then turn off

  for (int count = dimLight -> getBrightness(); count > MINBRIGHT; count--) {
    dimLight -> setBrightness(count);
    delay(ONOFFRAMP);
  }
  dimLight -> setBrightness(0);


  // Tell Alexa state
  fauxmo.setState(ALEXA_NAME, false, dimLight -> getBrightness());
  
  if (sleepcounter) {
    Serial.println("Manual Off: Clearing sleep counter");
    dim.detach(); // Stop calling dim sleep counter routine
    sleepcounter = false;
  }
}

void dimming_up() {

  fadetime = FADETIME;
  period = (fadetime * 60) / (MAXBRIGHT - MINBRIGHT);
  Serial.println(String("Dimming Time: ") + fadetime + String("mins Dimming Period: ") + period);
  
  // Tell Alexa state
  fauxmo.setState(ALEXA_NAME, true, MINBRIGHT);

  brightnessStep = MINBRIGHT; // Reset dimming start point 
  dim.attach(period, fadeup); // Start Ticker based fadeup routine
  dimmingup = true;
}

void dimming_cancel() {
  DimmableLight * dimLight = dlm.get(LAMP_NAME);
  if (!manualonoff) dimLight -> setBrightness(0);

   // Tell Alexa state
  fauxmo.setState(ALEXA_NAME, false, dimLight -> getBrightness());
  
  dim.detach(); // Stop calling fadeup routine
  dimmingup = false;
}

void fadeup(void) {

  DimmableLight * dimLight = dlm.get(LAMP_NAME);
  dimLight -> setBrightness(brightnessStep);

  if (brightnessStep == MAXBRIGHT) {
    Serial.println("Dimming complete, reverting to manual on");
    manual_on(); // Once top value reached, class as manually powered on now
  } else {
    brightnessStep++;
  }
}

// Setup (called on power up)

void setup() {

  // Serial Console

  Serial.begin(115200);
  delay(2000); // Pause a bit for serial console to catch up
  Serial.println();
  Serial.println();

  // Pins

  pinMode(powertoggle, INPUT_PULLUP); // Manual light switch (pulls to ground)
  pinMode(embeddedLEDpin, OUTPUT); // Initialize GPIO2 pin as an output
  digitalWrite(embeddedLEDpin, HIGH); // Turn off (HIGH=off)

  // Dimmer library
  Serial.println("Dimmable Light for Arduino (Fabiano Riccardi - https://github.com/fabianoriccardi/dimmable-light)");
  Serial.println("Initializing the dimmable light class... ");

  // Add in single light LAMP_NAME
  if (dlm.add(String(LAMP_NAME), psmpin)) {
    Serial.println("Light added correctly");
  } else {
    Serial.println("Light cannot be added");
  }

  DimmableLight::setSyncPin(syncpin);
  DimmableLightManager::begin();
  Serial.println("Light mananger started");
  
  Serial.println("Initializing WiFi... ");
  wifiSetup();
  
  Serial.println("Fauxmo ESP for Arduino (Xose Pérez & Paul Vint - https://github.com/vintlabs/fauxmoESP)");
  Serial.println("Initializing fauxmo for Alexa... ");
  fauxmoSetup();

  Serial.println("Setup Complete.");
}

// Main Loop

void loop() {

  // Debug bits every 5 seconds
  
    static unsigned long last = millis();
    if (millis() - last > 5000) {
        last = millis();
        DimmableLight* dimLight = dlm.get(LAMP_NAME);
        Serial.println(String("[DEBUG] Brightness:") + dimLight->getBrightness());
        Serial.println(String("[DEBUG] Frequency: ") + dimLight->getDetectedFrequency());
        //Serial.println(String("[DEBUG] Manualonoff:") + manualonoff);
        //Serial.println(String("[DEBUG] Alexaonoff:") + alexaonoff);
        // Serial.println(String("[DEBUG] Dimmingup:") + dimmingup);
        //Serial.println(String("[DEBUG] Switch:") + digitalRead(powertoggle));
        Serial.printf("[DEBUG] Free heap: %d bytes\n", ESP.getFreeHeap());
    }
 
  // Handle Alexa ON/OFF

  fauxmo.handle(); // Poll for Alexa command - flags set by callback routine

  if (alexacommand) {
    if (alexaonoff && manualonoff && !dimmingup) { // Command ON : Lamp on manually, not in dim sequence: Do nothing
      Serial.println("Alexa: Already on");
    } else if (alexaonoff && dimmingup && !manualonoff) { // Command ON : Lamp in dim sequence, not manually on: Turn on manually
      Serial.println("Alexa: Cancelling dimming - turning full on");
      dimming_cancel();
      manual_on();
    } else if (alexaonoff && !manualonoff && !dimmingup) { // Command ON : Lamp not manually on and not in dim sequence: Enter dim sequence
      Serial.println("Alexa: Starting dimming up");
      dimming_up();
    } else if (!alexaonoff && manualonoff && !dimmingup) { // Command OFF : Lamp manually on and not in dim sequence: Turn off
      Serial.println("Alexa: Turning off");
      manual_off();
    } else if (!alexaonoff && dimmingup && !manualonoff) { // Command OFF : Lamp in dim sequence, not manually on: Cancel dim & turn off
      Serial.println("Alexa: Cancelling dimming - turning off");
      dimming_cancel();
      manual_off();
    } else if (!alexaonoff && !manualonoff && !dimmingup) { // Command OFF : Lamp not manually on and not in dim sequence: Do nothing
      Serial.println("Alexa: Already off");
    }

    delay(500); // Delay for debounce of Alexa commands as sometimes it sends two - thanks Amazon...
    fauxmo.handle(); // Poll for Alexa command to junk
    fauxmo.handle(); // Poll for Alexa command to junk
    fauxmo.handle(); // Poll for Alexa command to junk
    alexacommand = false;
  }

  // WiFi Watchdog

  // Light ESP LED if connected, otherwise re-init the WiFi

  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(embeddedLEDpin, LOW);
  } else {
    digitalWrite(embeddedLEDpin, HIGH);
    Serial.println("WiFi not connected - restarting");
    wifiSetup();
    fauxmoSetup();
  }

  // Handle button press

  if (digitalRead(powertoggle) == LOW) { // If switch pressed
    if (manualonoff && !dimmingup) { // Switch Pressed : Lamp on manually, not in dim sequence: Turn off
      Serial.println("Switch: Turning off");
      manual_off();
    } else if (dimmingup && !manualonoff) { // Switch Pressed : Lamp in dim sequence, not manually on: Cancel dim & turn off
      Serial.println("Switch: Cancelling dimming - turning off");
      dimming_cancel();
      manual_off();
    } else if (!manualonoff && !dimmingup) { // Switch Pressed : Lamp not manually on and not in dim sequence: Turn on
      Serial.println("Switch: Turning on");
      manual_on();
    }
    delay(300); // Delay for debounce of button
  }
}
