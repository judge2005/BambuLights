#include <WSLEDConfigHandler.h>

void WSLEDConfigHandler::handle(AsyncWebSocketClient *client, char *data) {
	client->text(getData(data));
}

void WSLEDConfigHandler::broadcast(AsyncWebSocket &ws, char *data) {
	ws.textAll(getData(data));
}

String WSLEDConfigHandler::getData(char *data) {
	String json("{\"type\":\"sv.init.");
	json.reserve(200);
	json.concat(name);
	json.concat("\", \"value\":{");
    BaseConfigItem *deviceConfig = rootConfig.get(name);
    char sep = ' ';

    if (deviceConfig != 0) {
		deviceConfig->forEach([&json, &sep](BaseConfigItem& item) {
			const char* name = item.name;
			Serial.println(name);
			item.forEach([&json, &sep, name](BaseConfigItem& item) {
				json.concat(sep);
				json.concat("\"");
				json.concat(name);
				json.concat("-");
				json.concat(item.name);
				json.concat("\":");
				json.concat(item.toJSON());
				sep = ',';
			});
        }, false);
    }


	if (cbFunc != NULL) {
		json.concat(sep);
		json.concat(cbFunc());
	}

	json.concat("}}");

	return json;
}

