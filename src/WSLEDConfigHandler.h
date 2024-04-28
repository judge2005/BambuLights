#ifndef WSLEDCONFIGHANDLER_H_
#define WSLEDCONFIGHANDLER_H_

#include <ConfigItem.h>
#include <WSHandler.h>

class WSLEDConfigHandler: public WSHandler {
public:
	typedef String (*CbFunc)();

	WSLEDConfigHandler(BaseConfigItem& rootConfig, const char *name) :
		cbFunc(NULL),
		rootConfig(rootConfig),
		name(name) {
	}

	WSLEDConfigHandler(BaseConfigItem& rootConfig, const char *name, CbFunc cbFunc) :
		cbFunc(cbFunc),
		rootConfig(rootConfig),
		name(name) {
	}

	virtual void handle(AsyncWebSocketClient *client, char *data);
	virtual void broadcast(AsyncWebSocket &ws, char *data);

private:
	CbFunc cbFunc;

	String getData(char *data);
	
	BaseConfigItem& rootConfig;
	const char *name;
};

#endif /* WSLEDCONFIGHANDLER_H_ */
