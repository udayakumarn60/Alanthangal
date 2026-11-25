/*
 * ESP-01 watchdog for the Solar Monitor bridge ESP.
 *
 * Listens to the bridge's solar voltage topic. The bridge publishes a reading roughly every
 * 2.5 s, so if nothing arrives for SILENCE_TIMEOUT_MS (60 s) the watchdog energises a relay
 * for CYCLE_HOLD_MS (5 s). The bridge's supply rail runs through the relay's NC contact, so
 * this removes its power once, then restores it.
 *
 * Relay wiring: GPIO RELAY_PIN is HIGH normally (relay released, bridge powered) and goes
 * LOW to energise the relay (NC contact opens, bridge loses power).
 *
 * MQTT control, topic  /ATGL/solar-grid-monitor-powercycler   (payloads are case sensitive):
 *   "Cycle"     power-cycle the bridge once now
 *   "On"        energise the relay and HOLD it (bridge stays unpowered; auto-released after
 *               MANUAL_HOLD_MAX_MS so it cannot be forgotten)
 *   "Off"       release the relay (bridge powered)
 *   "Status"    publish relay state and an info line
 *   "AutoOn" / "AutoOff"   enable / disable the automatic watchdog (not kept over a reboot)
 *
 * Confirmations:
 *   /ATGL/confirm/solar-grid-monitor-powercycler        "On" (relay energised, bridge unpowered) or "Off"
 *   /INFO/solar-grid-monitor-powercycler   relay=.. auto=.. age=..s cycles=.. pending=.. last=..
 *
 * Safety behaviour:
 *   - nothing is evaluated while this ESP has no MQTT connection (its own outage must not
 *     cause a cycle) and the silence timer restarts whenever it reconnects
 *   - after a cycle the bridge gets BOOT_GRACE_MS to start up before it is judged again
 *   - after MAX_QUICK_CYCLES cycles in a row with no data returning (broker or network is
 *     probably the problem, not the bridge) it backs off to one cycle per BACKOFF_MS
 *   - if the Bluepill itself stops sending, the topic goes quiet and the bridge is cycled
 *     even though the bridge is fine: that is expected, it cannot tell the two apart
 */
#undef DEBUG

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <MyWiFiSTA.h>
#include <MyMQTTWrapper.h>
#include <LEDFlasher.h>

// ---------------- configuration ----------------
#define MQTT_SERVER    "atgl.udayakumarn.in"
#define CONTROLLER_ID  "solar-grid-switch-watchdog"      // must be unique on the broker
#define MY_SSID        "atgl-s24-wifi-hotspot"
#define WIFI_PASSWORD  "uday1-mifi"

#define SITE_ADDRESS   "/ATGL"
#ifndef SOLAR_VOLTAGE_TOPIC
#define SOLAR_VOLTAGE_TOPIC "solar-voltage"   // <-- must be the same string the bridge uses
#endif
#define RELAY_NAME     "solar-grid-monitor-powercycler"

#define RELAY_PIN      0                      // GPIO2 (GPIO0 also works). Must be HIGH at boot.
#define RELAY_ACTIVE   LOW                    // level that ENERGISES the relay
#define RELAY_INACTIVE (RELAY_ACTIVE == LOW ? HIGH : LOW)

#define SILENCE_TIMEOUT_MS  60000UL           // no voltage update for this long -> cycle
#define CYCLE_HOLD_MS       5000UL            // how long the relay is energised
#define BOOT_GRACE_MS       120000UL          // time allowed for the bridge to boot and reconnect
#define MAX_QUICK_CYCLES    3                 // cycles in a row before backing off
#define BACKOFF_MS          900000UL          // 15 minutes between cycles once backing off
#define MANUAL_HOLD_MAX_MS  600000UL          // "On" is released automatically after 10 min
#define INFO_INTERVAL_MS    60000UL
// -----------------------------------------------

Flasher      led(1, 500, 500);
WiFiClient   wifiClient;
PubSubClient mqttClient(wifiClient);

