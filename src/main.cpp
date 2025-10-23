// ESP32-C3 TWAI Master (NORMAL) — TX heartbeat + RX + auto-recovery
// Transceiver: SN65HVD230 (6-pin), wiring: TX=GPIO4->CTX(DIN), RX=GPIO5<-CRX(RO)
// Single-node bench: 120Ω total + bias (10k H->3V3, 10k L->GND). Two-node: 2×120Ω at ends, no extra bias.

#include <Arduino.h>
#include "driver/twai.h"

// ===== User config =====
static int TWAI_TX_GPIO = 4;   // ESP32-C3 -> SN65 CTX (DIN)
static int TWAI_RX_GPIO = 5;   // SN65 CRX (RO) -> ESP32-C3
// Choose your bitrate (match both nodes!)
static twai_timing_config_t tcfg = TWAI_TIMING_CONFIG_250KBITS();  // or _500KBITS()

// Accept all frames
static twai_filter_config_t fcfg = TWAI_FILTER_CONFIG_ACCEPT_ALL();

// Alerts we care about
static const uint32_t ALERTS =
  TWAI_ALERT_TX_SUCCESS |
  TWAI_ALERT_TX_FAILED |
  TWAI_ALERT_BUS_OFF |
  TWAI_ALERT_BUS_RECOVERED |
  TWAI_ALERT_ERR_ACTIVE |
  TWAI_ALERT_ERR_PASS |
  TWAI_ALERT_BUS_ERROR |
  TWAI_ALERT_RX_DATA |
  TWAI_ALERT_RX_QUEUE_FULL |
  TWAI_ALERT_RX_FIFO_OVERRUN |
  TWAI_ALERT_ARB_LOST;

const uint32_t HEARTBEAT_ID = 0x123;
const uint32_t HEARTBEAT_MS = 200;  // TX period
uint32_t lastHb = 0;
uint32_t txCount = 0, ackCount = 0, txFailCount = 0, busErrCount = 0, busOffCount = 0;

// ===== Helpers =====
const char* stateToStr(twai_state_t s){
  switch(s){
    case TWAI_STATE_STOPPED: return "STOPPED";
    case TWAI_STATE_RUNNING: return "RUNNING";
    case TWAI_STATE_BUS_OFF: return "BUS_OFF";
    default: return "UNKNOWN";
  }
}

void printStatus(const char* tag){
  twai_status_info_t st{};
  if (twai_get_status_info(&st) == ESP_OK){
    Serial.printf("[STATUS] %s: state=%s tx_fail=%lu bus_err=%lu tx_err=%lu rx_err=%lu to_tx=%lu to_rx=%lu\n",
      tag, stateToStr(st.state),
      (unsigned long)st.tx_failed_count,
      (unsigned long)st.bus_error_count,
      (unsigned long)st.tx_error_counter,
      (unsigned long)st.rx_error_counter,
      (unsigned long)st.msgs_to_tx,
      (unsigned long)st.msgs_to_rx);
  }
}

void dumpAlerts(uint32_t a){
  if (!a) return;
  Serial.print("[ALERT] ");
  if (a & TWAI_ALERT_TX_SUCCESS)     { Serial.print("TX_SUCCESS "); ackCount++; }
  if (a & TWAI_ALERT_TX_FAILED)      { Serial.print("TX_FAILED ");  txFailCount++; }
  if (a & TWAI_ALERT_BUS_OFF)        { Serial.print("BUS_OFF ");    busOffCount++; }
  if (a & TWAI_ALERT_BUS_RECOVERED)  { Serial.print("BUS_RECOVERED "); }
  if (a & TWAI_ALERT_ERR_ACTIVE)     { Serial.print("ERR_ACTIVE "); }
  if (a & TWAI_ALERT_ERR_PASS)       { Serial.print("ERR_PASS "); }
  if (a & TWAI_ALERT_ARB_LOST)       { Serial.print("ARB_LOST "); }
  if (a & TWAI_ALERT_RX_DATA)        { Serial.print("RX_DATA "); }
  if (a & TWAI_ALERT_RX_QUEUE_FULL)  { Serial.print("RX_Q_FULL "); }
  if (a & TWAI_ALERT_RX_FIFO_OVERRUN){ Serial.print("RX_FIFO_OVR "); }
  if (a & TWAI_ALERT_BUS_ERROR)      { Serial.print("BUS_ERROR ");  busErrCount++; }
  Serial.println();
}

