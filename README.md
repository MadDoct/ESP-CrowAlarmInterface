# ESP-CrowAlarmInterface
Interface for Crow Runner 8/16 Alarm System using an ESP8266 connected to CLK and DAT lines (usually used by the Keypads)

Based on https://github.com/sivann/crowalarm - thanks to sivann for figuring out the communication protocol.
In the meantime, upon further research, I found out that the alarm uses HDLC-like as the communication protocol and adapted accordingly (removed the bit invertion that was done based on sivann's implementation - I still don't know why he did it).

I started from his work, adapted it to C++/arduino sketch for usage with an ESP8266 (and changed it to reflect the actual communication protocol - akin to HDLC).
That allowed me to add reporting/control using MQTT.
I then figured out how to extend the zone reporting for upto 16 zones and added the ability to report the alarm status, including the zone that triggered the alarm.
I also added the possibility to activate 3 relays to activate/deactivate the alarm by simulating keyswitches.
Finally I was able to implement bidirectional communication, so now it's possible to activate and deactivate the alarm just by using the bus without keyswitches.
I had some help from ChatGPT, but to be truthfull it made a LOT of mistakes and it took me an absurdely long time to correct the parts of the code it provided and find out its mistakes...

Some strings are in Portuguese - my native language - sorry for that. I will probably "correct" that in the future.

Pinout: Initially converted CROW 5V clock (CLK) and data (DAT) keypad signals to 3.3V with a resistor divider connected to GPIO pins D5, D6 for Esp8266.. Then I started using a logic level converter instead of the voltage dividers to allow for bidirectional communication.
Also connected crow NEG to Esp8266 GND.
Pins D1, D2 and D7 can be connected to relays that get activated for 1 second, activating/disarming the alarm by simulating keyswitches (refer to the alarm manual on how to use this), if you rather use this than instead of the bus communication.

DISCLAIMER: This has been tested in my alarm and probably works with all the alarms of the same model, but due to possible differences in firmware or configuration of the alarm, your mileage may vary...
