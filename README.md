# kiln_controller
Connected electric kiln controller -> esp32, max31855, thermocouple type K, relay, contactor and energy meter. Including Kiln built plans.

## ESPHome firmware

An ESPHome configuration is available in [`esphome/kiln.yaml`](esphome/kiln.yaml). It targets the ESP32-WROOM-32 shown in the schematic and retains the four-stage ramp/hold firing schedule, 5 °C relay differential, 5.53 second contactor control cadence, controlled cooling, RGB status LED, and S0 energy pulse measurement using ESPHome's native `pulse_meter`. MQTT, Papertrail, and the old browser graph are not used by this configuration.

The ESPHome web server provides the firing profile number controls and Start, Stop, and Clear Fault buttons. Wi-Fi provisioning is handled by ESPHome's captive portal. It is a state control page; network credentials and firmware settings remain in the YAML/secrets file.

### Install

1. Copy `esphome/secrets.yaml.example` to `esphome/secrets.yaml` and replace its sample credentials. Generate a private API key with `openssl rand -base64 32`.
2. Install ESPHome 2026.9 or newer and run `esphome run esphome/kiln.yaml`.
3. After Wi-Fi setup, open `http://pottery-kiln.local/` or the ESP32's IP address. The HTTP page uses the username and password in `secrets.yaml`.
4. In the web page or Home Assistant, set all four target/rate/hold groups and press **Start Kiln Firing**. The target temperatures must be nondecreasing; the final target is limited to 1260 °C.

With no saved Wi-Fi credentials, connect to the `Kiln Fallback` access point and use its captive portal to configure the network. Profile parameters are saved as ESPHome number states. An interrupted or restarted firing does not resume automatically; the relay starts off and a new firing must be started manually.

### Hardware pins

| Schematic signal | ESP32 GPIO | ESPHome use |
| --- | ---: | --- |
| gRLY | 16 | Contactor relay output, active high, always off at boot |
| S0+ | 17 | Energy meter pulse input |
| MAX31855 CS | 22 | Thermocouple converter chip select |
| MAX31855 MISO/SO | 21 | Thermocouple converter data |
| MAX31855 SCK | 23 | Thermocouple converter clock |
| LED_R / LED_G / LED_B | 25 / 26 / 27 | Active-low RGB status indicator |

GPIO32/33 are the schematic's SDA/SCL connections and are not used by the current firmware. The energy calculation assumes the existing firmware's value of 0.5 Wh per S0 pulse.

### MAX31855 and high-temperature handling

The MAX31855K supports K-type measurements up to 1350 °C, so the project's 1260 °C profile limit is within its stated range. Although the schematic/BOM list L1/L2 ferrite beads, the assembled hardware has solder bridges at those positions; C7 (100 nF in the schematic) is also unpopulated. The MAX31855 inputs therefore have no added series ferrite or differential capacitor filtering, which can increase noise sensitivity. The original firmware's `Ignore SCG fault` behavior is retained by the small local ESPHome sensor component in `esphome/components/max31855_scg/`: it accepts a short-to-ground flag by itself, while an open thermocouple, short to VCC, or unreadable SPI data yields an invalid temperature and latches the heater off. This tolerance preserves the kiln workaround, but a real thermocouple short to ground can also make its reading unreliable. The 1300 °C firmware cutoff is a software limit and does not replace an independent kiln over-temperature cutout.

### Remote crash reporting

ESPHome's native API logger automatically retains warm-reset crash backtraces and reports them after the device reconnects. ESPHome CLI/Device Builder can decode the trace with the debug symbols from the matching firmware build. The configuration also exposes the last reset reason to Home Assistant and sends `esphome.kiln_crash` after panic, watchdog, brownout, or CPU lock-up resets. In the ESPHome integration settings, allow this device to perform Home Assistant actions. Then add [`home-assistant-crash-automation.yaml`](esphome/home-assistant-crash-automation.yaml) to send a persistent notification. See [ESPHome crash troubleshooting](https://esphome.io/guides/troubleshooting/) and the [MAX31855 component](https://esphome.io/components/sensor/max31855/).

