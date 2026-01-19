#!/usr/bin/env node

/*
 * A test server
 */
'use strict';

var expressStaticGzip = require("express-static-gzip");
var express = require('express');
var http = require('http');
var ws = require('ws');
var multiparty = require('multiparty');

var app = new express();

var server = http.createServer(app);

var wss = new ws.Server({ server });

var wssConn;

app.use(function(req, res, next) {
    console.log(req.originalUrl);
    // Add CORS headers
    res.header('Access-Control-Allow-Origin', '*');
    res.header('Access-Control-Allow-Methods', 'GET, HEAD, OPTIONS');
    res.header('Access-Control-Allow-Headers', 'Content-Type');
    next();
});

app.use(expressStaticGzip("src"));

// Serve files from current directory (web/)
app.use(express.static("."));

// Serve releases directory for manifest files
app.use("/releases", express.static("../releases"));

var pages = {
		"type":"sv.init.menu",
		"value": [
			{"1": { "url" : "mqtt.html", "title" : "Printer" }},
			{"2": { "url" : "leds.html", "title" : "LEDs" }},
			{"3": { "url" : "mqtt_ha.html", "title" : "Homeassistant" }},
			{"4": { "url" : "info.html", "title" : "Info" }}
		]
	}


var sendValues = function(conn, screen) {
}

var sendPages = function(conn) {
	var json = JSON.stringify(pages);
	conn.send(json);
	console.log(json);
}

var sendLEDValues = function(conn) {
	var json = '{"type":"sv.init.leds","value":';
	json += JSON.stringify(state[2]);
	json += '}';
	console.log(json);
	conn.send(json);
}

var sendMQTTValues = function(conn) {
	var json = '{"type":"sv.init.mqtt","value":';
	json += JSON.stringify(state[1]);
	json += '}';
	console.log(json);
	conn.send(json);
}

var sendMQTTHAValues = function(conn) {
	var json = '{"type":"sv.init.mqtt_ha","value":';
	json += JSON.stringify(state[3]);
	json += '}';
	console.log(json);
	conn.send(json);
}

var sendInfoValues = function(conn) {
	var json = '{"type":"sv.init.info","value":';
	json += JSON.stringify(state[4]);
	json += '}';
	console.log(json);
	conn.send(json);
}

var state = {
	"1": {
		'mqtt_host' : "192.168.10.10",
		'mqtt_port' : 1883,
		'mqtt_user' : "bblp",
		'mqtt_password' : "secret",
		'mqtt_serialnumber' : "abcdefg"
	},
	"2": {
		'light_mode' : 1,
		'led_type' : 1,
		'chamber_light' : true,
		'chamber_sync' : true,
		'num_leds' : 37,
		'noWiFi-colors' : false,
		'noWiFi-pattern': 1,
		'noWiFi-pulse_per_min': 7,
		'noWiFi-hue': 213,
		'noWiFi-saturation': 200,
		'noWiFi-value': 210,
		'noPrinterConnected-pattern': 0,
		'noPrinterConnected-pulse_per_min': 7,
		'noPrinterConnected-hue': 213,
		'noPrinterConnected-saturation': 255,
		'noPrinterConnected-value': 255,
		'printerConnected-pattern': 0,
		'printerConnected-pulse_per_min': 7,
		'printerConnected-hue': 0,
		'printerConnected-saturation': 0,
		'printerConnected-value': 128,
		'printing-pattern': 0,
		'printing-pulse_per_min': 7,
		'printing-hue': 0,
		'printing-saturation': 0,
		'printing-value': 255,
		'finished-timeout': 5,
		'finished-pattern': 0,
		'finished-pulse_per_min': 7,
		'finished-hue': 63,
		'finished-saturation': 255,
		'finished-value': 255,
		'error-pattern': 1,
		'error-pulse_per_min': 7,
		'error-hue': 0,
		'error-saturation': 255,
		'error-value': 255,
		'warning-pattern': 1,
		'warning-pulse_per_min': 7,
		'warning-hue': 128,
		'warning-saturation': 255,
		'warning-value': 255,
		'set_icon_leds': 'Bar'
	},
	"3": {
		'mqtt_ha_host' : "192.168.10.20",
		'mqtt_ha_port' : 1234,
		'mqtt_ha_user' : "mosquitto",
		'mqtt_ha_password' : "secret2",
	},
	"4": {
		'esp_boot_version' : "1234",
		'esp_free_heap' : "5678",
		'esp_sketch_size' : "90123",
		'esp_sketch_space' : "4567",
		'esp_flash_size' : "8901",
		'esp_chip_id' : "chip id",
		'wifi_ip_address' : "192.168.1.1",
		'wifi_mac_address' : "0E:12:34:56:78",
		'wifi_ssid' : "STC-Wonderful"
	}
}

var broadcastUpdate = function(conn, field, value) {
	var json = '{"type":"sv.update","value":{' + '"' + field + '":' + JSON.stringify(value) + '}}';
	console.log(json);
	try {
		conn.send(json);
	} catch (e) {
		console.log(e);
	}
}

var updateValue = function(conn, screen, pair) {
	console.log(screen);
	console.log(pair);
	var index = pair.indexOf(':');

	var key = pair.substring(0, index);
	var value = pair.substring(index+1);
	try {
		value = JSON.parse(value);
	} catch (e) {

	}

	state[screen][key] = value;

	broadcastUpdate(conn, key, state[screen][key]);
}

var updateHue = function(conn) {
	// var hue = state['2']['led_hue'];
	// hue = (hue + 5) % 256;
	// updateValue(conn, 2, "led_hue:" + hue);

	// var intensity = state['2']['led_value'];
	// intensity = (intensity + 2) % 256;
	// updateValue(conn, 2, "led_value:" + intensity);
	// var val = state['1']['time_or_date'];
	// val = (val + 1) % 3;
	// updateValue(conn, 1, "time_or_date:" + val)
}

wss.on('connection', function(conn) {
	wssConn = conn;

    console.log('connected');
	var hueTimer = setInterval(updateHue, 500, conn);

    //connection is up, let's add a simple simple event
	conn.on('message', function(data, isBinary) {

        //log the received message and send it back to the client
        console.log('received: %s', data);
        var message = isBinary ? data : data.toString();
    	var code = parseInt(message.substring(0, message.indexOf(':')));

    	switch (code) {
    	case 0:
    		sendPages(conn);
    		break;
    	case 1:
    		sendMQTTValues(conn);
    		break;
    	case 2:
    		sendLEDValues(conn);
    		break;
    	case 3:
    		sendMQTTHAValues(conn);
    		break;
    	case 4:
    		sendInfoValues(conn);
    		break;
    	case 9:
    		message = message.substring(message.indexOf(':')+1);
    		var screen = message.substring(0, message.indexOf(':'));
    		var pair = message.substring(message.indexOf(':')+1);
    		updateValue(conn, screen, pair);
    		break;
    	}
    });

	conn.on('close', function() {
		clearInterval(hueTimer);
	});
});

//start our server
server.listen(process.env.PORT || 8080, function() {
    console.log('Server started on port' + server.address().port + ':)');
});

