/*
 * VECTRA - High-Speed Line Follower
 * ESP32 + TB6612FNG + 8-array IR + 2 wing sensors
 * Logic: Decisive LSRB intersections + line-lost recovery + anti-wobble PID

#include <Arduino.h>
#include <QTRSensors.h>

// ==========================================================
// 1. HARDWARE SETUP
// ==========================================================
QTRSensors qtr;

// Motor Pins (Channel A = LEFT, Channel B = RIGHT)
#define PWMA 23
#define AIN1 21
#define AIN2 22
#define PWMB 16
#define BIN1 18
#define BIN2 4

// Main 8-array sensor pins (analog)
const uint8_t SensorCount = 8;
const uint8_t sensorPins[SensorCount] = {14, 27, 26, 25, 33, 32, 35, 34};
uint16_t sensorValues[SensorCount];

// Wing sensors (analog)
#define PIN_LEFT_WING  39   // GPIO39 / VN pad
#define PIN_RIGHT_WING 13

// Black/white threshold (12-bit ADC, 0..4095)
const uint16_t THRESHOLD = 4090;

// ==========================================================
// 2. TUNABLE VARIABLES
// ==========================================================
float Kp = 15;
float Kd = 30000;
int   baseSpeed = 250;
int   rightMotorOffset = 10;
float lastError = 0;

// Logic memory: -1 = last turned Left, 1 = last turned Right
int lastTurn = 0;

// ==========================================================
// 3. HELPER FUNCTIONS
// ==========================================================
void driveMotors(int left, int right) {
  if (right > 0)      right += rightMotorOffset;
  else if (right < 0) right -= rightMotorOffset;

  left  = constrain(left,  -255, 255);
  right = constrain(right, -255, 255);

  if (left >= 0) { digitalWrite(AIN1, HIGH); digitalWrite(AIN2, LOW); }
  else           { digitalWrite(AIN1, LOW);  digitalWrite(AIN2, HIGH); left = -left; }

  if (right >= 0) { digitalWrite(BIN1, HIGH); digitalWrite(BIN2, LOW); }
  else            { digitalWrite(BIN1, LOW);  digitalWrite(BIN2, HIGH); right = -right; }

  analogWrite(PWMA, left);
  analogWrite(PWMB, right);
}

// Weighted-average error from the main 8 sensors
float calculateError() {
  float weightedSum = 0;
  int   activeSensors = 0;
  int   weights[8] = {-40, -30, -20, -10, 10, 20, 30, 40};

  for (int i = 0; i < 8; i++) {
    if (sensorValues[i] > THRESHOLD) {
      weightedSum += weights[i];
      activeSensors++;
    }
  }
  if (activeSensors == 0) return -999;   // line lost
  return weightedSum / activeSensors;
}

void setup() {
  Serial.begin(115200);

  pinMode(AIN1, OUTPUT); pinMode(AIN2, OUTPUT); pinMode(PWMA, OUTPUT);
  pinMode(BIN1, OUTPUT); pinMode(BIN2, OUTPUT); pinMode(PWMB, OUTPUT);

  pinMode(PIN_LEFT_WING,  INPUT);
  pinMode(PIN_RIGHT_WING, INPUT);

  qtr.setTypeAnalog();
  qtr.setSensorPins(sensorPins, SensorCount);

  delay(500);
}

// ==========================================================
// 4. MAIN LOOP (decisive logic + PID)
// ==========================================================
void loop() {
  // 1. Read sensors
  qtr.read(sensorValues);
  bool leftWing  = (analogRead(PIN_LEFT_WING)  > THRESHOLD);
  bool rightWing = (analogRead(PIN_RIGHT_WING) > THRESHOLD);
  bool centerBlack = (sensorValues[3] > THRESHOLD) || (sensorValues[4] > THRESHOLD);

  // 2. Error
  float error = calculateError();

  // ---- PRIORITY 1: LSRB intersection + acute logic ----
  // CASE A: acute left (V-shape)
  if (leftWing && !centerBlack) {
    lastTurn = -1;
    driveMotors(-120, 160);
    delay(50);
    return;
  }
  // CASE B: normal left (left priority)
  else if (leftWing && centerBlack) {
    lastTurn = -1;
    driveMotors(-120, 160);
    delay(50);
    return;
  }
  // CASE C: straight beats right -> fall through to PID
  else if (centerBlack && (rightWing || sensorValues[6] > THRESHOLD || sensorValues[7] > THRESHOLD)) {
    // do nothing; go straight via PID
  }
  // CASE D: acute right (V-shape)
  else if (!centerBlack && (rightWing || sensorValues[6] > THRESHOLD || sensorValues[7] > THRESHOLD)) {
    lastTurn = 1;
    delay(50);
    driveMotors(160, -120);
    return;
  }

  // ---- PRIORITY 2: line lost recovery ----
  if (error == -999) {
    if (lastTurn == 1) driveMotors(150, -150);
    else               driveMotors(-150, 150);
    return;
  }

  // ---- PRIORITY 3: anti-wobble PID ----
  if (abs(error) < 4) error = 0;            // dead-band

  float P = error * Kp;
  float D = (error - lastError) * Kd;
  float correction = P + D;
  lastError = error;

  int leftSpeed  = baseSpeed + correction;
  int rightSpeed = baseSpeed - correction;
  driveMotors(leftSpeed, rightSpeed);

  if (error < -10)      lastTurn = 1;
  else if (error > 10)  lastTurn = -1;
}