void recoverIfBusOff(){
  twai_status_info_t st{};
  if (twai_get_status_info(&st) == ESP_OK && st.state == TWAI_STATE_BUS_OFF){
    Serial.println("[RECOVERY] BUS_OFF -> initiating recovery");
    twai_initiate_recovery();
    uint32_t deadline = millis() + 1200;
    while (millis() < deadline){
      uint32_t a=0;
      if (twai_read_alerts(&a, pdMS_TO_TICKS(50)) == ESP_OK && a){
        dumpAlerts(a);
        if (a & TWAI_ALERT_BUS_RECOVERED){
          Serial.println("[RECOVERY] Recovered");
          break;
        }
      }
      yield();
    }
  }
}

bool twaiStartNormal(){
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)TWAI_TX_GPIO,
                                                        (gpio_num_t)TWAI_RX_GPIO,
                                                        TWAI_MODE_NORMAL);
  g.tx_queue_len = 16;
  g.rx_queue_len = 32;
  g.alerts_enabled = ALERTS;
  g.clkout_divider = 0;

  Serial.printf("[TWAI] Install TX=%d RX=%d, NORMAL, bitrate=%s\n",
                TWAI_TX_GPIO, TWAI_RX_GPIO,
                (tcfg.brp==32?"250k":"500k/other"));
  if (twai_driver_install(&g, &tcfg, &fcfg) != ESP_OK){ Serial.println("[TWAI] install FAIL"); return false; }
  if (twai_start() != ESP_OK){ Serial.println("[TWAI] start FAIL"); twai_driver_uninstall(); return false; }
  Serial.println("[TWAI] started");
  recoverIfBusOff();
  printStatus("start");
  return true;
}

bool txHeartbeat(){
  twai_message_t m{};
  m.identifier = HEARTBEAT_ID;
  m.data_length_code = 8;
  m.data[0]='H'; m.data[1]='B'; m.data[2]=(uint8_t)(txCount>>24);
  m.data[3]=(uint8_t)(txCount>>16); m.data[4]=(uint8_t)(txCount>>8); m.data[5]=(uint8_t)(txCount);
  m.data[6]=0x25; m.data[7]=0x0A; // 0x250A => “250 kbps” tag (just a hint)
  esp_err_t e = twai_transmit(&m, pdMS_TO_TICKS(200));
  if (e == ESP_OK){
    txCount++;
    // Note: ACK status is seen via TX_SUCCESS alert
    return true;
  } else if (e == ESP_ERR_TIMEOUT){
    Serial.println("[TX] queue timeout (likely no ACK or bus busy)");
  } else {
    Serial.printf("[TX] error=%d\n", (int)e);
  }
  return false;
}

void rxDrain(uint32_t maxLoops=8){
  twai_message_t m{};
  for (uint32_t i=0; i<maxLoops; ++i){
    if (twai_receive(&m, 0) == ESP_OK){
      Serial.printf("[RX] id=0x%X %s %s dlc=%u  ",
        m.identifier, m.extd?"(EXT)":"(STD)", m.rtr?"(RTR)":"     ", m.data_length_code);
      for (int k=0;k<m.data_length_code;k++) Serial.printf("%02X ", m.data[k]);
      Serial.println();
    } else {
      break;
    }
  }
}

void printHealthEvery(uint32_t sec=5){
  static uint32_t next=0;
  if (millis() < next) return;
  next = millis() + sec*1000UL;
  Serial.printf("[HEALTH] tx=%lu ack=%lu txFail=%lu busErr=%lu busOff=%lu\n",
    (unsigned long)txCount, (unsigned long)ackCount,
    (unsigned long)txFailCount, (unsigned long)busErrCount,
    (unsigned long)busOffCount);
  printStatus("periodic");
}

void setup(){
  Serial.begin(115200);
  delay(200);
  Serial.println("\nTWAI MASTER (NORMAL): HB TX + RX + auto-recovery");
  Serial.println("Wiring: TX=GPIO4->CTX, RX=GPIO5<-CRX, SN65HVD230, 250 kbps default");
  // Start TWAI in NORMAL mode
  if (!twaiStartNormal()){
    Serial.println("FATAL: TWAI start failed");
    while(true){ delay(1000); }
  }
}

void loop(){
  // 1) Send heartbeat periodically (requires ACK to complete quickly on a shared bus)
  if (millis() - lastHb >= HEARTBEAT_MS){
    lastHb = millis();
    txHeartbeat();
  }

  // 2) Handle alerts (ACKs, BUS_OFF, errors, RX notifications)
  uint32_t a=0;
  if (twai_read_alerts(&a, pdMS_TO_TICKS(10)) == ESP_OK && a){
    dumpAlerts(a);
    if (a & TWAI_ALERT_BUS_OFF){
      recoverIfBusOff();
    }
  }

  // 3) Drain received frames (e.g., echo node replies)
  rxDrain();

  // 4) Print health summary periodically
  printHealthEvery(5);

  yield();
}
