#include <IPAddress.h>
#include <WiFi.h>
#include <AsyncWiFiManager.h>
#include <ArduinoJson.h>
#include <ConfigItem.h>

#include "MQTTBroker.h"

extern AsyncWiFiManager wifiManager;

std::map<int, std::string> MQTTBroker::CURRENT_STAGE_IDS = {
    {0, "printing"},
    {1, "auto_bed_leveling"},
    {2, "heatbed_preheating"},
    {3, "sweeping_xy_mech_mode"},
    {4, "changing_filament"},
    {5, "m400_pause"},
    {6, "paused_filament_runout"},
    {7, "heating_hotend"},
    {8, "calibrating_extrusion"},
    {9, "scanning_bed_surface"},
    {10, "inspecting_first_layer"},
    {11, "identifying_build_plate_type"},
    {12, "calibrating_micro_lidar"},
    {13, "homing_toolhead"},
    {14, "cleaning_nozzle_tip"},
    {15, "checking_extruder_temperature"},
    {16, "paused_user"},
    {17, "paused_front_cover_falling"},
    {18, "calibrating_micro_lidar"},
    {19, "calibrating_extrusion_flow"},
    {20, "paused_nozzle_temperature_malfunction"},
    {21, "paused_heat_bed_temperature_malfunction"},
    {22, "filament_unloading"},
    {23, "paused_skipped_step"},
    {24, "filament_loading"},
    {25, "calibrating_motor_noise"},
    {26, "paused_ams_lost"},
    {27, "paused_low_fan_speed_heat_break"},
    {28, "paused_chamber_temperature_control_error"},
    {29, "cooling_chamber"},
    {30, "paused_user_gcode"},
    {31, "motor_noise_showoff"},
    {32, "paused_nozzle_filament_covered_detected"},
    {33, "paused_cutter_error"},
    {34, "paused_first_layer_error"},
    {35, "paused_nozzle_clog"},
    {-1, "idle"}, 
    {255, "idle"}
};

// What about spaghetti detected? Do we need to also check HMS?
std::set<int> MQTTBroker::ERROR_STAGES = {
    6, 17, 20, 21, 26, 27, 28, 32, 33, 34, 35
};

std::set<int> MQTTBroker::CAMERA_OFF_STAGES = {
    8, 9, 10, 12, 18, 19
};

std::set<int> MQTTBroker::IDLE_STAGES = {
    -1, 255
};

