#ifndef WSMENUHANDLER_H_
#define WSMENUHANDLER_H_

#include <Arduino.h>
#include <WString.h>
#include <WSHandler.h>

class WSMenuHandler : public WSHandler {
public:
	WSMenuHandler(String **items) : items(items) { }
	virtual void handle(AsyncWebSocketClient *client, char *data);
	void setItems(String **items);

	static String ledsMenu;
	static String mqttMenu;
	static String infoMenu;

private:
	String **items;
};


#endif /* WSMENUHANDLER_H_ */
