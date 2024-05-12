#include <Arduino.h>
#include <freertos/task.h>
#include <esp_wifi.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include <AsyncWiFiManager.h>
#include <ConfigItem.h>
#include <EEPROMConfig.h>
#include <ImprovWiFi.h>
#include <Update.h>
#include <ASyncOTAWebUpdate.h>
#include "WSHandler.h"
#include "WSMenuHandler.h"
#include "WSConfigHandler.h"
#include "WSLEDConfigHandler.h"
#include "WSInfoHandler.h"
#include "MQTTBroker.h"
#include "BambuLights.h"

#define DEBUG(...) { Serial.println(__VA_ARGS__); }
#ifndef DEBUG
#define DEBUG(...) {  }
#endif

const char *manifest[]{
    // Firmware name
    "Bambu Lighting",
    // Firmware version
    "0.0.1",
    // Hardware chip/variant
    "ESP32",
    // Device name
    "Bambu"};

StringConfigItem hostName("hostname", 63, "bambulights");
AsyncWebServer server(80);
AsyncWebSocket ws("/ws"); // access at ws://[esp ip]/ws
DNSServer dns;
AsyncWiFiManager wifiManager(&server, &dns);
ASyncOTAWebUpdate otaUpdater(Update, "update", "secretsauce");
AsyncWiFiManagerParameter *hostnameParam;
MQTTBroker mqttBroker;
BambuLights bambuLights(36, 27);

SemaphoreHandle_t wsMutex;

TaskHandle_t wifiManagerTask;
TaskHandle_t improvTask;
TaskHandle_t ledTask;
TaskHandle_t commitEEPROMTask;
TaskHandle_t mqttBrokerTask;

String ssid = "BambuLights";

BaseConfigItem* mqttConfigSet[] = {
  &MQTTBroker::getHost(),
  &MQTTBroker::getUser(),
  &MQTTBroker::getPassword(),
  &MQTTBroker::getPort(),
  &MQTTBroker::getSerialNumber(),
  0
};

CompositeConfigItem mqttConfig("mqtt", 0, mqttConfigSet);

BaseConfigItem* rootConfigSet[] = {
  &mqttConfig,
  &BambuLights::getAllConfig(),
  &BambuLights::getLightMode(),
  0
};

CompositeConfigItem rootConfig("root", 0, rootConfigSet);

EEPROMConfig config(rootConfig);

// Declare some functions
void setWiFiCredentials(const char *ssid, const char *password);
void setWiFiAP(bool);
void infoCallback();

String getChipId(void)
{
	uint8_t macid[6];

  esp_efuse_mac_get_default(macid);
  String chipId = String((uint32_t)(macid[5] + (((uint32_t)(macid[4])) << 8) + (((uint32_t)(macid[3])) << 16)), HEX);
  chipId.toUpperCase();
  return chipId;
}

String chipId = getChipId();

void createSSID() {
	// Create a unique SSID that includes the hostname. Max SSID length is 32!
	ssid = (chipId + hostName).substring(0, 31);
}

void improvTaskFn(void *pArg)
{
  ImprovWiFi improvWiFi(
      manifest[0],
      manifest[1],
      manifest[2],
      manifest[3]);

  // improvWiFi.setInfoCallback([](const char *msg) {tfts->setStatus(msg);});
  improvWiFi.setWiFiCallback(setWiFiCredentials);

  DEBUG("Running improv");

  while (true)
  {
    xSemaphoreTake(wsMutex, portMAX_DELAY);
    improvWiFi.loop();
    xSemaphoreGive(wsMutex);

    delay(10);
  }
}

template<class T>
void onMqttParamsChanged(ConfigItem<T> &item) {
	mqttBroker.init(ssid);
}

