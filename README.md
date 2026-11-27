# N64 SwapDumper

Cartridge-swap-based cartridge dumper for Nintendo 64, requiring only a flash cart. You can dump saves, ROM, or both. Inspired by and based on [sharksaver64](https://github.com/Jhynjhiruu/sharksaver64) by [@Jhynjhiruu](https://github.com/Jhynjhiruu/) [@ppcasm](https://github.com/ppcasm/) [@Modman](https://github.com/RWeick/) 

# How to use
This application works by allowing you to boot with a flash cart, and then swap to another cartridge while it's running. You can then copy data from that cartridge, and swap back to the flash cart to save it. I've only tested this on a SummerCart.

#### Press A: Dump to SD
- This allows you to dump your cartridge ROM and/or save data to the SD card of your flash cart. When selected, you will be asked to press reset and then swap to the cartridge that you want to dump. When you select this, the application will attempt to auto-detect the correct ROM size, but if it's incorrect, you can manually change it. Then you select if you want to dump the ROM, save data, or both. It's recommended that you create the folders "dump" and "dump/saves" on your SD card before attempting to dump your ROM and/or save data. When the available system memory for the ROM buffer is full, it will ask you to swap back to the flash cart and then it writes that portion of the ROM to the SD card. Then you swap back to the cartridge and continue until the entire ROM has been copied. If you are dumping only save data, then you will only need to swap to the cartridge and back to the flash cart once.

#### Press B: Dump to Joybus
- This is a planned feature. It is not functional yet.

#### Press C-Up: Restore Save
- This allows you to restore a save file from SD:/dump/saves. Press left/right to select a save and then press A to continue. You will be asked to press reset and then swap to the cartridge that you want to use. It will only allow you to restore a save where the filename matches the product code in the cartridge header. For example, CZL*.sav for The Legend of Zelda: Ocarina of Time. If you're trying to restore a save that you dumped using this application, it should already have the correct name, but if not, you can try using the dump feature to find the product code for your cartridge and rename your save to match.

#### Press Z+Down: Clear Save
- This allows you to erase the save data on a cartridge. You will be asked to press reset and then swap to the cartridge that you want to erase. It will then ask if you're sure, and then ask you to press Z+L+Start to make sure that this isn't happening accidentally.

## Header CRC check
Before dumping, the cartridge header CRC is checked against the known CIC variants. Some cartridges, such as homebrew built with libdragon's open-source IPL3, have a zero or nonstandard header CRC and still boot fine on hardware. If the check fails, you can press A to retry or Start to skip the check and dump anyway. The full-ROM CRC32 comparison at the end of the dump still verifies that the SD file matches the cartridge.

## ModRetro 64 cartridges
ModRetro 64 cartridges don't work with most standard cartridge readers, but they can be dumped with this application. Tested: Xibalba, Buck Bumble, XenoCrisis, and Extreme-G.
- Xibalba has no header CRC, so you will need to skip the CRC check (see above).
- Carts that don't have a save chip (Buck Bumble, XenoCrisis, Extreme-G) can stop returning ROM data after the save probe, so dump them with **ROM only**. If this happens during a ROM + save dump, the dump stops with an error instead of writing an empty ROM. Power cycle the console before trying again.
- Xibalba's EEPROM save can be dumped normally.
- Check the detected ROM size before dumping. Extreme-G was detected as 8MB even though its data extends to about 21MB, which produced a dump that doesn't boot. If you're unsure, select 64MB; the extra padding at the end can be trimmed off afterwards.

## Disclaimer

THIS SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED. BY USING THIS SOFTWARE YOU ACKNOWLEDGE AND AGREE THAT:

- Hot-swapping cartridges while the Nintendo 64 console is powered on carries inherent risk and may cause permanent damage to the console, cartridges, accessories, or connected hardware.
- You assume all responsibility and liability for any damage, loss, or impairment to your game console, cartridges, controllers, SD cards, accessories, or any other equipment that may result from use of this software.
- The authors and contributors of this software shall not be held liable for any direct, indirect, incidental, special, exemplary, or consequential damages arising in any way from the use of this software.
- You use this software entirely at your own risk.

Human version: While I've tested this repeatedly and caused no damage to any of my hardware or cartridges, it is not recommended that you use this if you are worried about damaging your console. I reckon that it's particularly risky if your system has been modified to remove the region keyed cartridge tray insert, as its absense may make it easier for the cartridge to be inserted out of alignment, causing cartridge pins to connect to the wrong slot pins or bridge them together, etc. It's also not recommended if you use a bare PCB flash cart or game cartridge, or if your situation has any other factors that might allow the cartridge to be inserted crooked, off-center, etc. Use the software at your own risk! You have been warned!

