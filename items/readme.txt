Your item definitions go here.
All items files have to have the ".items" file extension and must follow a special syntax.

Check out the openHAB documentation for more details:
https://www.openhab.org/docs/configuration/items.html

This is copy [from:](https://community.openhab.org/t/state-command-in-mqtt-messages/10359/5)
State / Command in MQTT messages
Setup, Configuration and Use
Items & Sitemaps
state esp8266 mqtt
 
rlkoshak
Rich Koshak
Foundation member
May '16
What’s not clear is what is the purpose of having both state and command on the same line?

The “state” part of the binding above is actually part of the topic name. It could be anything. The “command” part of the above is to indicate that when this Item receives a command from OH to send a message to the topic. The “ON” and “OFF” part specifies which command received corresponds to which binding config. The “1” and “0” part indicates what the message to publish should be.

So the the binding means in English:

When the office_panel receives an ON command send the message “1” to the “office/switch1/state” topic on the mosquitto broker.

When the office_panel receives an OFF command send the message “0” to the “office/switch1/state” topic on the mosquitto broker.

This way I could “read” the state from another item, rule, script?

This Item set up with outbound MQTT binding configs only. When office_panel receives a command the MQTT binding will send the appropriate message to the topic. You can read the state of office_panel from your rules but it doesn’t really necessarily reflect the state of the actual switch because you don’t have any incoming messages. If someone or something else switches the switch OH will not know about it.

Is MQTT persistence necessary?

Are you talking about mosquitto config or OH config. In OH, MQTT persistence is not necessary. In mosquitto it depends on whether you want to guarantee that OH receives all messages even if it happens to be down when the message is sent. Most likely the answer is no.

Is it better to have the controlled device (ESP module in this case) send the state and persist it when a command is issued (just in case it is set from somewhere else)?

When possible it is best for the device to report its status if it can be controlled outside of OH. However, you will need to add an incoming binding config to your Item along the lines of:

<[mosquitto:office/switch1/state:state:default:.*]
This line means for any messages published to the topic “office/switch1/state” set the state of office_panel to the message.

The Switch Item is smart enough to convert “1” to ON and “0” to OFF for you so you do not need to specify a transform.

3



jose
Jose Vazquez
May '16
First of, thank you so much @rlkoshak for taking the time to reply and being so clear :slight_smile:

I added the line you suggested to the item:
Switch office_panel "Panel" <heating> (grpOffice) {mqtt=">[mosquitto:office/switch1/state:command:ON:1],>[mosquitto:office/switch1/state:command:OFF:0],<[mosquitto:office/switch1/state:state:default:.*]"}

I still can command it, but when I do, I get the following line in the log file:
[WARN ] [.c.i.events.EventPublisherImpl] - given new state is NULL, couldn't post update for 'office_panel'

Same happens if I publish either 0 or 1 to office/switch1/state from another MQTT client (some testing before I actually update the code in the ESP module). What could be wrong?

2 Replies


rlkoshak
Rich Koshak
Foundation member
May '16
The MQTT binding may not be as smart as some of the others in converting 1 and 0 to ON and OFF. Instead of “default” you can use a MAPPING and create a mapping file to convert 1 to ON and 0 to OFF. or change your ESP module to send the strings “ON” and “OFF” which I can confirm does work (I use them in my setup).




watou
John Cocula

jose
May '16
<[mosquitto:office/switch1/state:state:default:.*]

This isn’t the syntax for the inbound binding config 149 that you want to use. Instead of using the optional regex filter at the end, you want to replace default with a MAP transform:

<[mosquitto:office/switch1/state:state:MAP(onoff.map)]
transform/onoff.map:

0=OFF
1=ON
So whenever a message arrives at the binding on the given topic, if it is a “1”, then it is replaced with “ON”, which the Switch item can parse (roughly speaking).

You may also want to add autoupdate="false" to the item binding, so the act of sending a command to the item does not immediately set the item’s state. With autoupdate="false", updating the state will only occur explicitly when a binding or rule updates the item’s state.

Last point: You may want to have two MQTT topics, one dedicated to commands to your device that your device subscribes to, for example office/switch1/command, and a separate topic that openHAB subscribes to, office/switch1/state, which reflects the state of the switch.

Complete item definition incorporating the above advice:

Switch office_panel "Panel" <heating> (grpOffice) {mqtt=">[mosquitto:office/switch1/command:command:ON:1],>[mosquitto:office/switch1/command:command:OFF:0],<[mosquitto:office/switch1/state:state:MAP(onoff.map)]", autoupdate="false"}
Solution

4


2 YEARS LATER

koolpat
Pratish Patel
Dec '17
My setup:
EasyESP firmware on nodemcu.
Openhab2
Raspberry pi running openhab2 with mosquito mqtt broker.
In openhab2, i have install mqtt binding and MAP transformation.

home.items file for one of the switch.

Switch F2_Bedroom_Power “Power Outlet” (F2_Bedroom, gPower) {mqtt=">[broker:/nodemcu01/sw1/swcmd/cmd:command:ON:gpio,16,1], >[broker:/nodemcu01/sw1/swcmd/cmd:command:OFF:gpio,16,0], <[broker:/nodemcu01/sw1/swstat:state:MAP(binary.map)]"}

binary.map file content.

root@openHABianPi:/etc/openhab2/items# cat …/transform/binary.map
1=ON
0=OFF

Using above i can able to command my switch ( on/off ) also,it updates the state of the switch if its turned ON/OFF externally.



Hello! Looks like you’re enjoying the discussion, but you haven’t signed up for an account yet.

When you create an account, we remember exactly what you’ve read, so you always come right back where you left off. You also get notifications, here and via email, whenever someone replies to you. And you can like posts to share the love. 💗

 Sign Up   Remind me tomorrow no thanks
Suggested Topics