[![GitHub version](https://img.shields.io/github/v/release/ldab/esp32_pottery_kiln?include_prereleases)](https://github.com/ldab/kiln_controller/releases/latest)
![Build Status](https://github.com/ldab/esp32_pottery_kiln/actions/workflows/workflow.yml/badge.svg)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](https://github.com/ldab/kiln_controller/blob/master/LICENSE)

[![GitHub last commit](https://img.shields.io/github/last-commit/ldab/esp32_pottery_kiln.svg?style=social)](https://github.com/ldab/esp32_pottery_kiln)

 ## TODO

- [ ] Temperature Ramp rate monitor;
- [ ] Cooling monitor;
- [ ] Cool upload progress: https://codepen.io/takaneichinose/pen/jOWXBBd

## VOID

"Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum."

## Bil of materials

Description | Price
------------ | -------------
[4x Modules enclosure](https://www.amazon.de/gp/product/B07K5X5KZQ/ref=ppx_yo_dt_b_asin_title_o00_s00?ie=UTF8&psc=1) | €8.00
[Energy meter with S0 output](https://www.amazon.de/gp/product/B083H7NT2R/ref=ppx_yo_dt_b_asin_title_o00_s01?ie=UTF8&psc=1) | €13.00
[HDR-15 Power Supply](https://www.amazon.de/gp/product/B06XWQSJGW/ref=ppx_yo_dt_b_asin_title_o00_s00?ie=UTF8&psc=1) | €13.00
[12v/16A Relay](https://www.tme.eu/se/en/details/pi85012dc00ld/electromagnetic-relays-sets/relpol/pi85-012dc-00ld/) | €13.00
esp32 | €5.00
max31855 | €10.00
[Type-K thermocouple](https://www.keramik-kraft.com/en/Kiln-Building--Repair/Pyrometry/Thermocouple-Nickel-Typ-K/Thermocouple-Type-K-open-with-flange-l-12cm.html?sel=13) | €50.00
Cables, terminals, etc | €10.00

Total: **€120.00**

## Element calculations

### Element power

* This is determined by the power outlet, i.e `230Vac@16A` -> **3680W**

* Some [resources](https://knifedogs.com/threads/heat-treat-oven-how-to-design-and-calculate-the-heating-elements.21072/) indicate 0.6 - 1.3 W/cm2 of wall -> `Aw = (30.4 * 30.4 * 2) + (30.4 * 34.2 * 4) = 6,007.04 cm2` therefore Element should be between *3604W* and *7800W*.

* [KMT-614](https://skutt.com/products-page/ceramic-kilns/kmt-614/) and [Ecotop 20](https://www.rohde.eu/en/arts-and-crafts/products/toploaders/ecotop-series/ecotop-20) are both ~20L with 2300W element and rated to [Cone 6](https://www.ortonceramic.com/files/2676/File/orton-cone-chart-2016.pdf), aka ~1260°C

### Element size

Using Kanthal D - Other variants were hard to find, Kanthal D can be bought at [eveks.se](https://eveks.se/kategorier/461-kanthal-tr%C3%A5d-01-5mm-v%C3%A4rmekabel-14765-kanthal-d-motst%C3%A5ndstr%C3%A5d-1-100-meter-4066435006815.html)

* 1.6mm -> R = 0.67 Ohms/m
* L = ( 3680 / 16^2 ) / 0.67 = **~21.5m**

### Surface Load

* Accordingly to Kanthal [datasheet](./extras/Kanthal%20handbook.pdf), kiln application should have a Surface Load between 3-9 W/cm2:

`3680 / (2pi x 0.08 x 2150) = **3.4**W/cm2`

* [furnace handbook](./extras/file1359965681_U3423.pdf) page 7 indicates max 2.4W/cm2 @ 1100°C

### Coil Diameter

* In order to avoid deformation on horizontal coils, the coild diameter should stay below 10mm accordingly to:

<img src="./pics/Dd.png" width="50%"> 

* Therefore let's use a **M8** rod to coil the wire.

### Coil Length

* The internal element perimeter is ~1216mm, if 3x turns are planned, we end up with a **3648mm** coil;

### Coil Pitch

*Coil pitch is normally 2-4 times the wire diameter* [page_79](./extras/Kanthal20handbook.pdf) -> aka 2-4mm

`s = (11.2 - 8) x pi / sqrt((21500 / 3648) ^ 2 - 1) = ~**2mm**`

## Bricks

The kils was build with [JM23](https://www.morganthermalceramics.com/media/4120/ifb-insulation-range-1100-1315-english.pdf) bricks

### Bricks thermal heat loss

![](./pics/old_heat_loss.png)

## PCB

PCB layout and files are found at [](./pcb/) open it with KiCad

![](./pics/pcb.png)

## Known Limitations

* 

## Credits

Github Shields and Badges created with [Shields.io](https://github.com/badges/shields/)