void ledTaskFn(void *pArg) {
	bambuLights.begin();
	BambuLights::State prevLightsState = BambuLights::noWiFi;
	long startIdleTimeMs = 0;
	bool inFinishedPhase = false;

	while (true) {
		mqttBroker.checkConnection();

		bambuLights.setBrightness(255);	// Brightness scale factor

		BambuLights::State lightsState = BambuLights::noWiFi;

		if (WiFi.isConnected()) {		
			// Run the state machine
			switch (mqttBroker.getState()) {
				case MQTTBroker::disconnected:
					lightsState = BambuLights::noPrinter;
					break;
				case MQTTBroker::idle:
					if (prevLightsState == BambuLights::printing && !inFinishedPhase && BambuLights::getIdleTimeout() > 0) {
						inFinishedPhase = true;
						startIdleTimeMs = millis();
						lightsState = BambuLights::finished;
					} else {
						if (inFinishedPhase && (millis() - startIdleTimeMs < BambuLights::getIdleTimeout() * 60000)) {
							lightsState = BambuLights::finished;
						} else {
							inFinishedPhase = false;
							lightsState = BambuLights::printer;
						}
					}
					break;
				case MQTTBroker::printing:
					lightsState = BambuLights::printing;
					break;
				case MQTTBroker::no_lights:
					lightsState = BambuLights::no_lights;
					break;
				case MQTTBroker::warning:
					lightsState = BambuLights::warning;
					break;
				case MQTTBroker::error:
					lightsState = BambuLights::error;
					break;
			}
			prevLightsState = lightsState;

			// Override the results if told to
			if (BambuLights::getLightMode() == 0) {
				lightsState = BambuLights::white;
			} else if (BambuLights::getLightMode() == 1) {
				lightsState = BambuLights::no_lights;
			}
		}

		bambuLights.setState(lightsState);

		bambuLights.loop();
		delay(16);
	}
}

void setWiFiCredentials(const char *ssid, const char *password)
{
  WiFi.disconnect();
  wifiManager.setRouterCredentials(ssid, password);
  wifiManager.connect();
}

void sendUpdateForm(AsyncWebServerRequest *request) {
	request->send(LittleFS, "/update.html");
}

void sendUpdatingInfo(AsyncResponseStream *response, boolean hasError) {
  response->print("<html><head><meta http-equiv=\"refresh\" content=\"10; url=/\"></head><body>");

  hasError ?
      response->print("Update failed: please wait while the device reboots") :
      response->print("Update OK: please wait while the device reboots");

  response->print("</body></html>");
}

void sendFavicon(AsyncWebServerRequest *request) {
	DEBUG("Got favicon request")
	request->send(LittleFS, "/assets/favicon-32x32.png", "image/png");
}

String* items[] {
	&WSMenuHandler::mqttMenu,
	&WSMenuHandler::ledsMenu,
	&WSMenuHandler::infoMenu,
	0
};

String ledConfigCallback() {
	String json;
	json.reserve(20);
	json.concat("\"");
	json.concat(BambuLights::getLightMode().name);
	json.concat("\":");
	json.concat(BambuLights::getLightMode().toJSON());

	return json;
}

WSMenuHandler wsMenuHandler(items);
WSConfigHandler wsMqttHandler(rootConfig, "mqtt");
WSLEDConfigHandler wsLEDHandler(rootConfig, "leds", ledConfigCallback);
WSInfoHandler wsInfoHandler(infoCallback);

// Order of this needs to match the numbers in WSMenuHandler.cpp
WSHandler* wsHandlers[] {
	&wsMenuHandler,
	&wsMqttHandler,
	&wsLEDHandler,
	&wsInfoHandler,
	NULL,
	NULL,
	NULL,
	NULL
};


void infoCallback() {
	wsInfoHandler.setSsid(ssid);
	wsInfoHandler.setRevision(manifest[1]);

	wsInfoHandler.setFSSize(String(LittleFS.totalBytes()));
	wsInfoHandler.setFSFree(String(LittleFS.totalBytes() - LittleFS.usedBytes()));

	wsInfoHandler.setHostname(hostName);
}

void broadcastUpdate(String originalKey, const BaseConfigItem& item) {
	xSemaphoreTake(wsMutex, portMAX_DELAY);

	JsonDocument doc;
	JsonObject root = doc.to<JsonObject>();

	root["type"] = "sv.update";

	JsonVariant value = root.createNestedObject("value");
	String rawJSON = item.toJSON();	// This object needs to hang around until we are done serializing.
	value[originalKey] = serialized(rawJSON.c_str());

	size_t len = measureJson(root);
	AsyncWebSocketMessageBuffer * buffer = ws.makeBuffer(len); //  creates a buffer (len + 1) for you.
	if (buffer) {
		serializeJson(root, (char *)buffer->get(), len + 1);
		ws.textAll(buffer);
	}

	xSemaphoreGive(wsMutex);
}

void updateValue(String originalKey, String _key, String value, BaseConfigItem *item) {
	int index = _key.indexOf('-');
	if (index == -1) {
		const char* key = _key.c_str();
		item = item->get(key);
		if (item != 0) {
			item->fromString(value);
			item->put();

			// Order of below is important to maintain external consistency
			broadcastUpdate(originalKey, *item);
			item->notify();
		} else if (_key == "wifi_ap") {
			setWiFiAP(value == "true" ? true : false);
		} else if (_key == "hostname") {
			hostName = value;
			hostName.put();
			config.commit();
			ESP.restart();
		}
	} else {
		String firstKey = _key.substring(0, index);
		String nextKey = _key.substring(index+1);
		updateValue(originalKey, nextKey, value, item->get(firstKey.c_str()));
	}
}

