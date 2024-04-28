#ifndef BAMBULIGHTS_H
#define BAMBULIGHTS_H

#include <stdint.h>
#include <ConfigItem.h>
#include <NeoPixelBus.h>

class BambuLights {
public:
  BambuLights(int numPixels, int pin);

  enum Patterns { constant, pulse, num_patterns };
  enum State { noWiFi, noPrinter, printer, printing, no_lights, error, warning, finished };

  const static String patterns_str[num_patterns];

  static CompositeConfigItem& getAllConfig();
  static CompositeConfigItem& getNoWiFiConfig();
  static CompositeConfigItem& getNoPrinterConnectedConfig();
  static CompositeConfigItem& getPrinterConnectedConfig();
  static CompositeConfigItem& getPrintingConfig();
  static CompositeConfigItem& getWarningConfig();
  static CompositeConfigItem& getErrorConfig();
  static CompositeConfigItem& getFinishedConfig();
  static ByteConfigItem& getIdleTimeout() { static ByteConfigItem timeout("timeout", 5); return timeout; }

  void begin();
  void loop();

  void setState(State state);
  void togglePower() { off = !off; }
  void PowerOn()  { off = false; }
  void PowerOff()  { off = true; }
  void setOn(bool on) { off = !on; }
  void setBrightness(byte brightness) { this->brightness = brightness; }

private:
  bool off;
  byte brightness = 255;
  
  NeoPixelBus <NeoGrbFeature, Neo800KbpsMethod> pixels;
  NeoGamma<NeoGammaTableMethod> colorGamma;

  State currentState;
  ByteConfigItem *currentPattern;
  IntConfigItem *currentHue;
  ByteConfigItem *currentValue;
  ByteConfigItem *currentSaturation;
  ByteConfigItem *currentPulsePerMin;

  void setCurrentConfig(CompositeConfigItem& config);

  // Pattern methods
  void pulsePattern();

  void fill(uint8_t hue, uint8_t val, uint8_t sat);
  void show();
  void clear();
  void setPixelColor(uint8_t digit, uint8_t hue, uint8_t val, uint8_t sat);
};

#endif // BAMBULIGHTS_H
