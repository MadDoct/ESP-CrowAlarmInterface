# ESP-CrowAlarmInterface
Interface for Crow Runner 8/16 Alarm System using an ESP8266 connected to CLK and DAT lines (usually used by the Keypads)

Based on https://github.com/sivann/crowalarm - thanks to sivann for figuring out the communication protocol.
In the meantime, upon further research, I found out that the alarm uses HDLC-like as the communication protocol and adapted accordingly (removed the bit invertion that was done based on sivann's implementation - I still don't know why he did it).

I started from his work, adapted it to C++/arduino sketch for usage with an ESP8266 (and changed it to reflect the actual communication protocol - akin to HDLC).
So it now has:
- reporting/control using MQTT.
- extended zone reporting for upto 16 zones.
- ability to report the alarm status, including the zone that triggered the alarm.
- bidirectional communication:
  - now it's possible to activate and deactivate the alarm just by using the bus without keyswitches.
- possibility to activate 3 relays to activate/deactivate the alarm by simulating keyswitches.
- OTA updates using ArduinoOTA.

I had some help from ChatGPT, but to be truthfull it made a LOT of mistakes and it took me an absurdely long time to correct the parts of the code it provided and find out its mistakes...

Some strings are in Portuguese - my native language - sorry for that. I will probably "correct" that in the future.

Pinout: Initially converted CROW 5V clock (CLK) and data (DAT) keypad signals to 3.3V with a resistor divider connected to GPIO pins D5, D6 for Esp8266.. Then I started using a logic level converter instead of the voltage dividers to allow for bidirectional communication.
Also connected crow NEG to Esp8266 GND.
Pins D1, D2 and D7 can be connected to relays that get activated for 1 second, activating/disarming the alarm by simulating keyswitches (refer to the alarm manual on how to use this), if you rather use this instead of the bus communication.

There are 2 different config files for Home Assistant that use the MQTT Alarm Control Panel integration - https://www.home-assistant.io/integrations/alarm_control_panel.mqtt/ - in the folder HAConfig:
- alarm-bus.yaml should be used in case you are using the bus to control the alarm (with logic level converters);
- alarm-keyswitch.yaml should be used in case you are using the keyswitches to control the alarm (with voltage dividers and relays simulating keyswitches).

To control the alarm using the bus, sen the following payloads to the control topic (usually Alarm/control):
To arm away - total
To arm night - parcial
to disarm (replace xxxx with your usual code) - desarmar-xxxx

DISCLAIMER: This has been tested in my alarm and probably works with all the alarms of the same model, but due to possible differences in firmware or configuration of the alarm, your mileage may vary...
