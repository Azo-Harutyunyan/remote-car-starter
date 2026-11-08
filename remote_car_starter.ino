/*
 * Remote Car Starter
 * ------------------
 * Arduino-based remote engine starter controlled over Serial.
 * An INA219 sensor (I2C) measures battery voltage; the starter is cranked
 * until the voltage reaches a configurable threshold (engine running),
 * or until a timeout / stop command occurs.
 *
 * Wiring: INA219 VCC->VCC, GND->GND, SCL->A5, SDA->A4
 * Relays are ACTIVE LOW (LOW = relay on, HIGH = relay off).
 *
 * Serial commands (send as a number):
 *   1 - stop engine (ignition off; refused unless the handbrake is engaged)
 *   2 - start engine
 *   3 - open doors
 *   4 - close doors
 *   5 - ignition on
 *   6 - signal (horn / light pulse)
 *   8 - set start voltage threshold (then send the value, e.g. 10.5)
 *   9 - print status
 */

#include <Wire.h>
#include <Adafruit_INA219.h>

// ---------- Pins ----------
const int IGNITION_RELAY_PIN   = 2;
const int STARTER_RELAY_PIN    = 3;
const int DOOR_OPEN_RELAY_PIN  = 4;
const int DOOR_CLOSE_RELAY_PIN = 5;
const int HANDBRAKE_PIN        = 6;  // INPUT_PULLUP: LOW = handbrake released
const int SIGNAL_RELAY_PIN     = 7;

// ---------- Relay logic (active low) ----------
const int RELAY_ON  = LOW;
const int RELAY_OFF = HIGH;

// ---------- Voltage settings (volts) ----------
const float ENGINE_RUNNING_VOLTAGE = 13.0;  // above this the alternator is charging
const float MIN_THRESHOLD_VOLTAGE  = 9.5;
const float DEFAULT_START_THRESHOLD = 10.35;

// ---------- Timing (milliseconds) ----------
const unsigned long STARTER_SETTLE_MS     = 400;
const unsigned long STARTER_TIMEOUT_MS    = 5000;  // never crank longer than this
const unsigned long DOOR_OPEN_PULSE_MS    = 180;
const unsigned long DOOR_CLOSE_PULSE_MS   = 75;
const unsigned long SIGNAL_PULSE_MS       = 75;
const unsigned long HANDBRAKE_POLL_MS     = 500;

// ---------- Commands ----------
enum Command {
  CMD_STOP_ENGINE   = 1,
  CMD_START_ENGINE  = 2,
  CMD_OPEN_DOORS    = 3,
  CMD_CLOSE_DOORS   = 4,
  CMD_IGNITION_ON   = 5,
  CMD_SIGNAL        = 6,
  CMD_SET_THRESHOLD = 8,
  CMD_STATUS        = 9
};

Adafruit_INA219 ina219;
float startVoltageThreshold = DEFAULT_START_THRESHOLD;
bool doorsClosed = false;

// ---------- Helpers ----------
bool isHandbrakeEngaged() {
  return digitalRead(HANDBRAKE_PIN) == HIGH;
}

void pulseRelay(int pin, unsigned long durationMs) {
  digitalWrite(pin, RELAY_ON);
  delay(durationMs);
  digitalWrite(pin, RELAY_OFF);
}

void allRelaysOff() {
  digitalWrite(IGNITION_RELAY_PIN, RELAY_OFF);
  digitalWrite(STARTER_RELAY_PIN, RELAY_OFF);
  digitalWrite(DOOR_OPEN_RELAY_PIN, RELAY_OFF);
  digitalWrite(DOOR_CLOSE_RELAY_PIN, RELAY_OFF);
  digitalWrite(SIGNAL_RELAY_PIN, RELAY_OFF);
}

void waitUntilHandbrakeEngaged() {
  while (!isHandbrakeEngaged()) {
    Serial.println("Please engage the handbrake.");
    delay(HANDBRAKE_POLL_MS);
  }
}

// Returns true if the stop command (1) was received on Serial.
bool stopRequested() {
  if (Serial.available() > 0) {
    return Serial.parseInt() == CMD_STOP_ENGINE;
  }
  return false;
}

// ---------- Actions ----------
void stopEngine() {
  digitalWrite(STARTER_RELAY_PIN, RELAY_OFF);  // always make sure the starter is released

  // Safety: the driver must engage the handbrake before the engine is turned
  // off, so the car cannot roll when it is started again later.
  if (!isHandbrakeEngaged()) {
    Serial.println("Handbrake error: engage the handbrake, then send stop again. Engine left running.");
    return;
  }

  digitalWrite(IGNITION_RELAY_PIN, RELAY_OFF);
  Serial.println("Engine stopped (ignition off).");
}