/*
        # Example payload:
        # "hms": [
        #     {
        #         "attr": 50331904, # In hex this is 0300 0100
        #         "code": 65543     # In hex this is 0001 0007
        #     }
        # ],
        # So this is HMS_0300_0100_0001_0007:
*/
std::map<uint64_t, std::string> MQTTBroker::HMS_ERRORS = {
    {0x0300100000020001, "The 1st order mechanical resonance mode of X axis is low."},
    {0x0300100000020002, "The 1st order mechanical resonance mode of X axis differ much..."},
    {0x03000F0000010001, "The accelerometer data is unavailable"},
    {0x03000D000001000B, "The Z axis motor seems got stuck when moving up"},
    {0x03000D0000010002, "Hotbed homing failed. The environmental vibration is too great"},
    {0x03000D0000010003, "The build plate is not placed properly ..."},
    {0x03000D0000020001, "Heatbed homing abnormal. There may be a bulge on the ..."},
    {0x03000A0000010005, "the static voltage of force sensor 1/2/3 is not 0 ..."},
    {0x03000A0000010004, "External disturbance was detected when testing the force sensor"},
    {0x03000A0000010003, "The sensitivity of heatbed force sensor 1/2/3 is too low...."},
    {0x03000A0000010002, "The sensitivity of heatbed force sensor 1/2/3 is low..."},
    {0x03000A0000010001, "The sensitivity of heatbed force sensor 1/2/3 is too high..."},
    {0x0300040000020001, "The speed of part cooling fan if too slow or stopped ..."},
    {0x0300030000020002, "The speed of hotend fan is slow ..."},
    {0x0300030000010001, "The speed of the hotend fan is too slow or stopped..."},
    {0x0300060000010001, "Motor-A has an open-circuit. There may be a loose connection, or the motor may have failed."},
    {0x0300060000010002, "Motor-A has a short-circuit. It may have failed."},
    {0x0300060000010003, "The resistance of Motor-A is abnormal, the motor may have failed."},
    {0x0300010000010001, "The heatbed temperature is abnormal, the heater may have a short circuit."},
    {0x0300010000010002, "The heatbed temperature is abnormal, the heater may have an open circuit, or the thermal switch may be open."},
    {0x0300010000010003, "The heatbed temperature is abnormal, the heater is over temperature."},
    {0x0300010000010006, "The heatbed temperature is abnormal, the sensor may have a short circuit."},
    {0x0300010000010007, "The heatbed temperature is abnormal, the sensor may have an open circuit."},
    {0x0300130000010001, "The current sensor of Motor-A is abnormal. This may be caused by a failure of the hardware sampling circuit."},
    {0x0300400000020001, "Data transmission over the serial port is abnormal, the software system may be faulty."},
    {0x0300410000010001, "The system voltage is unstable, triggering the power failure protection function."},
    {0x0300020000010001, "The nozzle temperature is abnormal, the heater may be short circuit."},
    {0x0300020000010002, "The nozzle temperature is abnormal, the heater may be open circuit."},
    {0x0300020000010003, "The nozzle temperature is abnormal, the heater is over temperature."},
    {0x0300020000010006, "The nozzle temperature is abnormal, the sensor may be short circuit."},
    {0x0300020000010007, "The nozzle temperature is abnormal, the sensor may be open circuit."},
    {0x0300120000020001, "The front cover of the toolhead fell off."},
    {0x0C00010000010001, "The Micro Lidar camera is offline."},
    {0x0700010000010001, "AMS1 assist motor has slipped. The extrusion wheel may be worn down, or the filament may be too thin."},
    {0x0700010000010003, "AMS1 assist motor torque control is malfunctioning. The current sensor may be faulty."},
    {0x0700010000010004, "AMS1 assist motor speed control is malfunctioning. The speed sensor may be faulty."},
    {0x0700010000020002, "AMS1 assist motor is overloaded. The filament may be tangled or stuck."},
    {0x0700020000010001, "AMS1 filament speed and length error. The filament odometry may be faulty."},
    {0x0700100000010001, "AMS1 slot 1 motor has slipped. The extrusion wheel may be malfunctioning, or the filament may be too thin."},
    {0x0700100000010003, "AMS1 slot 1 motor torque control is malfunctioning. The current sensor may be faulty."},
    {0x0700100000020002, "AMS1 slot 1 motor is overloaded. The filament may be tangled or stuck."},
    {0x0700200000020001, "AMS1 slot 1 filament has run out."},
    {0x0700200000020002, "AMS1 slot 1 is empty."},
    {0x0700200000020003, "AMS1 slot 1 filament may be broken in AMS."},
    {0x0700200000020004, "AMS1 slot 1 filament may be broken in the tool head."},
    {0x0700200000020005, "AMS1 slot 1 filament has run out, and purging the old filament went abnormally, please check whether the filament is stuck in the tool head."},
    {0x0700200000030001, "AMS1 slot 1 filament has run out. Please wait while old filament is purged."},
    {0x0700200000030002, "AMS1 slot 1 filament has run out and automatically switched to the slot with the same filament."},
    {0x0700600000020001, "AMS1 slot 1 is overloaded. The filament may be tangled or the spool may be stuck."},
    {0x0C00010000020002, "The Micro Lidar camera is malfunctioning."},
    {0x0C00010000010003, "Synchronization between Micro Lidar camera and MCU is abnormal."},
    {0x0C00010000010004, "The Micro Lidar camera lens seems to be dirty."},
    {0x0C00010000010005, "Micro Lidar OTP parameter is abnormal."},
    {0x0C00010000020006, "Micro Lidar extrinsic parameter abnormal."},
    {0x0C00010000020007, "Micro Lidar laser parameters are drifted."},
    {0x0C00010000020008, "Failed to get image from chamber camera."},
    {0x0C00010000010009, "Chamber camera dirty."},
    {0x0C0001000001000A, "The Micro Lidar LED may be broken."},
    {0x0C0001000001000B, "Failed to calibrate Micro Lidar."},
    {0x0C00020000010001, "The horizontal laser is not lit."},
    {0x0C00020000020002, "The horizontal laser is too thick."},
    {0x0C00020000020003, "The horizontal laser is not bright enough."},
    {0x0C00020000020004, "Nozzle height seems too low."},
    {0x0C00020000010005, "A new Micro Lidar is detected."},
    {0x0C00020000020006, "Nozzle height seems too high."},
    {0x0C00030000020001, "Filament exposure metering failed."},
    {0x0C00030000020002, "First layer inspection terminated due to abnormal lidar data."},
    {0x0C00030000020004, "First layer inspection not supported for current print."},
    {0x0C00030000020005, "First layer inspection timeout."},
    {0x0C00030000030006, "Purged filaments may have piled up."},
    {0x0C00030000030007, "Possible first layer defects."},
    {0x0C00030000030008, "Possible spaghetti defects were detected."},
    {0x0C00030000010009, "The first layer inspection module rebooted abnormally."},
    {0x0C0003000003000B, "Inspecting first layer."},
    {0x0C0003000002000C, "The build plate localization marker is not detected."},
    {0x0500010000020001, "The media pipeline is malfunctioning."},
    {0x0500010000020002, "USB camera is not connected."},
    {0x0500010000020003, "USB camera is malfunctioning."},
    {0x0500010000030004, "Not enough space in SD Card."},
    {0x0500010000030005, "Error in SD Card."},
    {0x0500010000030006, "Unformatted SD Card."},
    {0x0500020000020001, "Failed to connect internet, please check the network connection."},
    {0x0500020000020002, "Failed to login device."},
    {0x0500020000020004, "Unauthorized user."},
    {0x0500020000020006, "Liveview service is malfunctioning."},
    {0x0500030000010001, "The MC module is malfunctioning. Please restart the device."},
    {0x0500030000010002, "The toolhead is malfunctioning. Please restart the device."},
    {0x0500030000010003, "The AMS module is malfunctioning. Please restart the device."},
    {0x050003000001000A, "System state is abnormal. Please restore factory settings."},
    {0x050003000001000B, "The screen is malfunctioning."},
    {0x050003000002000C, "Wireless hardware error. Please turn off/on WiFi or restart the device."},
    {0x0500040000010001, "Failed to download print job. Please check your network connection."},
    {0x0500040000010002, "Failed to report print state. Please check your network connection."},
    {0x0500040000010003, "The content of print file is unreadable. Please resend the print job."},
    {0x0500040000010004, "The print file is unauthorized."},
    {0x0500040000010006, "Failed to resume previous print."},
    {0x0500040000020007, "The bed temperature exceeds the filament's vitrification temperature, which may cause a nozzle clog."},
    {0x0700400000020001, "The filament buffer signal lost, the cable or position sensor may be malfunctioning."},
    {0x0700400000020002, "The filament buffer position signal error, the position sensor may be malfunctioning."},
    {0x0700400000020003, "The AMS Hub communication is abnormal, the cable may be not well connected."},
    {0x0700400000020004, "The filament buffer signal is abnormal, the spring may be stuck."},
    {0x0700450000020001, "The filament cutter sensor is malfunctioning. The sensor may be disconnected or damaged."},
    {0x0700450000020002, "The filament cutter's cutting distance is too large. The XY motor may lose steps."},
    {0x0700450000020003, "The filament cutter handle has not released. The handle or blade may be stuck."},
    {0x0700510000030001, "AMS is disabled, please load filament from spool holder."},
    {0x07FF200000020001, "External filament has run out, please load a new filament."},
    {0x07FF200000020002, "External filament is missing, please load a new filament."},
    {0x07FF200000020004, "Please pull out the filament on the spool holder from the extruder."}
};

