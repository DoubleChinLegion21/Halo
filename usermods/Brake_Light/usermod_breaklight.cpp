#include "wled.h"

class UsermodBreakLight : public Usermod {

  private:
    bool enabled = true;
    bool initDone = false;
    unsigned long lastTime = 0;
    unsigned long brakeLightSetTime = 0;
    unsigned long sidewaysSetTime = 0;
    bool isSideways = false;
    bool sidewaysSleep;
    bool isBraking = false;

    int travelAxis;
    int verticalAxis;    
    bool invertAxis;
    
    long engageBrakeLightGeForce;
    long futureFeature;   
    long disengageBrakeLightGeForce;
    long brakeSustainDuration;

    long xAccl = 0;
    long yAccl = 0;
    long zAccl = 0;
    int breakBrightness = 0;

    // string that are used multiple time (this will save some flash memory)
    static const char _name[];
    static const char _enabled[];
    

    // void publishMqtt(const char* state, bool retain = false);

  public:
    /**
     * Enable/Disable the usermod
     */
    inline void enable(bool enable) { this->enabled = enable; }

    /**
     * Get usermod enabled/disabled state
     */
    inline bool isEnabled() { return enabled; }

    /*
     * setup() is called once at boot. WiFi is not yet connected at this point.
     * readFromConfig() is called prior to setup()
     */
    void setup() 
    {      
      Wire.begin(21, 22);  // Initialise I2C communication as MASTER      
      // 5 and 6 for xiao esp32s3
      // 21 and 22 for HaloCore Gen 1
      // 35 and 36 for HaloTail Gen 1

      // BMA250 I2C address is 0x18(24)
      #define Addr 0x18
      
      // Try to start BMA253 5x times
      bool startup_failed = true;
      for (int i = 0; i < 5; ++i) {
        Wire.beginTransmission(Addr); // Start I2C Transmission      
        if (Wire.endTransmission() == 0) {
          enabled = true;
          startup_failed = false;
          break;
        }
        delay(250);
      }
      if (startup_failed)
      {
        Serial.println("BMA253 not found!");
        enabled = false;
        return;
      }

      Wire.beginTransmission(Addr);
      Wire.write(0x0F);             // Select range selection register      
      Wire.write(0x03);             // Set range +/- 2g      
      //Wire.write(0x05);             // Set range +/- 4g      
      Wire.endTransmission();       // Stop I2C Transmission
      
      Wire.beginTransmission(Addr); // Start I2C Transmission      
      Wire.write(0x10);             // Select bandwidth register      
      Wire.write(0x08);             // Set bandwidth 7.81 Hz      
      //Wire.write(0x09);             // Set bandwidth 15.63 Hz 
      Wire.endTransmission();       // Stop I2C Transmission
      delay(300);
      initDone = true;
    }


    /*
     * connected() is called every time the WiFi is (re)connected
     */
    void connected() 
    {
      ;//Serial.println("Connected to WiFi!");
    }

  long getAcclByAxis(int axis, long xAccl, long yAccl, long zAccl) {
    switch(axis) {
        case 0: return xAccl;
        case 1: return yAccl;
        case 2: // fall through
        default: return zAccl;
    }
  }  

  void turnOnFromSideways() {
    if (isSideways && bri == 0 && sidewaysSleep) 
    {
      applyPreset(1);
    }
    isSideways = false;
  }

  void readAccel()
  {
    unsigned int data[8];    
    Wire.beginTransmission(Addr); // Start I2C Transmission    
    Wire.write(0x02);             // Select Data Registers (0x02 − 0x07)
    Wire.endTransmission();       // Stop I2C Transmission
    Wire.requestFrom(Addr, 6);    // Request 6 bytes 
    
    // Read the six bytes 
    // xAccl lsb, xAccl msb, yAccl lsb, yAccl msb, zAccl lsb, zAccl msb
    if(Wire.available() != 6)
      return;
    
    data[0] = Wire.read();
    data[1] = Wire.read();
    data[2] = Wire.read();
    data[3] = Wire.read();
    data[4] = Wire.read();
    data[5] = Wire.read();
    
    // Convert the data to 10 bits
    xAccl = ((data[1] * 256) + (data[0] & 0xC0)) / 64;
    if (xAccl > 511)
    {
      xAccl -= 1024;
    }
    yAccl = ((data[3] * 256) + (data[2] & 0xC0)) / 64;
    if (yAccl > 511)
    {
      yAccl -= 1024;
    }
    zAccl = ((data[5] * 256) + (data[4] & 0xC0)) / 64;
    if (zAccl > 511)
    {
      zAccl -= 1024;
    }    
  }

