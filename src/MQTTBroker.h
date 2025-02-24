#ifndef ELEKSTUBE_MQTT_H
#define ELEKSTUBE_MQTT_H
#include <ConfigItem.h>
#include <espMqttClient.h>
#include <ArduinoJson.h>
#include <map>
#include <set>

class MQTTBroker
{
public:
    MQTTBroker();

    enum State { disconnected, idle, printing, no_lights, error, warning };

    static StringConfigItem& getHost() { static StringConfigItem mqtt_host("mqtt_host", 25, ""); return mqtt_host; }
    static IntConfigItem& getPort() { static IntConfigItem mqtt_port("mqtt_port", 8883); return mqtt_port; }
    static StringConfigItem& getUser() { static StringConfigItem mqtt_user("mqtt_user", 25, "bblp"); return mqtt_user; }
    static StringConfigItem& getPassword() { static StringConfigItem mqtt_password("mqtt_password", 25, ""); return mqtt_password; }
    static StringConfigItem& getSerialNumber() { static StringConfigItem mqtt_serialnumber("mqtt_serialnumber", 25, ""); return mqtt_serialnumber; }

    bool init(const String& id);
    void connect();
    void checkConnection();
    bool isConnected() { return connected; }
    bool isDoorOpen() { return doorOpen; }
    State getState() { return state; }
    void setChamberLight(bool on);

private:
    void onConnect(bool sessionPresent);
    void onDisconnect(espMqttClientTypes::DisconnectReason reason);
    void onMessage(const espMqttClientTypes::MessageProperties& properties, const char* topic, const uint8_t*  payload, size_t length, size_t index, size_t total_length);
    void onCompleteMessage(const espMqttClientTypes::MessageProperties& properties, const char* topic, const uint8_t* payload, size_t length);
    void handleMQTTMessage(JsonDocument &jsonMsg);

    String id;
    JsonDocument filter;
    char deviceTopic[64];
    char reportTopic[64];
    char requestTopic[64];

    bool reconnect = false;
    bool connected = false;
    State state = disconnected;
    bool doorOpen;
    int chamberLight = -1;

    uint32_t lastReconnect = 0;

    espMqttClientSecure client;

    static std::map<int, std::string> CURRENT_STAGE_IDS;
    static std::map<uint64_t, std::string> HMS_ERRORS;
    static std::map<int, std::string> HMS_SEVERITY_LEVELS;
    
    static std::set<int> ERROR_STAGES;
    static std::set<int> CAMERA_OFF_STAGES;
    static std::set<int> IDLE_STAGES;

    static std::set<int> PRINT_WARNINGS;
};
#endif