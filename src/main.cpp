// Voltage Protector - simple version
// Board: ESP32 DevKit 38 pin
//
// My bench uses 20V DC instead of 230V mains, so 20V on the bench = 230V.
// The relay switches the motor OFF when
//  - voltage is too high (over voltage)
//  - voltage is too low (under voltage)
//  - current is too high (over current), this one stays off until RESET
// Settings can be changed with the buttons or from the PC (USB or RS485)

#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <driver/gpio.h>

// pins
#define VOLT_PIN 34      // voltage divider 200k + 10k
#define AMP_PIN 35       // 10 ohm shunt, through 22k + 10k divider
#define MENU_BTN 25
#define UP_BTN 26
#define DOWN_BTN 27
#define BUZZER 13
#define RED_LED 32
#define GREEN_LED 33
#define RELAY 23         // LOW = relay on
#define RS485_RX 16
#define RS485_TX 17
#define LCD_ADDR 0x27

// pin mV to grid volts. divider is 21 and 20V means 230V
// if the LCD and multimeter dont match, change this number a little
float VOLT_FACTOR = 21.0 * (230.0 / 20.0) / 1000.0;
// pin mV to amps. divider 3.2, shunt 10 ohm
float AMP_FACTOR = 3.2 / 10.0 / 1000.0;

#define VOLT_DELAY 200    // ms voltage must stay bad before relay goes off
#define AMP_DELAY 1000    // ms current must stay high (motor takes more current at start)

// settings, saved in flash
int overVolt = 255;
int underVolt = 180;
float overAmp = 0.75;
int reconnectTime = 10;  // seconds

LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);
Preferences memory;

float volts = 0;
float amps = 0;
bool relayOn = false;
bool lockedOut = false;    // after over current, needs RESET
String reason = "START";   // why relay is off

unsigned long badVoltSince = 0;    // 0 = voltage is ok
unsigned long highAmpSince = 0;
unsigned long goodVoltSince = 0;

int menuPage = 0;          // 0 = normal screen, 1 to 4 = settings
unsigned long lastButton = 0;
unsigned long menuHeldSince = 0;  // 0 = MENU not pressed
bool menuLongDone = false;

String line1 = "";
String line2 = "";
unsigned long lastLcd = 0;
bool lcdOk = true;


// send text to PC, both USB and RS485
void say(String text) {
  Serial.println(text);
  Serial2.println(text);
}

void beep(int times) {
  for (int i = 0; i < times; i++) {
    ledcWrite(0, 128);
    delay(80);
    ledcWrite(0, 0);
    delay(80);
  }
}

void relay(bool on) {
  relayOn = on;
  if (on) digitalWrite(RELAY, LOW);
  else digitalWrite(RELAY, HIGH);
  digitalWrite(GREEN_LED, on);
}


// ---------- settings ----------

void saveSettings() {
  memory.putInt("ov", overVolt);
  memory.putInt("uv", underVolt);
  memory.putFloat("oc", overAmp);
  memory.putInt("recon", reconnectTime);
}

void loadSettings() {
  overVolt = memory.getInt("ov", 255);
  underVolt = memory.getInt("uv", 180);
  overAmp = memory.getFloat("oc", 0.75);
  reconnectTime = memory.getInt("recon", 10);
}

// keep settings inside limits
void fixSettings() {
  overVolt = constrain(overVolt, 200, 300);
  underVolt = constrain(underVolt, 150, 220);
  if (underVolt > overVolt - 20) underVolt = overVolt - 20;
  overAmp = constrain(overAmp, 0.10, 0.90);
  reconnectTime = constrain(reconnectTime, 3, 300);
}


// ---------- measuring ----------

// average of 40 readings, takes about 10ms
float readPin(int pin) {
  float sum = 0;
  for (int i = 0; i < 40; i++) {
    sum += analogReadMilliVolts(pin);
  }
  return sum / 40;
}

void measure() {
  volts = readPin(VOLT_PIN) * VOLT_FACTOR;
  amps = readPin(AMP_PIN) * AMP_FACTOR;
  if (amps < 0.05) amps = 0;  // esp32 cant read very small values
}


// ---------- protection ----------

