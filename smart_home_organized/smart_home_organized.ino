#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <Keypad.h>

//House modes

enum HouseMode {
  HOME_MODE,
  AWAY_MODE,
  SLEEP_MODE
};

HouseMode currentMode = AWAY_MODE;

// ------ PINS ------
//Keypad 
const byte ROWS = 4;
const byte COLS = 3;

byte rowPins[ROWS] = {2, 3, 4, 5};
byte colPins[COLS] = {6, 7, 8};

//Door
const int servoPin = 9;

//Security
const int windowPin = 0;  
const int pirPin = 1;     
const int buzzerPin = 10;
const int exteriorLedPin = 13;

//Lighting
const int buttonPin = 11;
const int ldrPin = A3;
const int indoorLedPin = 12;

//Climate
const int lm35Pin = A0;
const int potPin = A1;
const int fanPin = A2;

// -------- LCD -------
LiquidCrystal_I2C lcd(0x27, 16, 2);
String lastLine1 = "";
String lastLine2 = "";
unsigned long messageUntil = 0;   //used for keeping messages on screen

void setScreen(String a, String b) {
  if (a.length() > 16) a = a.substring(0, 16);
  if (b.length() > 16) b = b.substring(0, 16);

  while (a.length() < 16) a += ' ';
  while (b.length() < 16) b += ' ';

  if (a != lastLine1) {
    lcd.setCursor(0, 0);
    lcd.print(a);
    lastLine1 = a;
  }
  if (b != lastLine2) {
    lcd.setCursor(0, 1);
    lcd.print(b);
    lastLine2 = b;
  }
}

void showMessage(String a, String b, unsigned long holdMs) {
  lastLine1 = "";
  lastLine2 = "";
  setScreen(a, b);
  messageUntil = millis() + holdMs;
}

// --------- KEYPAD 4x4 -----------
char keys[ROWS][COLS] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};

Keypad keypad = Keypad(makeKeymap(keys),
rowPins, colPins, ROWS, COLS);

const String correctCode = "2007";
String enteredCode = "";
const byte MAX_CODE_LENGTH = 5;

int wrongAttempts = 0;
const int MAX_WRONG_ATTEMPTS = 3;
bool passwordAlarm = false;
bool selectingMode = false;
bool enteringPassword = true;   // true = LCD shows the password prompt

// ------------- DOOR SERVO --------------
Servo doorServo;

bool doorUnlocked = false;
unsigned long unlockTime = 0;
const unsigned long openTime = 5000; //how long door should stay open

// --------- SECURITY -----------
bool windowOpen = false;
bool windowAlarm = false;      // stays true until the correct PIN is entered

// ----------- LIGHTING -------------
const int onThreshold = 600;
const int offThreshold = 800;

// Pushbutton manual control:
//   short press: toggles the indoor light ON/OFF (LDR is ignored)
//   long press: hands control back to the LDR
bool manualControl = false;   
bool manualState = false;      // light state chosen with the button
bool lastButtonState = HIGH;   // previous button reading (HIGH = not pressed (input_pullup is used))
unsigned long pressStart = 0;
const unsigned long LONG_PRESS = 1000; //ms
bool indoorLightState = false; // LDR control

// ------------- CLIMATE ------------
const float TEMP_MIN = 20.0; //potentiometer range
const float TEMP_MAX = 50.0;

float currentTemp = 0;
float currentSetpoint = 30;
float TEMP_OFF = 28;
const float HYST = 2.0;
bool fanOn = false;

unsigned long lastRead = 0;
const unsigned long READ_INTERVAL = 1000;

//----------------Functions-----------------
// Temp
float readTemperature() {
  long total = 0;

  for (int i = 0; i < 10; i++) {
    total += analogRead(lm35Pin);
    delay(5);
  }

  float average = total / 10.0;
  return average * 5.0 / 1023.0 * 100.0;
}

float readSetpoint() {
  int raw = analogRead(potPin);
  float t = TEMP_MIN + (raw / 1023.0) * (TEMP_MAX - TEMP_MIN);

  return round(t * 2) / 2.0;
}

