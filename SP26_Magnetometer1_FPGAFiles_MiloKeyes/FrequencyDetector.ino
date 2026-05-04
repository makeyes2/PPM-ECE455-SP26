// FrequencyDetector.ino
// Arduino interface to FrequencyCounter FPGA module.
// Drives cnt_enable / start_transmit, reads done / sclk / freqOut,
// and computes frequency in Hz from the 32-bit cycle count.

#include <TinyGPSPlus.h>
#include <SD.h>

#define GNSS_SERIAL Serial1

// ────────────────────────────────────────────────────────────────────
//  Pin assignments  (change to match your wiring)
// ────────────────────────────────────────────────────────────────────
const int PIN_CNT_ENABLE     = 2;   // Arduino OUT → FPGA cnt_enable
const int PIN_START_TRANSMIT = 3;   // Arduino OUT → FPGA start_transmit
const int PIN_RST_N          = 4;   // Arduino OUT → FPGA rst_n
const int PIN_DONE           = 5;   // Arduino IN  ← FPGA done
const int PIN_SCLK           = 6;   // Arduino IN  ← FPGA sclk
const int PIN_FREQ_OUT       = 7;   // Arduino IN  ← FPGA freqOut

// ────────────────────────────────────────────────────────────────────
//  FPGA / measurement constants
// ────────────────────────────────────────────────────────────────────
const double   FPGA_CLK_HZ       = 12000000.0;  // 12 MHz FPGA clock
const uint16_t NUM_PERIODS       = 2048;         // rising edges counted
const uint32_t MEASURE_TIMEOUT_MS = 30000;       // 30 s max wait for done
const uint32_t TX_TIMEOUT_MS      = 5000;        // 5 s max for serial readout
const int      TX_BITS            = 32;
const uint32_t GNSS_BAUD_RATE     = 9600;
const char     LOG_FILENAME[]     = "/freq_log.csv";

TinyGPSPlus gps;
bool sdReady = false;

void pollGNSS() {
    while (GNSS_SERIAL.available() > 0) {
        gps.encode(GNSS_SERIAL.read());
    }
}

void writeCsvHeaderIfNeeded() {
    if (SD.exists(LOG_FILENAME)) {
        return;
    }

    File file = SD.open(LOG_FILENAME, FILE_WRITE);
    if (!file) {
        Serial.println("ERROR: unable to create CSV log file");
        return;
    }

    file.println("millis,gnss_utc,status,frequency_hz,latitude,longitude,altitude_m,satellites");
    file.close();
}

void writeCsvRow(double freq) {
    if (!sdReady) {
        return;
    }

    File file = SD.open(LOG_FILENAME, FILE_WRITE);
    if (!file) {
        Serial.println("ERROR: unable to append to CSV log file");
        return;
    }

    file.print(millis());
    file.print(',');

    if (gps.date.isValid() && gps.time.isValid()) {
        char ts[25];
        snprintf(ts, sizeof(ts), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                 gps.date.year(), gps.date.month(), gps.date.day(),
                 gps.time.hour(), gps.time.minute(), gps.time.second());
        file.print(ts);
    }
    file.print(',');

    if (freq < 0.0) {
        file.print("timeout");
    } else {
        file.print("ok");
    }
    file.print(',');

    if (freq < 0.0) {
        file.print("NA");
    } else {
        file.print(freq, 2);
    }
    file.print(',');

    if (gps.location.isValid()) {
        file.print(gps.location.lat(), 6);
    } else {
        file.print("NA");
    }
    file.print(',');

    if (gps.location.isValid()) {
        file.print(gps.location.lng(), 6);
    } else {
        file.print("NA");
    }
    file.print(',');

    if (gps.altitude.isValid()) {
        file.print(gps.altitude.meters(), 1);
    } else {
        file.print("NA");
    }
    file.print(',');

    if (gps.satellites.isValid()) {
        file.print(gps.satellites.value());
    } else {
        file.print("NA");
    }

    file.println();
    file.close();
}

// ────────────────────────────────────────────────────────────────────
//  resetFPGA() – pulse rst_n low for ~10 ms
// ────────────────────────────────────────────────────────────────────
void resetFPGA() {
    digitalWrite(PIN_RST_N, LOW);
    delay(10);
    digitalWrite(PIN_RST_N, HIGH);
    delay(1);
}

