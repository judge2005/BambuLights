#include "BambuLights.h"
#include <math.h>

CompositeConfigItem& BambuLights::getNoWiFiConfig() {
    static ByteConfigItem pattern("pattern", pulse);
    static IntConfigItem  hue("hue", 203);
    static ByteConfigItem value("value", 255);
    static ByteConfigItem saturation("saturation", 255);
    static ByteConfigItem pulse_per_min("pulse_per_min", 10);

    static BaseConfigItem* configSet[] {
        // Clock
        &pattern,
        &hue,
        &value,
        &saturation,
        &pulse_per_min,
        0
    };

    static CompositeConfigItem config("noWiFi", 0, configSet);

    return config;
}

CompositeConfigItem& BambuLights::getNoPrinterConnectedConfig() {
    static ByteConfigItem pattern("pattern", constant);
    static IntConfigItem  hue("hue", 203);
    static ByteConfigItem value("value", 255);
    static ByteConfigItem saturation("saturation", 255);
    static ByteConfigItem pulse_per_min("pulse_per_min", 7);

    static BaseConfigItem* configSet[] {
        &pattern,
        &hue,
        &value,
        &saturation,
        &pulse_per_min,
        0
    };

    static CompositeConfigItem config("noPrinterConnected", 0, configSet);

    return config;
}

CompositeConfigItem& BambuLights::getPrinterConnectedConfig() {
    static ByteConfigItem pattern("pattern", constant);
    static IntConfigItem  hue("hue", 0);
    static ByteConfigItem value("value", 128);
    static ByteConfigItem saturation("saturation", 0);
    static ByteConfigItem pulse_per_min("pulse_per_min", 7);

    static BaseConfigItem* configSet[] {
        &pattern,
        &hue,
        &value,
        &saturation,
        &pulse_per_min,
        0
    };

    static CompositeConfigItem config("printerConnected", 0, configSet);

    return config;
}

CompositeConfigItem& BambuLights::getPrintingConfig() {
    static ByteConfigItem pattern("pattern", constant);
    static IntConfigItem  hue("hue", 0);
    static ByteConfigItem value("value", 255);
    static ByteConfigItem saturation("saturation", 0);
    static ByteConfigItem pulse_per_min("pulse_per_min", 7);

    static BaseConfigItem* configSet[] {
        &pattern,
        &hue,
        &value,
        &saturation,
        &pulse_per_min,
        0
    };

    static CompositeConfigItem config("printing", 0, configSet);

    return config;
}

CompositeConfigItem& BambuLights::getErrorConfig() {
    static ByteConfigItem pattern("pattern", pulse);
    static IntConfigItem  hue("hue", 0);
    static ByteConfigItem value("value", 255);
    static ByteConfigItem saturation("saturation", 255);
    static ByteConfigItem pulse_per_min("pulse_per_min", 7);

    static BaseConfigItem* configSet[] {
        &pattern,
        &hue,
        &value,
        &saturation,
        &pulse_per_min,
        0
    };

    static CompositeConfigItem config("error", 0, configSet);

    return config;
}

CompositeConfigItem& BambuLights::getWarningConfig() {
    static ByteConfigItem pattern("pattern", constant);
    static IntConfigItem  hue("hue", 171);
    static ByteConfigItem value("value", 255);
    static ByteConfigItem saturation("saturation", 255);
    static ByteConfigItem pulse_per_min("pulse_per_min", 7);

    static BaseConfigItem* configSet[] {
        &pattern,
        &hue,
        &value,
        &saturation,
        &pulse_per_min,
        0
    };

    static CompositeConfigItem config("warning", 0, configSet);

    return config;
}

CompositeConfigItem& BambuLights::getFinishedConfig() {
    static ByteConfigItem pattern("pattern", constant);
    static IntConfigItem  hue("hue", 63);
    static ByteConfigItem value("value", 255);
    static ByteConfigItem saturation("saturation", 255);
    static ByteConfigItem pulse_per_min("pulse_per_min", 7);

    static BaseConfigItem* configSet[] {
        &pattern,
        &hue,
        &value,
        &saturation,
        &pulse_per_min,
        &getIdleTimeout(),
        0
    };

    static CompositeConfigItem config("finished", 0, configSet);

    return config;
}

CompositeConfigItem& BambuLights::getAllConfig() {
    static BaseConfigItem* configSet[] {
        &getNoWiFiConfig(),
        &getNoPrinterConnectedConfig(),
        &getPrinterConnectedConfig(),
        &getPrintingConfig(),
        &getErrorConfig(),
        &getWarningConfig(),
        &getFinishedConfig(),
	      0
    };

    static CompositeConfigItem config("leds", 0, configSet);

    return config;
};