//Mode selection
void applyMode(HouseMode mode) {
  currentMode = mode;
  enteringPassword = false;
  manualControl = false;   // changing mode returns the light to LDR control

  if (mode == HOME_MODE) {
    doorServo.write(90);
    doorUnlocked = true;
    unlockTime = millis();

    showMessage("WELCOME HOME", "Door opens 5 sec", 2000);
  }
  else if (mode == AWAY_MODE) {
    doorServo.write(0);
    doorUnlocked = false;

    showMessage("AWAY MODE ACTIVE", "Security ON", 2000);
  }
  else {
    doorServo.write(0);
    doorUnlocked = false;

    showMessage("SLEEP MODE ON", "Security ON", 2000);
  }
}

// -------------- SETUP ----------------
void setup() {
  pinMode(windowPin, INPUT_PULLUP);
  pinMode(pirPin, INPUT);
  pinMode(buttonPin, INPUT_PULLUP);   

  pinMode(buzzerPin, OUTPUT);
  pinMode(exteriorLedPin, OUTPUT);
  pinMode(indoorLedPin, OUTPUT);
  pinMode(fanPin, OUTPUT);

  digitalWrite(exteriorLedPin, LOW);
  digitalWrite(indoorLedPin, LOW);
  digitalWrite(fanPin, LOW);
  noTone(buzzerPin);

  Wire.begin();
  lcd.init();
  lcd.backlight();

  doorServo.attach(servoPin);
  doorServo.write(0);

  showMessage("Smart Home Demo", "System Starting", 0);
  delay(1500);

  currentTemp = readTemperature();
  currentSetpoint = readSetpoint();
  TEMP_OFF = currentSetpoint - HYST;

  enteringPassword = true;   // loop() now shows "Enter Password:"
}