    /*
     * loop() is called continuously. Here you can check for events, read sensors, etc.
     */
    void loop() 
    {
      // NOTE: on very long strips strip.isUpdating() may always return true so update accordingly
      if (!enabled || strip.isUpdating()) 
        return;

      // Serial.print("xAccl:");
      // Serial.print(xAccl);
      // Serial.print(",yAccl:");
      // Serial.print(yAccl);
      // Serial.print(",zAccl:");
      // Serial.print(zAccl); 

      unsigned long nowTime = millis();
      bool recalcBrakeLight = (nowTime - lastTime > 30);
      bool turnOffBrakeLight = (nowTime - brakeLightSetTime > brakeSustainDuration); 

      if(turnOffBrakeLight)
        if(currentPreset == 2)
            applyPreset(1);

      if(recalcBrakeLight) 
      {
        readAccel();
       
        long travelAccl = getAcclByAxis(travelAxis, xAccl, yAccl, zAccl);
        long verticalAccl = getAcclByAxis(verticalAxis, xAccl, yAccl, zAccl);
        if(invertAxis)
          travelAccl = -travelAccl;
        
        // First, handle if wheels is on side, vertical axis will be 0.
        if ((-50 < verticalAccl && verticalAccl < 50 && travelAccl > 200) && sidewaysSleep) 
        {
          if (currentPreset == 2) {
            applyPreset(1);
          }
          else if (!isSideways) 
          {
            sidewaysSetTime = nowTime;
            isSideways = true;  // isSideways will be set false in every other branch
          }
          else if (isSideways)
          {
            if (nowTime - sidewaysSetTime > 5000 && bri != 0)
            {
              bri = 0;
              stateUpdated(CALL_MODE_DIRECT_CHANGE);
              // toggleOnOff();
            }
          }
          lastTime = nowTime;  // Ignore for a while, the wheel is on its side
        }
        // Then handle regular braking conditions
        else if(travelAccl < engageBrakeLightGeForce)
        {
          turnOnFromSideways();
          isBraking = false;
          if(currentPreset == 2 && travelAccl < disengageBrakeLightGeForce)
            applyPreset(1);
            // RESET
        }
        else
        {
          turnOnFromSideways();
          brakeLightSetTime = nowTime;
          // isBraking scrubs applyPreset(2); from getting called repeatedly
          if (!isBraking) 
          {
            isBraking = true;
            applyPreset(2);
          }
          // BRAKE
        }

// #if 0
//         Serial.print(brightnesss);        
//         Serial.print("=map(");
//         Serial.print(travelAccl);
//         Serial.print(",");
//         Serial.print(engageBrakeLightGeForce);
//         Serial.print(",");
//         Serial.print(futureFeature);
//         Serial.print(",");
//         Serial.print(0);
//         Serial.print(",");
//         Serial.print(255);
//         Serial.print(")  ");

//         Serial.print("xAccl:");
//         Serial.print(xAccl);
//         Serial.print(",yAccl:");
//         Serial.print(yAccl);
//         Serial.print(",zAccl:");
//         Serial.print(zAccl); 
//         Serial.print(",brightnesss:");
//         Serial.println(brightnesss);      
// #endif
//         lastTime = millis();

//         if(brightnesss > breakBrightness)  
//         {
//           breakBrightness = brightnesss;
//           brakeLightSetTime = lastTime;
//         }  

//         if(travelAccl < disengageBrakeLightGeForce)
//           breakBrightness = 0;
      }
    }