void startEngine() {
  Serial.println("Starting...");

  if (!isHandbrakeEngaged()) {
    Serial.println("Handbrake error: engage the handbrake and try again.");
    return;
  }

  float voltage = ina219.getBusVoltage_V();
  if (voltage > ENGINE_RUNNING_VOLTAGE) {
    Serial.println("Engine is already running.");
    return;
  }

  digitalWrite(IGNITION_RELAY_PIN, RELAY_ON);
  digitalWrite(STARTER_RELAY_PIN, RELAY_ON);
  delay(STARTER_SETTLE_MS);  // let the voltage drop from cranking settle

  unsigned long startedAt = millis();
  bool cancelled = false;
  bool timedOut = false;

  while (true) {
    voltage = ina219.getBusVoltage_V();
    Serial.print("Bus voltage: ");
    Serial.print(voltage);
    Serial.println(" V");

    if (voltage >= startVoltageThreshold) break;  // engine started

    if (stopRequested()) {
      cancelled = true;
      break;
    }
    if (millis() - startedAt > STARTER_TIMEOUT_MS) {
      timedOut = true;
      break;
    }
  }

  digitalWrite(STARTER_RELAY_PIN, RELAY_OFF);  // always release the starter

  if (cancelled) {
    stopEngine();
    Serial.println("Start cancelled.");
  } else if (timedOut) {
    digitalWrite(IGNITION_RELAY_PIN, RELAY_OFF);
    Serial.println("Start failed: timeout, voltage did not reach threshold.");
  } else {
    Serial.println("Engine started.");
  }
}

void openDoors() {
  pulseRelay(DOOR_OPEN_RELAY_PIN, DOOR_OPEN_PULSE_MS);
  doorsClosed = false;
  Serial.println("Doors opened.");
}

void closeDoors() {
  pulseRelay(DOOR_CLOSE_RELAY_PIN, DOOR_CLOSE_PULSE_MS);
  doorsClosed = true;
  Serial.println("Doors closed.");
}

void setStartThreshold() {
  Serial.println("Enter start voltage threshold (e.g. 10.5):");
  Serial.setTimeout(10000);  // give the user 10 s to type the value
  float value = Serial.parseFloat();
  Serial.setTimeout(1000);

  if (value >= MIN_THRESHOLD_VOLTAGE && value < ENGINE_RUNNING_VOLTAGE) {
    startVoltageThreshold = value;
    Serial.print("Threshold set to ");
    Serial.print(startVoltageThreshold);
    Serial.println(" V");
  } else {
    Serial.print("Invalid value. Allowed range: ");
    Serial.print(MIN_THRESHOLD_VOLTAGE);
    Serial.print(" - ");
    Serial.print(ENGINE_RUNNING_VOLTAGE);
    Serial.println(" V");
  }
}

void printStatus() {
  Serial.print("Ignition relay: ");
  Serial.println(digitalRead(IGNITION_RELAY_PIN) == RELAY_ON ? "ON" : "OFF");
  Serial.print("Doors closed: ");
  Serial.println(doorsClosed ? "yes" : "no");
  Serial.print("Start threshold: ");
  Serial.print(startVoltageThreshold);
  Serial.println(" V");
}

// ---------- Arduino entry points ----------
void setup() {
  Serial.begin(9600);
  Serial.println("Measuring voltage and current with INA219...");

  if (!ina219.begin()) {
    Serial.println("INA219 not found. Check wiring.");
  }

  pinMode(HANDBRAKE_PIN, INPUT_PULLUP);
  pinMode(IGNITION_RELAY_PIN, OUTPUT);
  pinMode(STARTER_RELAY_PIN, OUTPUT);
  pinMode(DOOR_OPEN_RELAY_PIN, OUTPUT);
  pinMode(DOOR_CLOSE_RELAY_PIN, OUTPUT);
  pinMode(SIGNAL_RELAY_PIN, OUTPUT);

  allRelaysOff();
  waitUntilHandbrakeEngaged();

  Serial.println("Ready.");
}

void loop() {
  if (Serial.available() <= 0) return;

  int command = Serial.parseInt();
  Serial.println(command);

  switch (command) {
    case CMD_STOP_ENGINE:   stopEngine();                                  break;
    case CMD_START_ENGINE:  startEngine();                                 break;
    case CMD_OPEN_DOORS:    openDoors();                                   break;
    case CMD_CLOSE_DOORS:   closeDoors();                                  break;
    case CMD_IGNITION_ON:   digitalWrite(IGNITION_RELAY_PIN, RELAY_ON);    break;
    case CMD_SIGNAL:        pulseRelay(SIGNAL_RELAY_PIN, SIGNAL_PULSE_MS); break;
    case CMD_SET_THRESHOLD: setStartThreshold();                           break;
    case CMD_STATUS:        printStatus();                                 break;
    default: break;  // ignore unknown input (including stray newlines -> 0)
  }
}