void checkProtection() {
  if (relayOn) {
    // over or under voltage
    if (volts > overVolt || volts < underVolt) {
      if (badVoltSince == 0) badVoltSince = millis();
      if (millis() - badVoltSince > VOLT_DELAY) {
        relay(false);
        if (volts > overVolt) reason = "SURGE";
        else reason = "LOW V";
        say("TRIP " + reason + " V=" + String(volts, 1));
        badVoltSince = 0;
        highAmpSince = 0;
        goodVoltSince = 0;
        beep(3);
        return;   // relay is off now, nothing more to check
      }
    } else {
      badVoltSince = 0;
    }

    // over current
    if (amps > overAmp) {
      if (highAmpSince == 0) highAmpSince = millis();
      if (millis() - highAmpSince > AMP_DELAY) {
        relay(false);
        lockedOut = true;
        reason = "OVERLOAD";
        say("TRIP OVER CURRENT I=" + String(amps, 2));
        highAmpSince = 0;
        badVoltSince = 0;
        beep(3);
      }
    } else {
      highAmpSince = 0;
    }
  }
  else if (!lockedOut) {
    // relay is off, switch on again when voltage is good for some seconds
    // 5V margin so it does not click on and off near the limit
    if (volts > underVolt + 5 && volts < overVolt - 5) {
      if (goodVoltSince == 0) goodVoltSince = millis();
      if (millis() - goodVoltSince > reconnectTime * 1000UL) {
        relay(true);
        reason = "";
        goodVoltSince = 0;
        say("RELAY ON V=" + String(volts, 1));
        beep(1);
      }
    } else {
      goodVoltSince = 0;
    }
  }
}

void resetLockout() {
  if (lockedOut) {
    lockedOut = false;
    reason = "WAIT";
    say("RESET OK");
  } else {
    say("nothing to reset");
  }
}


// ---------- LCD ----------

// true if the LCD answers on I2C
bool lcdAnswers() {
  Wire.beginTransmission(LCD_ADDR);
  return Wire.endTransmission() == 0;
}

void show(String a, String b) {
  // relay and motor noise can upset the I2C wires
  // if the LCD does not answer, skip it so protection keeps running
  if (!lcdAnswers()) {
    lcdOk = false;
    return;
  }
  if (!lcdOk) {
    // LCD is back, start it again to clear any garbage
    lcd.init();
    lcd.backlight();
    line1 = "";
    line2 = "";
    lcdOk = true;
  }

  while (a.length() < 16) a += " ";
  while (b.length() < 16) b += " ";
  // only write when text changed, writing is slow
  if (a != line1) {
    lcd.setCursor(0, 0);
    lcd.print(a.substring(0, 16));
    line1 = a;
  }
  if (b != line2) {
    lcd.setCursor(0, 1);
    lcd.print(b.substring(0, 16));
    line2 = b;
  }
}

void updateLcd() {
  if (millis() - lastLcd < 300) return;
  lastLcd = millis();

  if (menuPage == 1) show("Over voltage", String(overVolt) + " V");
  else if (menuPage == 2) show("Under voltage", String(underVolt) + " V");
  else if (menuPage == 3) show("Over current", String(overAmp, 2) + " A");
  else if (menuPage == 4) show("Reconnect after", String(reconnectTime) + " s");
  else if (relayOn) show(String((int)volts) + "V " + String(amps, 2) + "A ON", "OV" + String(overVolt) + " UV" + String(underVolt));
  else if (lockedOut) show("OVERLOAD  OFF", "Hold MENU=reset");
  else show(reason + " " + String((int)volts) + "V OFF", "Wait...");
}


// ---------- buttons ----------
// MENU = next setting, after the last one it saves
// UP / DOWN = change the setting
// hold MENU 2s on normal screen = reset after over current

void checkButtons() {
  bool menu = digitalRead(MENU_BTN) == LOW;
  bool up = digitalRead(UP_BTN) == LOW;
  bool down = digitalRead(DOWN_BTN) == LOW;

  // MENU: check how long it is held, do the action when released
  if (menu) {
    if (menuHeldSince == 0) menuHeldSince = millis();
    // held 2s on normal screen = reset
    if (menuPage == 0 && !menuLongDone && millis() - menuHeldSince > 2000) {
      resetLockout();
      menuLongDone = true;
    }
  }
  else if (menuHeldSince != 0) {
    unsigned long heldFor = millis() - menuHeldSince;
    menuHeldSince = 0;
    if (!menuLongDone && heldFor > 50) {   // short press, 50ms ignores contact bounce
      menuPage++;
      if (menuPage > 4) {
        menuPage = 0;
        saveSettings();
        say("Settings saved");
      }
      lastButton = millis();
    }
    menuLongDone = false;
  }

  // UP / DOWN in menu, one step every 250ms while held
  if ((up || down) && menuPage > 0 && millis() - lastButton > 250) {
    int step = 1;
    if (down) step = -1;
    if (menuPage == 1) overVolt += step;
    if (menuPage == 2) underVolt += step;
    if (menuPage == 3) overAmp += step * 0.05;
    if (menuPage == 4) reconnectTime += step;
    fixSettings();
    lastButton = millis();
  }

  // no button for 15s, leave menu
  if (menuPage > 0 && millis() - lastButton > 15000) {
    menuPage = 0;
    saveSettings();
  }
}


