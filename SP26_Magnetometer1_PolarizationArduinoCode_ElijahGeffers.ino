const int PIN_COIL = 8;     // MOSFET gate
const int PIN_GREEN = 3;    // Green LED
const int PIN_RED = 5;      // Red LED

const unsigned long ON_TIME = 5000;   // 5 seconds
const unsigned long OFF_TIME = 5000;  // 5 seconds

void setup() {
pinMode(PIN_COIL, OUTPUT);
pinMode(PIN_GREEN, OUTPUT);
pinMode(PIN_RED, OUTPUT);

// Start in OFF state
digitalWrite(PIN_COIL, LOW);
digitalWrite(PIN_GREEN, LOW);
digitalWrite(PIN_RED, HIGH);
}

void loop() {
// ON state
digitalWrite(PIN_COIL, HIGH);   // energize coil
digitalWrite(PIN_GREEN, HIGH);  // green LED ON
digitalWrite(PIN_RED, LOW);     // red LED OFF
delay(ON_TIME);

// OFF state
digitalWrite(PIN_COIL, LOW);    // turn off coil
digitalWrite(PIN_GREEN, LOW);   // green LED OFF
digitalWrite(PIN_RED, HIGH);    // red LED ON
delay(OFF_TIME);
}