    /*
     * addToJsonInfo() can be used to add custom entries to the /json/info part of the JSON API.
     * Creating an "u" object allows you to add custom key/value pairs to the Info section of the WLED web UI.
     * Below it is shown how this could be used for e.g. a light sensor
     */
    void addToJsonInfo(JsonObject& root)
    {
      // if "u" object does not exist yet wee need to create it
      JsonObject user = root["u"];
      if (user.isNull()) user = root.createNestedObject("u");
    }


    /*
     * addToJsonState() can be used to add custom entries to the /json/state part of the JSON API (state object).
     * Values in the state object may be modified by connected clients
     */
    void addToJsonState(JsonObject& root)
    {
      if (!initDone || !enabled) 
        return;  // prevent crash on boot applyPreset()

      JsonObject usermod = root[FPSTR(_name)];
      if (usermod.isNull()) usermod = root.createNestedObject(FPSTR(_name));

      //usermod["user0"] = userVar0;
    }


    /*
     * addToConfig() can be used to add custom persistent settings to the cfg.json file in the "um" (usermod) object.
     * It will be called by WLED when settings are actually saved (for example, LED settings are saved)
     * If you want to force saving the current state, use serializeConfig() in your loop().
     * 
     * CAUTION: serializeConfig() will initiate a filesystem write operation.
     * It might cause the LEDs to stutter and will cause flash wear if called too often.
     * Use it sparingly and always in the loop, never in network callbacks!
     * 
     * addToConfig() will make your settings editable through the Usermod Settings page automatically.
     *
     * Usermod Settings Overview:
     * - Numeric values are treated as floats in the browser.
     *   - If the numeric value entered into the browser contains a decimal point, it will be parsed as a C float
     *     before being returned to the Usermod.  The float data type has only 6-7 decimal digits of precision, and
     *     doubles are not supported, numbers will be rounded to the nearest float value when being parsed.
     *     The range accepted by the input field is +/- 1.175494351e-38 to +/- 3.402823466e+38.
     *   - If the numeric value entered into the browser doesn't contain a decimal point, it will be parsed as a
     *     C int32_t (range: -2147483648 to 2147483647) before being returned to the usermod.
     *     Overflows or underflows are truncated to the max/min value for an int32_t, and again truncated to the type
     *     used in the Usermod when reading the value from ArduinoJson.
     * - Pin values can be treated differently from an integer value by using the key name "pin"
     *   - "pin" can contain a single or array of integer values
     *   - On the Usermod Settings page there is simple checking for pin conflicts and warnings for special pins
     *     - Red color indicates a conflict.  Yellow color indicates a pin with a warning (e.g. an input-only pin)
     *   - Tip: use int8_t to store the pin value in the Usermod, so a -1 value (pin not set) can be used
     *
     * See usermod_v2_auto_save.h for an example that saves Flash space by reusing ArduinoJson key name strings
     * 
     * If you need a dedicated settings page with custom layout for your Usermod, that takes a lot more work.  
     * You will have to add the setting to the HTML, xml.cpp and set.cpp manually.
     * See the WLED Soundreactive fork (code and wiki) for reference.  https://github.com/atuline/WLED
     * 
     * I highly recommend checking out the basics of ArduinoJson serialization and deserialization in order to use custom settings!
     */
    void addToConfig(JsonObject& root)
    {
      JsonObject top = root.createNestedObject(FPSTR(_name));
      top[FPSTR(_enabled)] = enabled;
      //save these vars persistently whenever settings are saved
      top["invertAxis"] = invertAxis;
      top["travelAxis"] = travelAxis;
      top["verticalAxis"] = verticalAxis;
      top["disengageBrakeLightGeForce"] = disengageBrakeLightGeForce;
      top["engageBrakeLightGeForce"]    = engageBrakeLightGeForce;
      top["futureFeature"]       = futureFeature;
      top["brakeSustainDuration"]         = brakeSustainDuration;     
      top["sidewaysSleep"]         = sidewaysSleep; 
    }


