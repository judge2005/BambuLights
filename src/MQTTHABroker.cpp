#include <IPAddress.h>
#include <WiFi.h>
#include <AsyncWiFiManager.h>
#include <ArduinoJson.h>
#include <ConfigItem.h>
#include <BambuLights.h>
#include <MQTTBroker.h>

#include "MQTTHABroker.h"

extern AsyncWiFiManager wifiManager;
extern const char *manifest[];
extern void setLightModeChangeCallback(std::function<void()> callback);
extern void setChamberLightChangeCallback(std::function<void()> callback);
extern void broadcastUpdate(String originalKey, const BaseConfigItem& item);
extern CompositeConfigItem rootConfig;
extern MQTTBroker mqttBroker;

char *MQTTHABroker::effectNames[] = {
    "White",
    "Reactive",
    0
};

MQTTHABroker::MQTTHABroker() {
}

void MQTTHABroker::onConnect(bool sessionPresent)
{
    connected = true;
	reconnect = false;
    sendHADiscoveryMessage();
}

#ifdef ASYNC_MTTT_HA_CLIENT
void MQTTHABroker::onDisconnect(AsyncMqttClientDisconnectReason reason)
#else
void MQTTHABroker::onDisconnect(espMqttClientTypes::DisconnectReason reason)
#endif
{
	Serial.printf("Disconnected from HA: %u\n", static_cast<uint8_t>(reason));

    connected = false;
    reconnect = true;
    lastReconnect = millis();
}

#ifdef ASYNC_MTTT_HA_CLIENT
void MQTTHABroker::onMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t length, size_t index, size_t total_length)
#else
void MQTTHABroker::onMessage(const espMqttClientTypes::MessageProperties& properties, const char* topic, const uint8_t* payload, size_t length, size_t index, size_t total_length)
#endif
{
	static uint8_t mqttMessageBuffer[256];

		// payload is bigger then max: return chunked
	if (total_length >= sizeof(mqttMessageBuffer)) {
		DEBUG("MQTT_HA message too large");
		return;
	}

	// add data and dispatch when done
	memcpy(&mqttMessageBuffer[index], payload, length);
	if (index + length == total_length) {
		// message is complete here
		mqttMessageBuffer[total_length] = 0;
        onCompleteMessage(properties, topic, mqttMessageBuffer, total_length);
	}
}

#ifdef ASYNC_MTTT_HA_CLIENT
void MQTTHABroker::onCompleteMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t length)
#else
void MQTTHABroker::onCompleteMessage(const espMqttClientTypes::MessageProperties& properties, const char* topic, const uint8_t* payload, size_t length)
#endif
{
    Serial.printf("Received message %s on topic %s\n", (char*)payload, topic);

    if (strcmp(topic, lightCommandTopic) == 0) {
        if (strcmp((const char *)payload, "OFF") == 0) {
            if (mqttBroker.isLightOn()) {
                mqttBroker.setChamberLight(false);
            } else {
                publishLightState();
            }
        } else if (strcmp((const char *)payload, "ON") == 0){
            if (!mqttBroker.isLightOn()) {
                mqttBroker.setChamberLight(true);
            } else {
                publishLightState();
            }
        } else {
            Serial.printf("Unknown chamber light %s\n", (const char *)payload);
        }
    } else if (strcmp(topic, effectCommandTopic) == 0) {
        bool found = false;
        for (int i=0; effectNames[i] != 0; i++) {
            if (strcmp(effectNames[i], (const char *)payload) == 0) {
                BambuLights::getLightMode() = i;
                found = true;
                break;
            }
        }
        if (found) {
            broadcastUpdate(BambuLights::getLightMode().name, BambuLights::getLightMode());
        } else {
            Serial.printf("Unknown light effect %s\n", (const char*)payload);
        }
        publishEffectState();
    } else {
        Serial.print("Unknown topic: ");
        Serial.print(topic);
        Serial.print(", value: ");
        Serial.println((const char*)payload);
    }
}

