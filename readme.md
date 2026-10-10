## Hardware & Circuit Notes

This build acts as an active protection test bench for simulated mains loads. Instead of running live AC, it uses an isolated DC rail stepped up to a safe 20.0 V "grid" reference.

### Parts List

* **ESP32 DevKitC (38-pin):** Main controller, running off micro-USB.
* **Simulated "Grid" Supply:** 12 V / 2 A wall adapter fed through a 2 A fuse into an XL6009 boost module calibrated to 20.0 V.
* **Load / Appliance:** 24 V DC motor wired across the relay’s Normally Open (NO) contacts. To suppress inductive kickback, clamp a 1N5819 diode and a 100 nF ceramic capacitor in parallel directly across the motor terminals. Physically grabbing the motor shaft creates mechanical resistance to simulate a direct current overload.
* **Current Shunt:** 10 Ω (5–10 W rated ceramic or wirewound resistor) in series on the return path.
* **Sensing Dividers & RC Filters:**
  * Voltage channel: 200 kΩ + 10 kΩ with a 100 nF smoothing cap to ground.
  * Current channel: 22 kΩ + 10 kΩ across the shunt with a 100 nF smoothing cap.
* **Actuation & UI:**
  * 1-channel 5 V relay board (active-LOW trigger).
  * 16×2 character LCD with a standard I2C backpack (0x27).
  * 3 tactile push buttons (internal pull-ups enabled, switching to ground).
  * Red and green status LEDs (each with a 330 Ω current-limiting resistor).
  * 5 V passive or active buzzer.
* **Serial Interface:** HW-519 TTL-to-RS485 transceiver paired with an external USB-RS485 dongle for serial terminal management.

---

### Pin Mapping

| ESP32 Pin | Connection / Node | Signal Details |
|---|---|---|
| **GPIO34** | Grid Voltage Sense | Input from divider (200 kΩ / 10 kΩ). Steps 20 V → ~0.95 V. Filtered by 100 nF to GND. |
| **GPIO35** | Current Sense | Tapped off shunt via 22 kΩ / 10 kΩ divider. Converts 0.1 A → ~0.31 V. Filtered by 100 nF. |
| **GPIO23** | Relay Signal | Connected to module `IN` pin (**Active LOW**: driven low to energize coil). |
| **GPIO25** | Menu Button | Momentary switch to GND. Cycles setting parameters. |
| **GPIO26** | Up Button | Momentary switch to GND. Value increment. |
| **GPIO27** | Down Button | Momentary switch to GND. Value decrement. |
| **GPIO32** | Red LED | Series 330 Ω to anode. Fault / trip indicator. |
| **GPIO33** | Green LED | Series 330 Ω to anode. Relay closed / normal run. |
| **GPIO13** | Buzzer | Drives audio alerts (fault chirps and reconnect tones). |
| **GPIO21** | I2C SDA | Data line for the 16×2 LCD backpack. |
| **GPIO22** | I2C SCL | Clock line for the 16×2 LCD backpack. |
| **GPIO17** | RS485 RXD | UART2 RX, wired to `RO` on the HW-519. |
| **GPIO16** | RS485 TXD | UART2 TX, wired to `DI` on the HW-519. |
| **5V Rail** | Peripheral Power | Powers the relay coil and LCD logic/backlight. |
| **3V3 Rail** | Transceiver Power | Powers the HW-519 logic rail. |
| **GND** | System Ground | Tied together at a star ground point near the shunt resistor return. |