// ---------- commands from PC ----------
// GET          shows the values
// SET OV 250   change a setting (OV, UV, OC, RECON)
// RESET        reset after over current
// HELP

void runCommand(String cmd) {
  cmd.trim();
  cmd.toUpperCase();
  if (cmd == "") return;

  if (cmd == "HELP") {
    say("GET | SET OV/UV/OC/RECON <value> | RESET");
  }
  else if (cmd == "GET") {
    String state = "OFF";
    if (relayOn) state = "ON";
    if (lockedOut) state = "LOCKOUT";
    say("V=" + String(volts, 1) + " I=" + String(amps, 2) + " RELAY=" + state +
        " OV=" + String(overVolt) + " UV=" + String(underVolt) +
        " OC=" + String(overAmp, 2) + " RECON=" + String(reconnectTime));
  }
  else if (cmd == "RESET") {
    resetLockout();
  }
  else if (cmd.startsWith("SET ")) {
    String name = cmd.substring(4, cmd.lastIndexOf(' '));
    String value = cmd.substring(cmd.lastIndexOf(' ') + 1);
    float x = value.toFloat();
    if (x == 0 && value != "0") {   // toFloat gives 0 when it is not a number
      say("ERR not a number");
      return;
    }
    if (name == "OV" && x >= 200 && x <= 300 && x - 20 >= underVolt) overVolt = x;
    else if (name == "UV" && x >= 150 && x <= 220 && x + 20 <= overVolt) underVolt = x;
    else if (name == "OC" && x >= 0.1 && x <= 0.9) overAmp = x;
    else if (name == "RECON" && x >= 3 && x <= 300) reconnectTime = x;
    else {
      say("ERR wrong name or value");
      return;
    }
    saveSettings();
    say("OK");
  }
  else {
    say("ERR unknown command, type HELP");
  }
}

void checkSerial() {
  if (Serial.available()) runCommand(Serial.readStringUntil('\n'));
  if (Serial2.available()) runCommand(Serial2.readStringUntil('\n'));
}


// ---------- setup and loop ----------

void setup() {
  // relay off first so it does not click at start
  digitalWrite(RELAY, HIGH);
  pinMode(RELAY, OUTPUT_OPEN_DRAIN);   // my 5V relay module only turns off when the pin floats

  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(MENU_BTN, INPUT_PULLUP);
  pinMode(UP_BTN, INPUT_PULLUP);
  pinMode(DOWN_BTN, INPUT_PULLUP);
  ledcSetup(0, 2700, 8);    // buzzer 2700Hz
  ledcAttachPin(BUZZER, 0);

  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, RS485_RX, RS485_TX);
  gpio_pullup_en(GPIO_NUM_16);   // stops noise when RS485 module is unplugged
  Serial.setTimeout(10);
  Serial2.setTimeout(10);

  lcd.init();
  lcd.backlight();
  Wire.setTimeOut(5);   // if I2C gets stuck, give up after 5ms
  show("Voltage Protect", "Starting...");

  memory.begin("protector", false);
  loadSettings();
  fixSettings();

  // lamp test, both LEDs and one beep
  digitalWrite(RED_LED, HIGH);
  digitalWrite(GREEN_LED, HIGH);
  beep(1);
  delay(500);
  digitalWrite(RED_LED, LOW);
  digitalWrite(GREEN_LED, LOW);

  // watchdog, if program hangs for 2s the esp32 restarts and relay goes off
  esp_task_wdt_init(2, true);
  esp_task_wdt_add(NULL);

  // reset reason: 1 power on, 3 software, 4 crash, 5 6 7 watchdog, 9 brown out (USB voltage dropped)
  say("Voltage Protector ready, reset reason " + String(esp_reset_reason()) + ", type HELP");
}

void loop() {
  measure();
  checkProtection();   // protection first
  checkSerial();
  checkButtons();
  updateLcd();

  // red LED: on after a trip, blinking when locked out
  if (lockedOut) digitalWrite(RED_LED, (millis() / 300) % 2);
  else if (!relayOn && reason != "START") digitalWrite(RED_LED, HIGH);
  else digitalWrite(RED_LED, LOW);

  esp_task_wdt_reset();
}
