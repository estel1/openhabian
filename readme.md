# Smart home components & configuration for openhabian



## Начало работы
1. [Install ESP8266](https://habr.com/post/371853/).
2. [Arduino core for ESP8266 WiFi chip](https://github.com/esp8266/Arduino#installing-with-boards-manager).
3. [Arduino core for ESP32 WiFi chip](https://github.com/espressif/arduino-esp32#arduino-core-for-esp32-wifi-chip----).
4. Get source code
```bash
$ git clone https://github.com/estel1/openhabian
```
5. [ESP32S pinout](https://einstronic.com/wp-content/uploads/2017/06/NodeMCU-32S-Catalogue.pdf)
6. [ESP01 pinout](https://ecksteinimg.de/Datasheet/Ai-thinker%20ESP-01%20EN.pdf)

## Подключение Relay
[Relay Isolation](https://arduino-info.wikispaces.com/RelayIsolation)

[4 ways to eliminate ESP8266 resets](https://internetofhomethings.com/homethings/?p=396)

## Разное
Clored values in sitemap:
Text item=soil_moisture valuecolor=[soil_moisture>120="orange", soil_moisture>300="red"]