void updateValue(int screen, String pair) {
	int index = pair.indexOf(':');
	DEBUG(pair)
	// _key has to hang around because key points to an internal data structure
	String _key = pair.substring(0, index);
	String value = pair.substring(index+1);

	updateValue(_key, _key, value, &rootConfig);
}

/*
 * Handle application protocol
 */
void handleWSMsg(AsyncWebSocketClient *client, char *data) {
	String wholeMsg(data);
	int code = wholeMsg.substring(0, wholeMsg.indexOf(':')).toInt();

	if (code < 9) {
		wsHandlers[code]->handle(client, data);
	} else {
		String message = wholeMsg.substring(wholeMsg.indexOf(':')+1);
		int screen = message.substring(0, message.indexOf(':')).toInt();
		String pair = message.substring(message.indexOf(':')+1);
		updateValue(screen, pair);
	}
}

/*
 * Handle transport protocol
 */
void wsHandler(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
	//Handle WebSocket event
	switch (type) {
	case WS_EVT_CONNECT:
		DEBUG("WS connected")
		;
		break;
	case WS_EVT_DISCONNECT:
		DEBUG("WS disconnected")
		;
		break;
	case WS_EVT_ERROR:
		DEBUG("WS error")
		;
		DEBUG((char* )data)
		;
		break;
	case WS_EVT_PONG:
		DEBUG("WS pong")
		;
		break;
	case WS_EVT_DATA:	// Yay we got something!
		DEBUG("WS data")
		;
		AwsFrameInfo * info = (AwsFrameInfo*) arg;
		if (info->final && info->index == 0 && info->len == len) {
			//the whole message is in a single frame and we got all of it's data
			if (info->opcode == WS_TEXT) {
				DEBUG("WS text data");
				data[len] = 0;
				handleWSMsg(client, (char *) data);
			} else {
				DEBUG("WS binary data");
			}
		} else {
			DEBUG("WS data was split up!");
		}
		break;
	}
}

void mainHandler(AsyncWebServerRequest *request) {
	DEBUG("Got request")
	request->send(LittleFS, "/index.html");
}

void configureWebServer() {
	server.serveStatic("/", LittleFS, "/");
	server.on("/", HTTP_GET, mainHandler).setFilter(ON_STA_FILTER);
	server.on("/assets/favicon-32x32.png", HTTP_GET, sendFavicon);
	server.serveStatic("/assets", LittleFS, "/assets");
	otaUpdater.init(server, "/update", sendUpdateForm, sendUpdatingInfo);

	// attach AsyncWebSocket
	ws.onEvent(wsHandler);
	server.addHandler(&ws);
	server.begin();
	ws.enable(true);
}

void setWiFiAP(bool on) {
	if (on) {
		wifiManager.startConfigPortal(ssid.c_str(), "secretsauce");
	} else {
		wifiManager.stopConfigPortal();
	}
}

void SetupServer() {
	DEBUG("SetupServer()");
	hostName = String(hostnameParam->getValue());
	hostName.put();
	config.commit();
	createSSID();
	wifiManager.setAPCredentials(ssid.c_str(), "secretsauce");
	DEBUG(hostName.value);
	MDNS.begin(hostName.value.c_str());
  MDNS.addService("http", "tcp", 80);

//	getTime();
}

void commitEEPROMTaskFn(void *pArg) {
	while(true) {
		delay(60000);
		Serial.println("Committing config");
		config.commit();
	}
}

void initFromEEPROM() {
//	config.setDebugPrint(debugPrint);
	config.init();
//	rootConfig.debug(debugPrint);
	DEBUG(hostName);
	rootConfig.get();	// Read all of the config values from EEPROM
	DEBUG(hostName);

	hostnameParam = new AsyncWiFiManagerParameter("Hostname", "device host name", hostName.value.c_str(), 63);
}

void connectedHandler() {
	DEBUG("connectedHandler");
	wifi_ps_type_t wifi_ps_mode;
	esp_wifi_get_ps(&wifi_ps_mode);
	DEBUG(wifi_ps_mode);
	MDNS.end();
	MDNS.begin(hostName.value.c_str());
	MDNS.addService("http", "tcp", 80);
}