MyWifiSTA     wifiStation(MY_SSID, WIFI_PASSWORD, 15);
MyMQTTWrapper myMQTT(&mqttClient, MQTT_SERVER, CONTROLLER_ID, "uday", "swim2sea");

enum RelayMode { MODE_IDLE, MODE_CYCLING, MODE_HELD };

static RelayMode     relayMode        = MODE_IDLE;
static unsigned long relayModeStart   = 0;
static bool          autoEnabled      = true;
static unsigned long lastSolarUpdate  = 0;
static unsigned int  cyclesWithoutData = 0;   // relay operations since data was last seen
static unsigned long totalCycles      = 0;
static unsigned long lastInfo         = 0;
static const char   *lastAction       = "none";
static char          resetReason[48];

static char voltageTopic[96];
static char cmdTopic[64];
static char confirmTopic[64];
static char infoTopic[80];

static void buildTopics() {
  snprintf(voltageTopic, sizeof(voltageTopic), "%s/solar-monitor/%s", SITE_ADDRESS, SOLAR_VOLTAGE_TOPIC);
  snprintf(cmdTopic,     sizeof(cmdTopic),     "%s/%s", SITE_ADDRESS, RELAY_NAME);
  snprintf(confirmTopic, sizeof(confirmTopic), "%s/confirm/%s", SITE_ADDRESS, RELAY_NAME);
  snprintf(infoTopic,    sizeof(infoTopic),    "%s/%s", "/INFO", RELAY_NAME);
}

static bool publishMsg(const char *topic, const char *msg) {
  if (!mqttClient.connected())
    return false;
  return mqttClient.publish(topic, msg);
}

static void driveRelay(bool energised) {
  digitalWrite(RELAY_PIN, energised ? RELAY_ACTIVE : RELAY_INACTIVE);
}

static const char *relayStateText() {
  return relayMode == MODE_IDLE ? "Off" : "On";
}

static void publishRelayState() {
  publishMsg(confirmTopic, relayStateText());
}

static void publishInfo() {
  char buf[180];
  snprintf(buf, sizeof(buf),
           "relay=%s auto=%s age=%lus cycles=%lu pending=%u last=%s reset=%s uptime=%lus heap=%lu",
           relayStateText(), autoEnabled ? "On" : "Off",
           (millis() - lastSolarUpdate) / 1000UL, totalCycles, cyclesWithoutData,
           lastAction, resetReason, millis() / 1000UL, (unsigned long)ESP.getFreeHeap());
  publishMsg(infoTopic, buf);
}

static void startCycle(const char *why) {
  if (relayMode == MODE_CYCLING)
    return;
  driveRelay(true);
  relayMode      = MODE_CYCLING;
  relayModeStart = millis();
  lastAction     = why;
  totalCycles++;
  publishRelayState();
  publishInfo();
}

static void startHold() {
  driveRelay(true);
  relayMode      = MODE_HELD;
  relayModeStart = millis();
  lastAction     = "hold";
  publishRelayState();
}

// Release the relay: the bridge gets its power back and has to boot.
static void releaseRelay() {
  driveRelay(false);
  relayMode = MODE_IDLE;
  if (cyclesWithoutData < 255)
    cyclesWithoutData++;
  lastSolarUpdate = millis();           // judge the bridge from now on (plus boot grace)
  publishRelayState();
  publishInfo();
}

// How long without data before the next automatic cycle
static unsigned long silenceLimit() {
  if (cyclesWithoutData == 0)
    return SILENCE_TIMEOUT_MS;
  if (cyclesWithoutData < MAX_QUICK_CYCLES)
    return BOOT_GRACE_MS + SILENCE_TIMEOUT_MS;
  return BACKOFF_MS;
}