/*
def get_HMS_severity(code: int) -> str:
    uint_code = code >> 16
    if code > 0 and uint_code in HMS_SEVERITY_LEVELS:
        return HMS_SEVERITY_LEVELS[uint_code]
    return HMS_SEVERITY_LEVELS["default"]
*/
std::map<int, std::string> MQTTBroker::HMS_SEVERITY_LEVELS = {
    {1, "fatal"},
    {2, "serious"},
    {3, "common"},
    {4, "info"}
};

MQTTBroker::MQTTBroker() : client(espMqttClientTypes::UseInternalTask::YES) {
	filter["print"]["stg_cur"] = true;
	filter["print"]["hms"] = true;
}

void MQTTBroker::onConnect(bool sessionPresent)
{
    connected = true;
    state = idle;
	reconnect = false;
	Serial.println("Connected to Printer");
	Serial.print("Session present: ");
	Serial.println(sessionPresent);
	uint16_t packetIdSub = client.subscribe(reportTopic, 0);
	Serial.print("Subscribing at QoS 0, packetId: ");
	Serial.println(packetIdSub);
}

void MQTTBroker::onDisconnect(espMqttClientTypes::DisconnectReason reason)
{
	Serial.printf("Disconnected from Printer: %u\n", static_cast<uint8_t>(reason));

    connected = false;
    state = disconnected;
    reconnect = true;
    lastReconnect = millis();
}

