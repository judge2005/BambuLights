#include <WSMenuHandler.h>

String WSMenuHandler::ledsMenu = "{\"1\": { \"url\" : \"leds.html\", \"title\" : \"LEDs\" }}";
String WSMenuHandler::mqttMenu = "{\"2\": { \"url\" : \"mqtt.html\", \"title\" : \"Printer\" }}";
String WSMenuHandler::mqttHAMenu = "{\"3\": { \"url\" : \"mqtt_ha.html\", \"title\" : \"Homeassistant\" }}";
String WSMenuHandler::infoMenu = "{\"4\": { \"url\" : \"info.html\", \"title\" : \"Info\" }}";

void WSMenuHandler::handle(AsyncWebSocketClient *client, char *data) {
	String json("{\"type\":\"sv.init.menu\", \"value\":[");
	char *sep = "";
	for (int i=0; items[i] != 0; i++) {
		json.concat(sep);json.concat(*items[i]);sep=",";
	}
	json.concat("]}");
	client->text(json);
}

void WSMenuHandler::setItems(String **items) {
	this->items = items;
}

