# PUNKTRACKER 2.5 for Quan Sheng UV-K5

Open firmware based on the Quan Sheng UV-K5 v2.1.27 reimplementation and the
PUNKTRACKER II sources modified by Lightbringer.

## Changes in this fork / Изменения в этой версии

### Hardware AFC for Doppler correction / Аппаратный AFC

FM reception uses the BK4819 hardware AFC to capture and track a signal whose
carrier is offset from the selected receiver frequency.  This is useful for
LEO satellites such as the ISS, where Doppler shift changes during a pass.
The stored RX channel and TX frequency are not modified.

The main screen shows `AFC:+/-...` in hertz while a signal is actually being
received or Monitor is active.  The value is read from the BK4819 AFC register
and is relative to the selected RX frequency.  It can therefore also show the
characteristic frequency error of another transmitter.  The indication is
hidden before squelch opens so that receiver noise does not make it flicker.

The `AFC` menu has two capture ranges:

* `STD 7K` — up to approximately ±7 kHz.
* `MAX 10K` — up to approximately ±10 kHz; recommended for ISS reception.

The selected range is stored in EEPROM.  The AFC display is positioned after
the bandwidth indicator without overlapping `25k`, `12.5k`, or `6.25k`.

При приёме FM аппаратный AFC микросхемы BK4819 автоматически захватывает и
сопровождает сигнал со смещением частоты. На основном экране во время приёма
отображается текущее отклонение `AFC:+/-...` в герцах. Для МКС рекомендуется
режим `MAX 10K`; для обычной работы доступен `STD 7K`.

### Receive audio attenuator / Аттенюатор НЧ

The `AF Att` menu reduces the complete received audio signal before the
radio's analog volume control.  It makes the low end of a volume potentiometer
with an abrupt response easier to adjust.

Available levels are:

* `OFF` — 0 dB (default).
* `-6 dB`.
* `-12 dB`.
* `-18 dB`.

The attenuator uses the dedicated BK4819 `AF Rx Gain-1` stage, preserves the
factory `VOLUME_GAIN` calibration, and is stored in EEPROM.  The spectrum
scanner now changes only the DAC field of `REG_48`, so opening or scanning a
signal no longer clears `AF Att` or overwrites the calibrated receive gain.

Пункт `AF Att` ослабляет весь принимаемый НЧ-сигнал до аналоговой ручки
громкости. Заводская калибровка не изменяется. Для слишком резкой ручки
рекомендуется начать с `-12 dB`, при необходимости выбрать `-18 dB`.

### Firmware builds

Every push is built by GitHub Actions.  The downloadable artifact contains
both `firmware.bin` and `firmware.packed.bin`.  Use `firmware.packed.bin` with
the official updater.

## Upstream project

This repository is a preservation project of the UV K5 v2.1.27 firmware.
It is dedicated to understanding how the radio works and help developers making their own customisations/fixes/etc.
It is by no means fully understood or has all variables/functions properly named, as this is best effort only.
This fork adds the features documented above while retaining the upstream
license and credits.

For improved/better firmware and new features, you can find the following repositories by other collaborators:

* https://github.com/fagci/uv-k5-firmware-fagci-mod
* https://github.com/OneOfEleven/uv-k5-firmware-custom
* https://github.com/Tunas1337/uv-k5-firmware (Check the branches)
* https://github.com/rebezhir/openquack for Russian users

# Compiler

arm-none-eabi GCC version 10.3.1 is recommended, which is the current version on Ubuntu 22.04.03 LTS.
Other versions may generate a flash file that is too big.
You can get an appropriate version from: https://developer.arm.com/downloads/-/gnu-rm

# Building

To build the firmware, you need to fetch the submodules and then run make:
```
git submodule update --init --recursive --depth=1
make
```

# Flashing with the official updater

* Use the firmware.packed.bin file

# Flashing with [k5prog](https://github.com/piotr022/k5prog)

* ./k5prog -F -YYY -b firmware.bin

# Flashing with SWD

* If you own a JLink or compatible device and want to use the Segger software, you can find a flash loader [here](https://github.com/DualTachyon/dp32g030-flash-loader)
* If you want to use OpenOCD instead, you can use run "make flash" off this repo.
* The DP32G030 has flash masking to move the bootloader out of the way. Do not try to flash your own way outside of the above methods or risk losing your bootloader.

# Support

* If you like my work, you can support me through https://ko-fi.com/DualTachyon

# Credits

Many thanks to various people on Telegram for putting up with me during this effort and helping:

* [Mikhail](https://github.com/fagci/)
* [Andrej](https://github.com/Tunas1337)
* [Manuel](https://github.com/manujedi)
* @wagner
* @Lohtse Shar
* [@Matoz](https://github.com/spm81)
* @Davide
* @Ismo OH2FTG
* [OneOfEleven](https://github.com/OneOfEleven)
* and others I forget

# License

Copyright 2023 Dual Tachyon
https://github.com/DualTachyon

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