void MQTTBroker::handleMQTTMessage(JsonDocument &jsonMsg) {
    serializeJson(jsonMsg, Serial);
    Serial.println("");

    JsonVariant printValues = jsonMsg["print"];
    if (printValues) {
        if (printValues.containsKey("stg_cur")) {
            int stage = printValues["stg_cur"];
            if (ERROR_STAGES.count(stage) > 0) {
                state = error;
            } else if (CAMERA_OFF_STAGES.count(stage) > 0) {
                state = no_lights;
            } else if (IDLE_STAGES.count(stage) > 0) {
                state = idle;
            } else {
                state = printing;
            }
        }

        JsonVariant hms = printValues["hms"];
        if (hms) {
            JsonArray hmsArray = hms.as<JsonArray>();
            if (hmsArray.size() > 0) {
                state = error;
                for (int i=0; i<hmsArray.size(); i++) {
                    uint64_t attr = hmsArray[i]["attr"].as<uint64_t>(); 
                    uint64_t code = hmsArray[i]["code"].as<uint64_t>();
                    int level = code >> 16;
                    uint64_t value = (attr << 32) | code;
                    Serial.print("HMS value=");Serial.println(HMS_ERRORS[value].c_str());
                    if (value == 0x0C0003000003000B) {
                        state = no_lights;
                        break;
                    }

                    if (level >= 3) {
                        state = warning;
                    } else {
                        state = error;
                    }
                }
            }
        }
    }

    Serial.print("state=");Serial.println(state);
}

void MQTTBroker::onCompleteMessage(const espMqttClientTypes::MessageProperties& properties, const char* topic, const uint8_t* payload, size_t length) {
	JsonDocument jsonMsg;
	DeserializationError deserializeError = deserializeJson(jsonMsg, payload, length, DeserializationOption::Filter(filter));
	if (!deserializeError) {
		if (jsonMsg.containsKey("print")) {
			handleMQTTMessage(jsonMsg);
		} else {
			serializeJson(jsonMsg, Serial);
			Serial.println("");
		}
	} else {
		Serial.print(F("Deserialize error while parsing mqtt message: "));
		Serial.println(deserializeError.c_str());
	}
}


void MQTTBroker::onMessage(const espMqttClientTypes::MessageProperties& properties, const char* topic, const uint8_t* payload, size_t length, size_t index, size_t total_length)
{
	static uint8_t mqttMessageBuffer[16384];

		// payload is bigger then max: return chunked
	if (total_length >= sizeof(mqttMessageBuffer)) {
		DEBUG("MQTT message too large");
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

bool MQTTBroker::init(const String& id) {
    this->id = id;

    client.disconnect();

    IPAddress ipAddress;
    if (ipAddress.fromString(getHost().value.c_str())) {
        Serial.println("Initializing printer connection properties");
        sprintf(deviceTopic, "device/%s", getSerialNumber().value.c_str());
        sprintf(reportTopic, "%s/report", deviceTopic);
        sprintf(requestTopic, "%s/request", deviceTopic);

        client.setServer(getHost().value.c_str(), getPort());
        client.setCredentials(getUser().value.c_str(),getPassword().value.c_str());
        client.setClientId(id.c_str());
        client.onConnect([this](bool sessionPresent) { this->onConnect(sessionPresent); });
        client.onDisconnect([this](espMqttClientTypes::DisconnectReason reason) { this->onDisconnect(reason); });
        client.onMessage([this](const espMqttClientTypes::MessageProperties& properties, const char* topic, const uint8_t* payload, size_t length, size_t index, size_t total_length) {
            this->onMessage(properties, topic, payload, length, index, total_length);
        });
        client.setInsecure();
        client.setCleanSession(true);
        
        reconnect = true;
    }

    return reconnect;
}

void MQTTBroker::connect() {
    if (WiFi.isConnected() && !wifiManager.isAP()) {
        Serial.println("Connecting to Printer...");
        client.connect();
    }
}

void MQTTBroker::checkConnection() {
    if (reconnect && (millis() - lastReconnect >= 2000)) {
        lastReconnect = millis();
        connect();
    }
}
