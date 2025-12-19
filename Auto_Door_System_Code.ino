
#include <WiFi.h>
#include <ESP32Servo.h>

Servo myservo;

// ===== WiFi DETAILS =====
const char* ssid = "Wifi_Name";
const char* password = "Password";

// ===== ThingSpeak =====
const char* server = "api.thingspeak.com";
String apiKey = "Write_API_Key";
WiFiClient client;

// ===== PIN DEFINITIONS =====
const int trigPin  = 14;
const int echoPin  = 12;
const int servoPin = 13;

// ===== DISTANCE THRESHOLDS =====
const int OPEN_DISTANCE  = 8;  // Door opens if object closer than 8 cm
const int CLOSE_DISTANCE = 13; // Door closes if object farther than 13 cm

// ===== SERVO POSITIONS =====
const int SERVO_OPEN   = 90;
const int SERVO_CLOSED = 0;

// ===== STATE =====
bool doorOpen = false;
int nearCount = 0;
int farCount = 0;
const int REQUIRED_HITS = 3; // consecutive readings needed
unsigned long lastUpload = 0;

// ===== FILTER =====
const int SAMPLES = 5;  // moving average samples

// ===== NON-BLOCKING TIMERS =====
unsigned long lastWiFiCheck = 0;
const unsigned long THINGSPEAK_INTERVAL = 15000; // 15 sec

void setup() {
  Serial.begin(115200);

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  myservo.attach(servoPin);
  myservo.write(SERVO_CLOSED);

  // Start WiFi (non-blocking)
  WiFi.begin(ssid, password);
  Serial.println("WiFi connecting...");
}

// ===== DISTANCE FUNCTIONS =====
int readDistance() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, 30000);
  if (duration == 0) return 999;
  return duration * 0.034 / 2;
}

// Moving average filter
int readFilteredDistance() {
  int sum = 0;
  int valid = 0;
  for (int i = 0; i < SAMPLES; i++) {
    int d = readDistance();
    if (d < 400) { // valid reading
      sum += d;
      valid++;
    }
    delay(5); // tiny delay to keep servo smooth
  }
  if (valid == 0) return 999;
  return sum / valid;
}

// ===== NON-BLOCKING WIFI MAINTENANCE =====
void maintainWiFi() {
  unsigned long now = millis();
  if (now - lastWiFiCheck > 3000) { // every 3 sec
    lastWiFiCheck = now;
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi lost → reconnecting...");
      WiFi.disconnect();
      WiFi.begin(ssid, password);
    }
  }
}

// ===== RELIABLE THINGSPEAK UPLOAD =====
void uploadThingSpeak(int distance, bool doorStatus) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, cannot upload");
    return;
  }

  if (!client.connect(server, 80)) {
    Serial.println("ThingSpeak connection failed");
    return;
  }

  String url = "/update?api_key=" + apiKey;
  url += "&field1=" + String(distance);
  url += "&field2=" + String(doorStatus ? 1 : 0);

  client.print("GET " + url + " HTTP/1.1\r\n");
  client.print("Host: api.thingspeak.com\r\n");
  client.print("Connection: close\r\n\r\n");

  Serial.println("✔ ThingSpeak request sent: " + url);

  unsigned long timeout = millis();
  while (!client.available()) {
    if (millis() - timeout > 2000) { // 2 sec timeout
      Serial.println("ThingSpeak response timeout");
      client.stop();
      return;
    }
  }

  while (client.available()) {
    String line = client.readStringUntil('\n');
    Serial.println(line); // should print "0" for success
  }

  client.stop();
  lastUpload = millis();
}

void loop() {
  unsigned long now = millis();
  int distance = readFilteredDistance();

  Serial.print("Distance: ");
  Serial.println(distance);

  // ===== OPEN LOGIC =====
  if (!doorOpen) {
    if (distance < OPEN_DISTANCE) nearCount++;
    else nearCount = 0;

    if (nearCount >= REQUIRED_HITS) {
      myservo.write(SERVO_OPEN);
      doorOpen = true;
      nearCount = 0;
      farCount = 0;
      uploadThingSpeak(distance, doorOpen); // real-time update
    }
  }

  // ===== CLOSE LOGIC =====
  if (doorOpen) {
    if (distance > CLOSE_DISTANCE) farCount++;
    else farCount = 0;

    if (farCount >= REQUIRED_HITS) {
      myservo.write(SERVO_CLOSED);
      doorOpen = false;
      nearCount = 0;
      farCount = 0;
      uploadThingSpeak(distance, doorOpen); // real-time update
    }
  }

  // ===== REGULAR THINGSPEAK UPLOAD =====
  if (now - lastUpload >= THINGSPEAK_INTERVAL) {
    uploadThingSpeak(distance, doorOpen);
  }

  // ===== NON-BLOCKING WIFI CHECK =====
  maintainWiFi();

  delay(20); // tiny delay to keep loop smooth
}