BambuLights::BambuLights(int numPixels, int pin) : pixels(numPixels, pin), currentState(noWiFi) {
    setCurrentConfig(getNoWiFiConfig());
}

void BambuLights::setState(State state) {
  if (currentState != state) {
    currentState = state;

// noWiFi, noPrinter, printer, printing, no_lights, warning, error, finished
    bool oldOff = off;
    off = false;
    CHSV oldColor = {*currentHue, *currentSaturation, *currentValue};
    switch (state) {
      case noPrinter:
        setCurrentConfig(getNoPrinterConnectedConfig());
        break;
      case printer:
        setCurrentConfig(getPrinterConnectedConfig());
        break;
      case printing:
        setCurrentConfig(getPrintingConfig());
        break;
      case no_lights:
        off = true;
        break;
      case warning:
        setCurrentConfig(getWarningConfig());
        break;
      case error:
        setCurrentConfig(getErrorConfig());
        break;
      case finished:
        setCurrentConfig(getFinishedConfig());
        break;
      default:
        setCurrentConfig(getNoWiFiConfig());
        break;
    }
    CHSV newColor = {*currentHue, *currentSaturation, *currentValue};
    if (oldOff != off) {
      if (off) {
        newColor.h = oldColor.h;
        newColor.s = oldColor.s;
        newColor.v = 0;
      } else {
        oldColor.h = newColor.h;
        oldColor.s = newColor.s;
        oldColor.v = 0;;
      }
    }
    crossFade(oldColor, newColor);
  }
}

void BambuLights::setCurrentConfig(CompositeConfigItem& config) {
  currentConfig = &config;
  currentPattern = (ByteConfigItem*)config.get("pattern");
  currentHue = (IntConfigItem*)config.get("hue");
  currentValue = (ByteConfigItem*)config.get("value");
  currentSaturation = (ByteConfigItem*)config.get("saturation");
  currentPulsePerMin = (ByteConfigItem*)config.get("pulse_per_min");
}

void BambuLights::begin()  {
	pixels.Begin(); // This initializes the NeoPixel library.
	pixels.Show();
}

void BambuLights::loop() {
  //   enum patterns { dark, constant, rainbow, pulse, breath, num_patterns };
  uint8_t current_pattern = *currentPattern;

  if (off ) {
    clear();
    show();
  }
  else if (current_pattern == constant) {
    uint16_t val = *currentValue;

    val = val * brightness / 255;
    
    fill(*currentHue, *currentSaturation, val);
    show();
  }
  else if (current_pattern == pulse) {
    pulsePattern();
  }
}

void BambuLights::crossFade(const CHSV& oldColor, const CHSV& newColor) {
  for (int i=0; i < 255; i++) {
    CHSV blendedColor = ::blend(oldColor, newColor, i , SHORTEST_HUES);
    fill(blendedColor.h, blendedColor.s, blendedColor.v);
    show();
    delay(3); // So we will take approximately 0.75s to do the whole blend
  }
}

static byte valueMin = 20;

void BambuLights::pulsePattern() {
  // https://sean.voisen.org/blog/2011/10/breathing-led-with-arduino/
  float delta = (currentValue->value - valueMin) / 2.35040238;

  float pulse_length_millis = (60.0f * 1000) / currentPulsePerMin->value;
  float val = valueMin + (exp(sin(2 * M_PI * millis() / pulse_length_millis)) - 0.36787944f) * delta;
  val = val * currentValue->value / 256;
  val = val * brightness / 255;

  fill(*currentHue, *currentSaturation, val);

  show();
}

void BambuLights::fill(uint8_t hue, uint8_t sat, uint8_t val) {
  RgbColor color = HsbColor((byte)(hue)/256.0, (byte)(sat)/256.0, val/256.0);

  for (uint8_t digit=0; digit < pixels.PixelCount(); digit++) {
	  pixels.SetPixelColor(digit, colorGamma.Correct(color));
  }
}

void BambuLights::clear() {
  fill(0, 0, 0);
}

void BambuLights::show() {
  pixels.Show();
}

void BambuLights::setPixelColor(uint8_t digit, uint8_t hue, uint8_t sat, uint8_t val) {
    RgbColor color = HsbColor((byte)(hue)/256.0, (byte)(sat)/256.0, val/256.0);
    pixels.SetPixelColor(digit, colorGamma.Correct(color));
}

const String BambuLights::patterns_str[BambuLights::num_patterns] = 
  { "Constant", "Pulse" };
