# ESP-CrowAlarmInterface
Interface for Crow Runner 8/16 Alarm System using an ESP8266 connected to CLK and DAT lines (usually used by the Keypads)

Based on https://github.com/sivann/crowalarm - thanks to sivann for figuring out the communication protocol
I started from his work, adapted it to C++/arduino sketch for usage with an ESP8266.
That allowed me to add reporting/control using MQTT.
I then figured out how to extend the zone reporting for upto 16 zones and added the ability to report the alarm status, including the zone that triggered the alarm.
I also added the possibility to activate 3 relays to simulate activation of the alarm using keyswitches to control the activation of the alarm.

Pinout: Converted CROW 5V clock (CLK) and data (DAT) keypad signals to 3.3V with a resistor divider. 
Then connected clock (CLK) and data (DAT) to GPIO pins D5, D6 for Esp8266
Also connected crow NEG to Esp8266 GND
Pins D1, D2 and D7 are connected to relays that get activated for 1 second, activating/disarming the alarm by simulating keyswitches (refer to the alarm manual on how to use this)
