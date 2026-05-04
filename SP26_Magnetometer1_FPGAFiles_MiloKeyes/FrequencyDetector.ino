// FrequencyDetector.ino
// Arduino interface to FrequencyCounter FPGA module.
// Drives cnt_enable / start_transmit, reads done / sclk / freqOut,
// and computes frequency in Hz from the 32-bit cycle count.

#include <SD.h>
#include <TinyGPSPlus.h>

// ────────────────────────────────────────────────────────────────────
//  Pin assignments  (change to match your wiring)
// ────────────────────────────────────────────────────────────────────
const int PIN_CNT_ENABLE     = 2;   // Arduino OUT → FPGA cnt_enable
const int PIN_START_TRANSMIT = 3;   // Arduino OUT → FPGA start_transmit
const int PIN_RST_N          = 4;   // Arduino OUT → FPGA rst_n
const int PIN_DONE           = 5;   // Arduino IN  ← FPGA done
const int PIN_SCLK           = 6;   // Arduino IN  ← FPGA sclk
const int PIN_FREQ_OUT       = 7;   // Arduino IN  ← FPGA freqOut

// GT-U7 GPS on Teensy 4.1 Serial1:
//   GPS TX -> Teensy pin 0 (RX1)
//   GPS RX -> Teensy pin 1 (TX1, optional)
const int PIN_GPS_RX1 = 0;
const int PIN_GPS_TX1 = 1;

// ────────────────────────────────────────────────────────────────────
//  FPGA / measurement constants
// ────────────────────────────────────────────────────────────────────
const double   FPGA_CLK_HZ       = 12000400.0;  // 12 MHz FPGA clock
const uint16_t NUM_PERIODS       = 2048;         // rising edges counted
const uint32_t MEASURE_TIMEOUT_MS = 30000;       // 30 s max wait for done
const uint32_t TX_TIMEOUT_MS      = 5000;        // 5 s max for serial readout
const int      TX_BITS            = 32;

const uint32_t GPS_BAUD_RATE      = 9600;

#ifdef BUILTIN_SDCARD
const int SD_CS_PIN = BUILTIN_SDCARD;
#else
const int SD_CS_PIN = 10;
#endif

TinyGPSPlus gps;
bool sdReady = false;
char currentLogFileName[13] = "FREQ0000.CSV";

void updateGPS() {
    while (Serial1.available() > 0) {
        gps.encode(Serial1.read());
    }
}

void printOrNA(Print &out, bool valid, double value, int decimals) {
    if (valid) {
        out.print(value, decimals);
    } else {
        out.print("NA");
    }
}

void printTwoDigits(Print &out, uint8_t v) {
    if (v < 10) {
        out.print('0');
    }
    out.print(v);
}

void printFourDigits(Print &out, uint16_t v) {
    if (v < 1000) {
        out.print('0');
    }
    if (v < 100) {
        out.print('0');
    }
    if (v < 10) {
        out.print('0');
    }
    out.print(v);
}

bool createSessionLogFile() {
    if (!sdReady) {
        return false;
    }

    for (uint16_t i = 1; i <= 9999; i++) {
        snprintf(currentLogFileName, sizeof(currentLogFileName), "FREQ%04u.CSV", (unsigned)i);
        if (SD.exists(currentLogFileName)) {
            continue;
        }

        File f = SD.open(currentLogFileName, FILE_WRITE);
        if (!f) {
            Serial.println("ERROR: failed to create session log file");
            return false;
        }

        f.println("boot_ms,frequency_hz,gps_date_utc,gps_time_utc,latitude,longitude,altitude_m,satellites,hdop");
        f.close();

        Serial.print("Logging to ");
        Serial.println(currentLogFileName);
        return true;
    }

    Serial.println("ERROR: no free session log filename available");
    return false;
}

