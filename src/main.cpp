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
#include "MQTTHABroker.h"
#include "BambuLights.h"

#define DEBUG(...) { Serial.println(__VA_ARGS__); }
#ifndef DEBUG
#define DEBUG(...) {  }
#endif

static String TRUE_STRING("true");
static String FALSE_STRING("false");

const char *manifest[]{
    // Firmware name
    "Bambu Lighting",
    // Firmware version
    "0.4.3",
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
MQTTHABroker mqttHABroker;
BambuLights *bambuLights;

SemaphoreHandle_t wsMutex;

TaskHandle_t wifiManagerTask;
TaskHandle_t improvTask;
TaskHandle_t ledTask;
TaskHandle_t commitEEPROMTask;

String ssid = "BambuLights";

BaseConfigItem* mqttConfigSet[] = {
  &hostName,
  &MQTTBroker::getHost(),
  &MQTTBroker::getUser(),
  &MQTTBroker::getPassword(),
  &MQTTBroker::getPort(),
  &MQTTBroker::getSerialNumber(),
  0
};

CompositeConfigItem mqttConfig("mqtt", 0, mqttConfigSet);

BaseConfigItem* mqttHAConfigSet[] = {
  &MQTTHABroker::getHost(),
  &MQTTHABroker::getUser(),
  &MQTTHABroker::getPassword(),
  &MQTTHABroker::getPort(),
  0
};

CompositeConfigItem mqttHAConfig("mqtt_ha", 0, mqttHAConfigSet);

BaseConfigItem* rootConfigSet[] = {
  &mqttConfig,
  &mqttHAConfig,
  &BambuLights::getAllConfig(),
  0
};

CompositeConfigItem rootConfig("root", 0, rootConfigSet);

EEPROMConfig config(rootConfig);

// Declare some functions
void setWiFiCredentials(const char *ssid, const char *password);
void setWiFiAP(bool);
void infoCallback();
void broadcastUpdate(String originalKey, String& originalValue);
void broadcastUpdate(String originalKey, const BaseConfigItem& item);

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

void onLedTypeChanged(ConfigItem<byte> &item) {
	bambuLights->updatePixelCount();
}

std::function<void()> lightModeChanged = [](){};

void setLightModeChangeCallback(std::function<void()> callback) {
	lightModeChanged = callback;
}

void onLightModeChanged(ConfigItem<byte> &item) {
	lightModeChanged();
}

std::function<void()> lightStateChanged = [](){};

void setLightStateChangeCallback(std::function<void()> callback) {
	lightStateChanged = callback;
}

void onLightStateChanged(ConfigItem<boolean> &lightState) {
	lightStateChanged();
	if (BambuLights::getChamberSync()) {
		mqttBroker.setChamberLight(lightState);
	}
}

void onChamberSyncChanged(ConfigItem<boolean> &chamberSync) {
	if (chamberSync) {
		mqttBroker.setChamberLight(BambuLights::getLightState());
	}
}

void onNumLedsChanged(ConfigItem<byte> &item) {
	bambuLights->updatePixelCount();
}

template<class T>
void onMqttParamsChanged(ConfigItem<T> &item) {
	mqttBroker.init(ssid);
}

template<class T>
void onMqttHAParamsChanged(ConfigItem<T> &item) {
	mqttHABroker.init(ssid);
}

template<class T>
void onHostnameChanged(ConfigItem<T> &item) {
	config.commit();
	ESP.restart();
}

void ledTaskFn(void *pArg) {
	bambuLights->begin();
	BambuLights::State prevLightsState = BambuLights::noWiFi;
	long startIdleTimeMs = 0;
	bool inFinishedPhase = false;
	bool doorWasOpen = false;
	bool chamberLightWasOn = true;

	while (true) {
		mqttBroker.checkConnection();
		mqttHABroker.checkConnection();

		bambuLights->setBrightness(255);	// Brightness scale factor

		BambuLights::State lightsState = BambuLights::noWiFi;

		if (WiFi.isConnected()) {		
			// Run the state machine
			switch (mqttBroker.getState()) {
				case MQTTBroker::disconnected:
					lightsState = BambuLights::noPrinter;
					break;
				case MQTTBroker::idle:
					{
						lightsState = BambuLights::printer;

						if (prevLightsState == BambuLights::printing && !inFinishedPhase) {
							inFinishedPhase = true;
							startIdleTimeMs = millis();
							lightsState = BambuLights::finished;
						}

						if (inFinishedPhase) {
							lightsState = BambuLights::finished;

							// Switch to normal idle lights if timed out
							if (BambuLights::getIdleTimeout() > 0 && (millis() - startIdleTimeMs > BambuLights::getIdleTimeout() * 60000)) {
								lightsState = BambuLights::printer;
								inFinishedPhase = false;
							}

							// Switch to normal idle lights if door is opened after print complete
							if (!doorWasOpen && mqttBroker.isDoorOpen()) {
								lightsState = BambuLights::printer;
								inFinishedPhase = false;
							}
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

			doorWasOpen = mqttBroker.isDoorOpen();

			boolean chamberLightIsOn = mqttBroker.isLightOn();
            if (chamberLightIsOn != chamberLightWasOn) {
				chamberLightWasOn = chamberLightIsOn;
				if (BambuLights::getChamberSync() && (BambuLights::getLightState() != chamberLightIsOn)) {
					BambuLights::getLightState() = chamberLightIsOn;
					BambuLights::getLightState().notify();
					broadcastUpdate(BambuLights::getLightState().name, BambuLights::getLightState());
				}
			}

			// Override the results if told to
			if (!BambuLights::getLightState()) {
				lightsState = BambuLights::no_lights;
			} else {
				if (BambuLights::getLightMode() == 0) {
					lightsState = BambuLights::white;
				}
			}
		}

		bambuLights->setState(lightsState);

		bambuLights->loop();
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
	&WSMenuHandler::ledsMenu,
	&WSMenuHandler::mqttMenu,
	&WSMenuHandler::mqttHAMenu,
	&WSMenuHandler::infoMenu,
	0
};

WSMenuHandler wsMenuHandler(items);
WSConfigHandler wsMqttHandler(rootConfig, "mqtt");
WSConfigHandler wsMqttHAHandler(rootConfig, "mqtt_ha");
WSLEDConfigHandler wsLEDHandler(rootConfig, "leds");
WSInfoHandler wsInfoHandler(infoCallback);

// Order of this needs to match the numbers in WSMenuHandler.cpp
WSHandler* wsHandlers[] {
	&wsMenuHandler,
	&wsLEDHandler,
	&wsMqttHandler,
	&wsMqttHAHandler,
	&wsInfoHandler,
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

void broadcastUpdate(String originalKey, String& originalValue) {
	xSemaphoreTake(wsMutex, portMAX_DELAY);

	JsonDocument doc;
	JsonObject root = doc.to<JsonObject>();

	root["type"] = "sv.update";

	JsonVariant value = root.createNestedObject("value");
	value[originalKey] = serialized(originalValue.c_str());

	size_t len = measureJson(root);
	AsyncWebSocketMessageBuffer * buffer = ws.makeBuffer(len); //  creates a buffer (len + 1) for you.
	if (buffer) {
		serializeJson(root, (char *)buffer->get(), len + 1);
		ws.textAll(buffer);
	}

	xSemaphoreGive(wsMutex);
}

void broadcastUpdate(String originalKey, const BaseConfigItem& item) {
	String rawJSON = item.toJSON();
	broadcastUpdate(originalKey, rawJSON);
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
			setWiFiAP(value == TRUE_STRING ? true : false);
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

	bambuLights = new BambuLights(27);

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
		4096,  /* Stack size in words */
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

	hostName.setCallback(onHostnameChanged);
	BambuLights::getLedType().setCallback(onLedTypeChanged);
	BambuLights::getNumLEDs().setCallback(onNumLedsChanged);
	BambuLights::getLightMode().setCallback(onLightModeChanged);
	BambuLights::getLightState().setCallback(onLightStateChanged);
	BambuLights::getChamberSync().setCallback(onChamberSyncChanged);

	MQTTBroker::getHost().setCallback(onMqttParamsChanged);
	MQTTBroker::getPort().setCallback(onMqttParamsChanged);
	MQTTBroker::getUser().setCallback(onMqttParamsChanged);
	MQTTBroker::getPassword().setCallback(onMqttParamsChanged);
	MQTTBroker::getSerialNumber().setCallback(onMqttParamsChanged);
	mqttBroker.init(ssid);

	MQTTHABroker::getHost().setCallback(onMqttHAParamsChanged);
	MQTTHABroker::getPort().setCallback(onMqttHAParamsChanged);
	MQTTHABroker::getUser().setCallback(onMqttHAParamsChanged);
	MQTTHABroker::getPassword().setCallback(onMqttHAParamsChanged);
	mqttHABroker.init(ssid);

  Serial.print("setup() running on core ");
  Serial.println(xPortGetCoreID());

  vTaskDelete(NULL);	// Delete this task (so loop() won't be called)
}

void loop()
{
  // Should never be called
}