bool MQTTHABroker::init(const String& id) {
    this->id = id;

    client.disconnect();

    IPAddress ipAddress;
    if (ipAddress.fromString(getHost().value.c_str())) {
        sprintf(lightStateTopic,   "bambu_lights/%s/bambu_lights/light/state", id.c_str());
        sprintf(lightCommandTopic, "bambu_lights/%s/bambu_lights/light/set", id.c_str());
        sprintf(effectStateTopic,  "bambu_lights/%s/bambu_lights/effect/state", id.c_str());
        sprintf(effectCommandTopic,"bambu_lights/%s/bambu_lights/effect/set", id.c_str());
        

        client.setServer(getHost().value.c_str(), getPort());
        client.setCredentials(getUser().value.c_str(),getPassword().value.c_str());
        client.setClientId(id.c_str());
        client.onConnect([this](bool sessionPresent) { this->onConnect(sessionPresent); });
#ifdef ASYNC_MTTT_HA_CLIENT
        client.onDisconnect([this](AsyncMqttClientDisconnectReason reason) { this->onDisconnect(reason); });
        client.onMessage([this](char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t length, size_t index, size_t total_length) {
            this->onMessage(topic, payload, properties, length, index, total_length);
        });
#else
        client.onDisconnect([this](espMqttClientTypes::DisconnectReason reason) { this->onDisconnect(reason); });
        client.onMessage([this](const espMqttClientTypes::MessageProperties& properties, const char* topic, const uint8_t* payload, size_t length, size_t index, size_t total_length) {
            this->onMessage(properties, topic, payload, length, index, total_length);
        });
#endif
        setLightModeChangeCallback([this]() { this->publishEffectState(); });
        setChamberLightChangeCallback([this]() { this->publishLightState(); });
        
        reconnect = true;
    }

    return reconnect;
}

void MQTTHABroker::connect() {
    if (WiFi.isConnected() && !wifiManager.isAP()) {
        Serial.println("Connecting to MQTTHA...");
        client.connect();
    }
}

void MQTTHABroker::checkConnection() {
    if (reconnect && (millis() - lastReconnect >= 2000)) {
        lastReconnect = millis();
        connect();
    }
}

void MQTTHABroker::publishLightState() {
    if (client.connected()) {
        JsonDocument state;
        state["light"] = mqttBroker.isLightOn() ? "ON" : "OFF";
        char buffer[256];
        serializeJson(state, buffer);

        Serial.print(lightStateTopic);
        Serial.print(":");
        Serial.println(buffer);

        uint16_t result = client.publish(lightStateTopic, 0, true, buffer);
        Serial.printf("State send result %d\n", result);
    }
}

void MQTTHABroker::publishEffectState() {
    if (client.connected()) {
        JsonDocument state;
        state["effect"] = effectNames[BambuLights::getLightMode()];
        char buffer[256];
        serializeJson(state, buffer);

        Serial.print(effectStateTopic);
        Serial.print(":");
        Serial.println(buffer);

        uint16_t result = client.publish(effectStateTopic, 0, true, buffer);

        Serial.printf("State send result %d\n", result);
    }
}

void MQTTHABroker::sendHADiscoveryMessage() {
    client.subscribe(lightCommandTopic, 0);
    client.subscribe(effectCommandTopic, 0);

    // This is the discovery topic for the 'light_mode' switch
    char discoveryTopic[128];
    sprintf(discoveryTopic, "homeassistant/light/%s/bambu_lights/config", id.c_str());

    JsonDocument doc;
    char buffer[1024];

    doc["name"] = "Bambu Lights";
    doc["icon"] = "mdi:led-strip-variant";
    doc["unique_id"] = "bambu_lights_" + id;
    doc["state_topic"] = lightStateTopic;
    doc["command_topic"] = lightCommandTopic;
    doc["state_value_template"] = "{{value_json.light}}";
    doc["effect"] = true;
    doc["effect_state_topic"] = effectStateTopic;
    doc["effect_command_topic"] = effectCommandTopic;
    doc["effect_value_template"] = "{{value_json.effect}}";
    JsonArray myArray = doc["effect_list"].to<JsonArray>();
    for (int i=0; effectNames[i] != 0; i++) {
        myArray.add(effectNames[i]);
    }
    doc["device"]["configuration_url"] = "http://" + WiFi.localIP().toString() + "/";
    doc["device"]["name"] = manifest[3];
    doc["device"]["identifiers"][0] = WiFi.macAddress();
    doc["device"]["model"] = manifest[0];
    doc["device"]["sw_version"] = manifest[1];

    size_t n = serializeJson(doc, buffer);

    client.publish(discoveryTopic, 0, true, buffer);

    Serial.print(discoveryTopic);
    Serial.print(":");
    Serial.println(buffer);

    publishLightState();
    publishEffectState();
}