// ------------- MAIN LOOP -------------
void loop() {
  unsigned long now = millis();
  char key = keypad.getKey();

  // password entry
  if (key && !selectingMode) {
    if (key == '*') {
      enteredCode = "";
      enteringPassword = true;
    }
    else if (key == '#') {
      enteringPassword = true;

      if (enteredCode == correctCode) {
        passwordAlarm = false;
        windowAlarm = false;      // correct PIN silences the window alarm 
        wrongAttempts = 0;
        enteredCode = "";
        selectingMode = true;

      }
      else {
        wrongAttempts++;
        enteredCode = "";

        if (wrongAttempts >= MAX_WRONG_ATTEMPTS) {
          passwordAlarm = true;
        }
        else {
          showMessage("Access Denied", "Try Again", 1500);
        }
      }
    }
    else if (key >= '0' && key <= '9') {
      enteringPassword = true;

      if (enteredCode.length() < MAX_CODE_LENGTH) {
        enteredCode += key;
      }
    }
  }

  //Mode choice
  else if (key && selectingMode) {
    if (key == '1') {
      selectingMode = false;
      applyMode(HOME_MODE);
    }
    else if (key == '2') {
      selectingMode = false;
      applyMode(AWAY_MODE);
    }
    else if (key == '3') {
      selectingMode = false;
      applyMode(SLEEP_MODE);
    }
    else if (key == '*') {
      selectingMode = false;
      enteredCode = "";
      enteringPassword = true;
    }
  }

  // Close door
  if (doorUnlocked && now - unlockTime >= openTime) {
    doorServo.write(0);
    doorUnlocked = false;
  }

  // Security
  bool securityEnabled =
      (currentMode == AWAY_MODE ||
       currentMode == SLEEP_MODE);

  windowOpen = (digitalRead(windowPin) == HIGH); // HIGH means window open 

  if (securityEnabled && windowOpen && !selectingMode) { //window alarm rings when mode selection mode is not open
    windowAlarm = true;
  }
  

  //exterior motion light
  bool motionDetected = (digitalRead(pirPin) == HIGH);

  digitalWrite(exteriorLedPin,(securityEnabled && motionDetected) ? HIGH : LOW);

  // Climate control
  if (now - lastRead >= READ_INTERVAL) {
    lastRead = now;

    currentTemp = readTemperature();
    currentSetpoint = readSetpoint();
    TEMP_OFF = currentSetpoint - HYST;

    if (currentMode == AWAY_MODE) {
      fanOn = false;
    }
    else {
      if (currentTemp >= currentSetpoint) {
        fanOn = true;
      }
      else if (currentTemp <= TEMP_OFF) {
        fanOn = false;
      }
    }

    digitalWrite(fanPin, fanOn ? HIGH : LOW);
  }
  if (selectingMode) {
    fanOn = false;
    digitalWrite(fanPin, LOW);
  }

  // Light pushbutton (short press = toggle, long press = back to LDR) 
  bool buttonState = digitalRead(buttonPin);   // LOW = pressed (INPUT_PULLUP)

  if (lastButtonState == HIGH && buttonState == LOW) {
    // button just pressed
    delay(30);                                  // debounce
    if (digitalRead(buttonPin) == LOW) {
      pressStart = millis();
    }
    else {
      buttonState = HIGH;                       // it was just noise
    }
  }
  else if (lastButtonState == LOW && buttonState == HIGH) {
    // button just released
    delay(30);                                  
    if (digitalRead(buttonPin) == HIGH && currentMode != AWAY_MODE) { //button control only works if person is inside house
      unsigned long held = millis() - pressStart; //calculate time button was pressed for

      if (held >= LONG_PRESS) {
        manualControl = false;                  // long press: LDR is in charge again
      }
      else if (!manualControl) {
        manualState = !indoorLightState;        // first press: flip what the light is showing
        manualControl = true;
      }
      else {
        manualState = !manualState;             // later presses: toggle ON/OFF
      }
    }
    else {
      buttonState = LOW;                        // it was just noise
    }
  }
  lastButtonState = buttonState;

  //LDR Lighting
  int lightReading = analogRead(ldrPin);

  if (currentMode == AWAY_MODE) {
    indoorLightState = false;
    digitalWrite(indoorLedPin, LOW); // Force OFF in Away Mode
  }
  else {

    if (currentMode == SLEEP_MODE) {
      indoorLightState = false;   // Sleep mode: LDR never turns the light on, only the button does
    }
    else if (lightReading < onThreshold) {
      indoorLightState = true;
    }
    else if (lightReading > offThreshold) {
      indoorLightState = false;
    }


    // button decides if manual control is on, otherwise the LDR decides
    bool finalLightState = manualControl ? manualState : indoorLightState;

    digitalWrite(indoorLedPin, finalLightState ? HIGH : LOW);
  }
  if (selectingMode){
    
    digitalWrite(indoorLedPin, LOW);

  }

  //Buzzer
  if (passwordAlarm || windowAlarm) {
    tone(buzzerPin, 1000);
  }
  else {
    noTone(buzzerPin);
  }

  // LCD status
  if (windowAlarm) {
    // show the PIN so the user can silence the alarm
    if (enteredCode.length() > 0) {
      String stars = "";
      for (unsigned int i = 0; i < enteredCode.length(); i++) {
        stars += '*';
      }
      setScreen("SECURITY ALERT!", "PIN: " + stars);
    }
    else {
      setScreen("SECURITY ALERT!", "Enter PIN");
    }
  }
  else if (passwordAlarm) {
  if (enteredCode.length() > 0) {
    String stars = "";
    for (unsigned int i = 0; i < enteredCode.length(); i++) stars += '*';
    setScreen("ACCESS ALARM!", "PIN: " + stars);
  }
  else {
    setScreen("ACCESS ALARM!", "Enter PIN");
  }
}
  else if (now < messageUntil) {
    // a pop-up message is still being shown; leave the screen alone
  }
  else if (selectingMode) {
    setScreen("Select Mode", "1:H 2:Away 3:Slp");
  }
  else if (enteringPassword) {
    String stars = "";
    for (unsigned int i = 0; i < enteredCode.length(); i++) {
      stars += '*';
    }
    setScreen("Enter Password:", stars);
  }
  else if (doorUnlocked) {
    setScreen("Door: OPEN", "Closing soon...");
  }
  else if (currentMode == AWAY_MODE) {
    setScreen("AWAY:Security ON", "Fan OFF Lamp OFF");
  }
  else if (currentMode == SLEEP_MODE) {
    setScreen("SLEEP: Security",
              "T:" + String(currentTemp, 0) + "C Fan:" +
              (fanOn ? "ON" : "OFF"));
  }
  else {
    setScreen("HOME T:" + String(currentTemp, 0) + "C",
              "Set:" + String(currentSetpoint, 1) +
              " Sec:OFF");
  }
}