void printGpsDebugLine() {
    Serial.print("GPS UTC ");
    if (gps.date.isValid() && gps.time.isValid()) {
        printFourDigits(Serial, gps.date.year());
        Serial.print('-');
        printTwoDigits(Serial, gps.date.month());
        Serial.print('-');
        printTwoDigits(Serial, gps.date.day());
        Serial.print(' ');
        printTwoDigits(Serial, gps.time.hour());
        Serial.print(':');
        printTwoDigits(Serial, gps.time.minute());
        Serial.print(':');
        printTwoDigits(Serial, gps.time.second());
        Serial.print('.');
        printTwoDigits(Serial, gps.time.centisecond());
    } else {
        Serial.print("NA");
    }

    Serial.print(" | Lat: ");
    printOrNA(Serial, gps.location.isValid(), gps.location.lat(), 6);
    Serial.print(" | Lon: ");
    printOrNA(Serial, gps.location.isValid(), gps.location.lng(), 6);
    Serial.print(" | Alt(m): ");
    printOrNA(Serial, gps.altitude.isValid(), gps.altitude.meters(), 2);
    Serial.print(" | Sats: ");
    if (gps.satellites.isValid()) {
        Serial.print(gps.satellites.value());
    } else {
        Serial.print("NA");
    }
    Serial.print(" | HDOP: ");
    if (gps.hdop.isValid()) {
        Serial.print(gps.hdop.hdop(), 2);
    } else {
        Serial.print("NA");
    }
    Serial.println();
}

void logMeasurement(double freqHz) {
    if (!sdReady) {
        return;
    }

    File f = SD.open(currentLogFileName, FILE_WRITE);
    if (!f) {
        Serial.println("ERROR: failed to open log file");
        return;
    }

    f.print(millis());
    f.print(',');
    f.print(freqHz, 2);
    f.print(',');

    if (gps.date.isValid()) {
        printFourDigits(f, gps.date.year());
        f.print('-');
        printTwoDigits(f, gps.date.month());
        f.print('-');
        printTwoDigits(f, gps.date.day());
    } else {
        f.print("NA");
    }
    f.print(',');

    if (gps.time.isValid()) {
        printTwoDigits(f, gps.time.hour());
        f.print(':');
        printTwoDigits(f, gps.time.minute());
        f.print(':');
        printTwoDigits(f, gps.time.second());
        f.print('.');
        printTwoDigits(f, gps.time.centisecond());
    } else {
        f.print("NA");
    }
    f.print(',');

    printOrNA(f, gps.location.isValid(), gps.location.lat(), 6);
    f.print(',');
    printOrNA(f, gps.location.isValid(), gps.location.lng(), 6);
    f.print(',');
    printOrNA(f, gps.altitude.isValid(), gps.altitude.meters(), 2);
    f.print(',');

    if (gps.satellites.isValid()) {
        f.print(gps.satellites.value());
    } else {
        f.print("NA");
    }
    f.print(',');

    if (gps.hdop.isValid()) {
        f.print(gps.hdop.hdop(), 2);
    } else {
        f.print("NA");
    }
    f.println();
    f.close();
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
        updateGPS();
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
        updateGPS();
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
    Serial1.begin(GPS_BAUD_RATE);

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

    sdReady = SD.begin(SD_CS_PIN);
    if (!sdReady) {
        Serial.println("WARNING: SD init failed; logging disabled");
    } else {
        if (!createSessionLogFile()) {
            sdReady = false;
        }
    }

    Serial.println("FrequencyDetector ready.");
}

// ────────────────────────────────────────────────────────────────────
//  loop()
// ────────────────────────────────────────────────────────────────────
void loop() {
    updateGPS();
    double freq = measure();

    if (freq < 0.0) {
        Serial.println("ERROR: measurement timed out");
        printGpsDebugLine();
        resetFPGA();  // reset FPGA FSM to recover from error state
    } else {
        Serial.print("Frequency: ");
        Serial.print(freq, 2);
        Serial.print(" Hz | ");
        printGpsDebugLine();
        logMeasurement(freq);
    }

    delay(1000);  // wait 1 s between measurements
}
