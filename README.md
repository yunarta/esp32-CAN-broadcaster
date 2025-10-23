# CAN-Master (ESP32-C3 + TWAI/CAN, Arduino/PlatformIO)

An ESP32-C3 (LOLIN C3 Mini) example acting as a CAN/TWAI "master" in NORMAL mode:
- Periodically transmits a heartbeat frame (every 200 ms)
- Receives and prints any incoming frames
- Monitors and prints TWAI alerts (ACK success, errors, BUS_OFF)
- Attempts automatic recovery from BUS_OFF

This project targets PlatformIO (Arduino framework) and the ESP32-C3 TWAI driver.

## Hardware
- MCU board: LOLIN C3 Mini (ESP32-C3)
- CAN transceiver: SN65HVD230 (6-pin breakout)
- Default wiring (from src/main.cpp):
  - ESP32-C3 GPIO4 (TX)  -> SN65HVD230 CTX/DIN
  - ESP32-C3 GPIO5 (RX)  <- SN65HVD230 CRX/RO
  - 3V3, GND, CANH, CANL as per your transceiver/bus

Bus termination and biasing:
- Two-node setup (real network): 120 Ω at each end of the bus (2×120 Ω total). No extra bias resistors.
- Single-node bench (isolated): total ≈120 Ω and bias (e.g., 10 kΩ from CANH to 3V3, 10 kΩ from CANL to GND) so the bus is recessive when idle.

Important: In TWAI NORMAL mode, successful transmission requires an ACK from another active node on the same bus and bitrate. If you test with only one node and no ACK, you may see TX queue timeouts and possibly BUS_OFF.

## Features
- Heartbeat CAN ID: 0x123
- Heartbeat payload: "HB" + 32-bit counter + tag bytes (0x25, 0x0A)
- Default bitrate: 250 kbit/s
- Serial monitoring at 115200 baud
- Alert-driven counters: TX successes (ACKed), TX failures, bus errors, BUS_OFF count
- Automatic BUS_OFF recovery attempt

## Build and Flash (PlatformIO)
1. Install PlatformIO (VS Code extension) or the PlatformIO Core CLI.
2. Connect the LOLIN C3 Mini via USB.
3. From this project root, using the CLI:
   - Build and upload: `pio run -t upload`
   - Open serial monitor (115200): `pio device monitor -b 115200`

Project configuration: see `platformio.ini` (uses `[env:lolin_c3_mini]`).

## Runtime Output
On reset you should see lines similar to:
```
TWAI MASTER (NORMAL): HB TX + RX + auto-recovery
Wiring: TX=GPIO4->CTX, RX=GPIO5<-CRX, SN65HVD230, 250 kbps default
[TWAI] Install TX=4 RX=5, NORMAL, bitrate=250k
[TWAI] started
[STATUS] start: state=RUNNING ...
[ALERT] TX_SUCCESS ...
[HEALTH] tx=... ack=... txFail=... busErr=... busOff=...
```
If there is no other node acknowledging frames, you may instead see TX timeouts and alerts indicating missing ACK or bus errors.

## Configuration
Edit `src/main.cpp` to adjust:
- Pins: `TWAI_TX_GPIO` (default 4), `TWAI_RX_GPIO` (default 5)
- Bitrate: `tcfg` (default `TWAI_TIMING_CONFIG_250KBITS()`); alternatives include `TWAI_TIMING_CONFIG_500KBITS()`
- Heartbeat period and ID: `HEARTBEAT_MS`, `HEARTBEAT_ID`

Serial monitor speed: 115200 baud (see `platformio.ini`).

## Troubleshooting
- No ACK / TX timeouts: Ensure a second node is online at the same bitrate and wired correctly with proper termination. NORMAL mode requires an ACK from another node.
- BUS_OFF occurs repeatedly: Check CANH/CANL wiring, ground reference, termination resistors, and that both nodes use the same bitrate.
- Wrong bitrate: Both nodes must match. Default here is 250 kbit/s.
- Power: Many SN65HVD230 boards can run at 3.3 V. Ensure the transceiver voltage level matches your board and wiring recommendations.

## License
This project is released under the MIT License. See `LICENSE.md`.