void apChange(AsyncWiFiManager *wifiManager) {
	DEBUG("apChange()");
	DEBUG(wifiManager->isAP());
	wifi_ps_type_t wifi_ps_mode;
	esp_wifi_get_ps(&wifi_ps_mode);
	DEBUG(wifi_ps_mode);
//	esp_wifi_set_ps(WIFI_PS_NONE);
//	broadcastUpdate(wifiCallback());
}

void wifiManagerTaskFn(void *pArg) {

	while(true) {
		xSemaphoreTake(wsMutex, portMAX_DELAY);
		wifiManager.loop();
		xSemaphoreGive(wsMutex);

		delay(50);
	}
}

void setup()
{
	/*
	 * setup() runs on core 1
	 */
  Serial.begin(115200);
  Serial.setDebugOutput(true);

	wsMutex = xSemaphoreCreateMutex();

	createSSID();

	EEPROM.begin(2048);
	initFromEEPROM();

	LittleFS.begin();

  xTaskCreatePinnedToCore(
    commitEEPROMTaskFn,   /* Function to implement the task */
    "Commit EEPROM task", /* Name of the task */
    2048,                 /* Stack size in words */
    NULL,                 /* Task input parameter */
    tskIDLE_PRIORITY,     /* More than background tasks */
    &commitEEPROMTask,    /* Task handle. */
    xPortGetCoreID());

  xTaskCreatePinnedToCore(
		ledTaskFn, /* Function to implement the task */
		"led task", /* Name of the task */
		1500,  /* Stack size in words */
		NULL,  /* Task input parameter */
		tskIDLE_PRIORITY + 2,  /* Priority of the task (idle) */
		&ledTask,  /* Task handle. */
		1	/*  */
	);

  xTaskCreatePinnedToCore(
		improvTaskFn, /* Function to implement the task */
		"Improv task", /* Name of the task */
		2048,  /* Stack size in words */
		NULL,  /* Task input parameter */
		tskIDLE_PRIORITY + 1,  /* More than background tasks */
		&improvTask,  /* Task handle. */
		0
	);

  wifiManager.setDebugOutput(true);
  wifiManager.setHostname(hostName.value.c_str()); // name router associates DNS entry with
  wifiManager.setCustomOptionsHTML("<br><form action='/t' name='time_form' method='post'><button name='time' onClick=\"{var now=new Date();this.value=now.getFullYear()+','+(now.getMonth()+1)+','+now.getDate()+','+now.getHours()+','+now.getMinutes()+','+now.getSeconds();} return true;\">Set Clock Time</button></form><br><form action=\"/app.html\" method=\"get\"><button>Configure Clock</button></form>");
  wifiManager.addParameter(hostnameParam);
  wifiManager.setSaveConfigCallback(SetupServer);
  wifiManager.setConnectedCallback(connectedHandler);
  wifiManager.setConnectTimeout(2000); // milliseconds
  wifiManager.setAPCallback(apChange);
  wifiManager.setAPCredentials(ssid.c_str(), "secretsauce");
  wifiManager.start();

  configureWebServer();

   /*
	* Very occasionally the wifi stack will close with a beacon timeout error.
	*
	* This is apparently related to it not being able to allocate a buffer at
	* some point because of a transitory memory issue.
	*
	* In older versions this issue is not well handled so the wifi stack never
	* recovers. The work-around is to not allow the wifi radio to sleep.
	*
	* https://github.com/espressif/esp-idf/issues/11615
	*/
  esp_wifi_set_ps(WIFI_PS_NONE);

  xTaskCreatePinnedToCore(
    wifiManagerTaskFn,    /* Function to implement the task */
    "WiFi Manager task",  /* Name of the task */
    4096,                 /* Stack size in words */
    NULL,                 /* Task input parameter */
    tskIDLE_PRIORITY + 2, /* Priority of the task (idle) */
    &wifiManagerTask,     /* Task handle. */
    0);

	MQTTBroker::getHost().setCallback(onMqttParamsChanged);
	MQTTBroker::getPort().setCallback(onMqttParamsChanged);
	MQTTBroker::getUser().setCallback(onMqttParamsChanged);
	MQTTBroker::getPassword().setCallback(onMqttParamsChanged);
	MQTTBroker::getSerialNumber().setCallback(onMqttParamsChanged);
	mqttBroker.init(ssid);

  Serial.print("setup() running on core ");
  Serial.println(xPortGetCoreID());

  vTaskDelete(NULL);	// Delete this task (so loop() won't be called)
}

void loop()
{
  // Should never be called
}