static void updateRelay() {
  unsigned long now = millis();
  switch (relayMode) {
    case MODE_CYCLING:
      if ((now - relayModeStart) >= CYCLE_HOLD_MS)
        releaseRelay();
      break;
    case MODE_HELD:
      if ((now - relayModeStart) >= MANUAL_HOLD_MAX_MS) {
        lastAction = "hold-timeout";
        releaseRelay();
      }
      break;
    case MODE_IDLE:
      if (autoEnabled && mqttClient.connected() &&
          (now - lastSolarUpdate) >= silenceLimit())
        startCycle("auto");
      break;
  }
}

void processCallback(char *topic, byte *payload, unsigned int length) {
  // Any reading from the bridge proves it is alive
  if (strcmp(topic, voltageTopic) == 0) {
    lastSolarUpdate   = millis();
    cyclesWithoutData = 0;
    return;
  }

  if (strcmp(topic, cmdTopic) != 0)
    return;

  char buf[20];
  if (length == 0 || length >= sizeof(buf))
    return;
  memcpy(buf, payload, length);
  buf[length] = '\0';

  if (!strcmp(buf, "Cycle")) {
    if (relayMode == MODE_IDLE)
      startCycle("mqtt-cycle");
    else
      publishRelayState();
  } else if (!strcmp(buf, "On")) {
    if (relayMode != MODE_HELD)
      startHold();
    else
      publishRelayState();
  } else if (!strcmp(buf, "Off")) {
    if (relayMode != MODE_IDLE) {
      lastAction = "mqtt-off";
      releaseRelay();
    } else {
      publishRelayState();
    }
  } else if (!strcmp(buf, "Status")) {
    publishRelayState();
    publishInfo();
  } else if (!strcmp(buf, "AutoOn")) {
    autoEnabled     = true;
    lastSolarUpdate = millis();         // fresh silence timer
    publishInfo();
  } else if (!strcmp(buf, "AutoOff")) {
    autoEnabled = false;
    publishInfo();
  }
}

static void subscribeAll() {
  myMQTT.subscribe(voltageTopic);
  myMQTT.subscribe(cmdTopic);
}

void setup() {
  // Relay released before anything else. The base/pull-up circuit should also hold it
  // released while the ESP is booting.
  digitalWrite(RELAY_PIN, RELAY_INACTIVE);
  pinMode(RELAY_PIN, OUTPUT);

#ifdef DEBUG
  Serial.begin(115200);
  Serial.println();
  Serial.println("Starting...");
#endif
  buildTopics();
  strncpy(resetReason, ESP.getResetReason().c_str(), sizeof(resetReason) - 1);
  resetReason[sizeof(resetReason) - 1] = '\0';
  for (char *p = resetReason; *p; p++)
    if (*p == ' ') *p = '_';

  wifiStation.setRestartAfter(600000UL);   // reboot if WiFi is down for 10 minutes
  myMQTT.setRestartAfter(30);              // reboot after 30 failed MQTT reconnects in a row

#ifdef DEBUG
  Serial.println("Connect to Wifi");
#endif

  while (!wifiStation.wifiConnected()) {
    wifiStation.keepalive();
    delay(200);
  }

#ifdef DEBUG
  Serial.println("WiFi Connected");
#endif

  while (!myMQTT.connected()) {
    if (!myMQTT.connectMe())
      delay(500);
    if (millis() > 300000UL)
      ESP.restart();                       // could not reach the broker for 5 minutes
    wifiStation.keepalive();
    yield();
  }

#ifdef DEBUG
  Serial.println("MQTT Connected");
#endif

  myMQTT.setCallback(processCallback);
  subscribeAll();
  lastSolarUpdate = millis();
  publishRelayState();
  publishInfo();
  lastInfo = millis();

#ifdef DEBUG
  Serial.println("Setup Complete");
#endif

}

void loop() {
  led.Update();
  wifiStation.keepalive();

  if (!myMQTT.keepalive()) {               // just reconnected to the broker
    subscribeAll();
    lastSolarUpdate = millis();            // give the bridge a full silence window again
    publishRelayState();
  }

  updateRelay();

  if ((millis() - lastInfo) >= INFO_INTERVAL_MS) {
    lastInfo = millis();
    publishInfo();
  }
}
