# Remote Car Starter (Arduino, C++)

An Arduino-based remote engine starter controlled over Serial. It reads the
car battery voltage with an INA219 sensor (I2C) to detect when the engine has
started, and controls relays for ignition, starter, door locks and a signal.

Tested on a real vehicle.

## How it works

1. Send `2` over Serial to start the engine.
2. The program checks that the handbrake is engaged and that the engine is not already running (bus voltage > 13 V).
3. It turns on ignition and starter, then measures voltage in a loop.
4. The starter is released when voltage reaches the configurable threshold, when a stop command (`1`) arrives, or after a 5 second timeout.

## Serial commands

| Command | Action |
|---|---|
| 1 | Stop engine (ignition off) |
| 2 | Start engine |
| 3 | Open doors |
| 4 | Close doors |
| 5 | Ignition on |
| 6 | Signal pulse |
| 8 | Set start voltage threshold (then send the value, e.g. `10.5`) |
| 9 | Print status |

## Hardware

- Arduino (Uno/Nano)
- INA219 current/voltage sensor (SCL -> A5, SDA -> A4)
- Relay module (active low) for ignition, starter, door open, door close, signal
- Handbrake switch on pin 6 (INPUT_PULLUP)

## Safety features

- Handbrake check before starting
- Handbrake check before stopping: the engine is only turned off when the handbrake is engaged, so the car cannot roll on the next start
- Starter timeout (never cranks longer than 5 s)
- Starter relay is always released when the start loop ends
- Threshold input is validated

## Libraries

- Wire
- Adafruit INA219