// ────────────────────────────────────────────────────────────────────
//  measure() – run one complete measurement transaction
//
//  Returns the measured frequency in Hz, or -1.0 on error/timeout.
//
//  Protocol:
//    1.  Assert cnt_enable          → FPGA starts counting.
//    2.  Wait for done == HIGH      → 2048 signal periods captured.
//    3.  De-assert cnt_enable.
//    4.  Pulse start_transmit       → FPGA shifts out 32 bits MSB-first.
//    5.  Read each bit on the rising edge of sclk.
//    6.  Compute:  freq = FPGA_CLK_HZ * NUM_PERIODS / count
// ────────────────────────────────────────────────────────────────────
double measure() {

    // ── 1. Start measurement ────────────────────────────────────────
    digitalWrite(PIN_CNT_ENABLE, HIGH);

    // ── 2. Wait for done ────────────────────────────────────────────
    unsigned long t0 = millis();
    while (digitalRead(PIN_DONE) == LOW) {
        pollGNSS();
        if (millis() - t0 > MEASURE_TIMEOUT_MS) {
            // Timed out waiting for measurement
            digitalWrite(PIN_CNT_ENABLE, LOW);
            return -1.0;
        }
    }

    // ── 3. De-assert cnt_enable ─────────────────────────────────────
    digitalWrite(PIN_CNT_ENABLE, LOW);

    // ── 4. Pulse start_transmit (one clock-visible pulse) ───────────
    digitalWrite(PIN_START_TRANSMIT, HIGH);
    delayMicroseconds(10);  // ensure FPGA sees it for ≥1 clk edge
    digitalWrite(PIN_START_TRANSMIT, LOW);

    // ── 5. Receive 32 bits MSB-first on rising sclk edges ──────────
    uint32_t value   = 0;
    int      prevClk = LOW;
    int      bitsRead = 0;

    t0 = millis();
    while (bitsRead < TX_BITS) {
        pollGNSS();
        if (millis() - t0 > TX_TIMEOUT_MS) {
            // Timed out during serial readout
            return -1.0;
        }

        int curClk = digitalRead(PIN_SCLK);

        // Detect rising edge of sclk
        if (curClk == HIGH && prevClk == LOW) {
            int bit = digitalRead(PIN_FREQ_OUT);
            // Bits arrive MSB (bit 31) first, down to LSB (bit 0)
            value |= ((uint32_t)bit << (TX_BITS - 1 - bitsRead));
            bitsRead++;
        }
        prevClk = curClk;
    }

    // ── 6. Compute frequency ────────────────────────────────────────
    if (value == 0) {
        return 0.0;  // avoid division by zero
    }

    double frequency = (FPGA_CLK_HZ * (double)NUM_PERIODS) / (double)value;
    return frequency;
}

// ────────────────────────────────────────────────────────────────────
//  setup()
// ────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    GNSS_SERIAL.begin(GNSS_BAUD_RATE);

    sdReady = SD.begin(BUILTIN_SDCARD);
    if (sdReady) {
        writeCsvHeaderIfNeeded();
    }

    pinMode(PIN_CNT_ENABLE,     OUTPUT);
    pinMode(PIN_START_TRANSMIT, OUTPUT);
    pinMode(PIN_RST_N,          OUTPUT);

    pinMode(PIN_DONE,     INPUT);
    pinMode(PIN_SCLK,     INPUT);
    pinMode(PIN_FREQ_OUT, INPUT);

    // Initialise outputs low (cnt_enable & start_transmit inactive)
    digitalWrite(PIN_CNT_ENABLE,     LOW);
    digitalWrite(PIN_START_TRANSMIT, LOW);

    // Reset the FPGA FSM
    resetFPGA();

    Serial.println("FrequencyDetector ready.");
    Serial.println("GNSS logging enabled.");
    if (sdReady) {
        Serial.print("CSV logging to SD file: ");
        Serial.println(LOG_FILENAME);
    } else {
        Serial.println("WARNING: SD init failed, CSV logging disabled.");
    }
}

// ────────────────────────────────────────────────────────────────────
//  loop()
// ────────────────────────────────────────────────────────────────────
void loop() {
    pollGNSS();

    double freq = measure();
    writeCsvRow(freq);

    if (freq < 0.0) {
        Serial.print("ERROR: measurement timed out");
    } else {
        Serial.print("Frequency_Hz=");
        Serial.print(freq, 2);
    }

    if (gps.location.isValid()) {
        Serial.print(", Lat=");
        Serial.print(gps.location.lat(), 6);
        Serial.print(", Lon=");
        Serial.print(gps.location.lng(), 6);
    } else {
        Serial.print(", Lat=NA, Lon=NA");
    }

    if (gps.altitude.isValid()) {
        Serial.print(", Alt_m=");
        Serial.print(gps.altitude.meters(), 1);
    }

    if (gps.satellites.isValid()) {
        Serial.print(", Sats=");
        Serial.print(gps.satellites.value());
    }

    Serial.println();

    delay(1000);  // wait 1 s between measurements
}
