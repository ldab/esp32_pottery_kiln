# kiln_controller
Connected electric kiln controller -> esp32, max31855, thermocouple type K, relay, contactor and energy meter. Including Kiln built plans.

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