    /*
     * readFromConfig() can be used to read back the custom settings you added with addToConfig().
     * This is called by WLED when settings are loaded (currently this only happens immediately after boot, or after saving on the Usermod Settings page)
     * 
     * readFromConfig() is called BEFORE setup(). This means you can use your persistent values in setup() (e.g. pin assignments, buffer sizes),
     * but also that if you want to write persistent values to a dynamic buffer, you'd need to allocate it here instead of in setup.
     * If you don't know what that is, don't fret. It most likely doesn't affect your use case :)
     * 
     * Return true in case the config values returned from Usermod Settings were complete, or false if you'd like WLED to save your defaults to disk (so any missing values are editable in Usermod Settings)
     * 
     * getJsonValue() returns false if the value is missing, or copies the value into the variable provided and returns true if the value is present
     * The configComplete variable is true only if the "exampleUsermod" object and all values are present.  If any values are missing, WLED will know to call addToConfig() to save them
     * 
     * This function is guaranteed to be called on boot, but could also be called every time settings are updated
     */
    bool readFromConfig(JsonObject& root)
    {
      JsonObject top = root[FPSTR(_name)];

      bool configComplete = !top.isNull();

      configComplete &= getJsonValue(top[FPSTR(_enabled)], enabled);
      configComplete &= getJsonValue(top["travelAxis"], travelAxis, 2);
      configComplete &= getJsonValue(top["verticalAxis"], verticalAxis, 0);
      configComplete &= getJsonValue(top["invertAxis"], invertAxis, true);
      configComplete &= getJsonValue(top["engageBrakeLightGeForce"], engageBrakeLightGeForce, 60);
      configComplete &= getJsonValue(top["futureFeature"], futureFeature, 130);
      configComplete &= getJsonValue(top["disengageBrakeLightGeForce"], disengageBrakeLightGeForce, -20);
      configComplete &= getJsonValue(top["brakeSustainDuration"], brakeSustainDuration, 2500);
      configComplete &= getJsonValue(top["sidewaysSleep"], sidewaysSleep, true);

      return configComplete;
    }

    /*
     * appendConfigData() is called when user enters usermod settings page
     * it may add additional metadata for certain entry fields (adding drop down is possible)
     * be careful not to add too much as oappend() buffer is limited to 3k
     */
    void appendConfigData()
    {
      oappend(F("addInfo('BrakeLight:engageBrakeLightGeForce',    1, 'g-force');"));
      oappend(F("addInfo('BrakeLight:disengageBrakeLightGeForce', 1, 'g-force');"));
      oappend(F("addInfo('BrakeLight:futureFeature',              1, 'g-force');"));
      oappend(F("addInfo('BrakeLight:brakeSustainDuration',       1, 'milliseconds');")); 
     
      oappend(F("dd=addDropdown('BrakeLight','travelAxis');")); 
      oappend(F("addOption(dd,'X',0);"));
      oappend(F("addOption(dd,'Y',1);"));
      oappend(F("addOption(dd,'Z',2);"));

      oappend(F("dd=addDropdown('BrakeLight','verticalAxis');")); 
      oappend(F("addOption(dd,'X',0);"));
      oappend(F("addOption(dd,'Y',1);"));
      oappend(F("addOption(dd,'Z',2);"));
    }


    /**
     * onStateChanged() is used to detect WLED state change
     * @mode parameter is CALL_MODE_... parameter used for notifications
     */
    void onStateChange(uint8_t mode) {
      // do something if WLED state changed (color, brightness, effect, preset, etc)
    }


    /*
     * getId() allows you to optionally give your V2 usermod an unique ID (please define it in const.h!).
     * This could be used in the future for the system to determine whether your usermod is installed.
     */
    uint16_t getId()
    {
      return USERMOD_ID_BREAKLIGHT;
    }

   //More methods can be added in the future, this example will then be extended.
   //Your usermod will remain compatible as it does not need to implement all methods from the Usermod base class!
};


// add more strings here to reduce flash memory usage
const char UsermodBreakLight::_name[]    PROGMEM = "BrakeLight";
const char UsermodBreakLight::_enabled[] PROGMEM = "enabled";

static UsermodBreakLight brakelight;
REGISTER_USERMOD(brakelight);
