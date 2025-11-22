#ifndef MQTT_HA_BROKER_H
#define MQTT_HA_BROKER_H
#include <ConfigItem.h>
#ifdef ASYNC_MTTT_HA_CLIENT
#include <AsyncMqttClient.h>
#else
#include <espMqttClient.h>
#endif
#include <ArduinoJson.h>

class MQTTHABroker
{
public:
    MQTTHABroker();

    static StringConfigItem& getHost() { static StringConfigItem mqtt_host("mqtt_ha_host", 25, ""); return mqtt_host; }
    static IntConfigItem& getPort() { static IntConfigItem mqtt_port("mqtt_ha_port", 1883); return mqtt_port; }
    static StringConfigItem& getUser() { static StringConfigItem mqtt_user("mqtt_ha_user", 25, ""); return mqtt_user; }
    static StringConfigItem& getPassword() { static StringConfigItem mqtt_password("mqtt_ha_password", 25, ""); return mqtt_password; }

    bool init(const String& id);
    void connect();
    void checkConnection();

private:
    void onPrinterStateChanged(MQTTBroker* printerBroker);

    void onConnect(bool sessionPresent);
#ifdef ASYNC_MTTT_HA_CLIENT
    void onDisconnect(AsyncMqttClientDisconnectReason reason);
    void onMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t length, size_t index, size_t total_length);
    void onCompleteMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t length);
#else
    void onDisconnect(espMqttClientTypes::DisconnectReason reason);
    void onMessage(const espMqttClientTypes::MessageProperties& properties, const char* topic, const uint8_t*  payload, size_t length, size_t index, size_t total_length);
    void onCompleteMessage(const espMqttClientTypes::MessageProperties& properties, const char* topic, const uint8_t* payload, size_t length);
#endif
    void publishLightState();
    void publishEffectState();
    void sendHADiscoveryMessage();

    String id;
    JsonDocument filter;
    char persistentStateTopic[64];
    char availabilityTopic[64];
    char lightStateTopic[64];
    char lightCommandTopic[64];
    char chamberLightCommandTopic[64];
    char effectStateTopic[64];
    char effectCommandTopic[64];
    char printerStateTopic[64];
    static char* effectNames[];

    bool connected = false;
    bool reconnect = false;
    uint32_t lastReconnect = 0;

#ifdef ASYNC_MTTT_HA_CLIENT
    AsyncMqttClient client;
#else
    espMqttClient client;
#endif

    static std::map<MQTTBroker::State, std::string> PRINTER_STATES;
};
#endif