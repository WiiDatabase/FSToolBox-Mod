FSToolBox Mod
======
FSToolBox is a NAND Browser and Dumper for the Wii and the Wii U's Wii Mode. It lists all directorys and files in the NAND and can dump individual files or the whole directory, including subdirectorys.
    
This is a modification by the WiiDatabase.de Team which cleans up the code a bit, speeds up the start process and navigation a lot and removes unnecessary patches.

## Installation
1. Grab the ZIP from the [releases page](https://github.com/WiiDatabase/FSToolBox-Mod/releases)
2. Extract it to the root of your SD card, so that you have the path "SD://apps/FSToolBox-Mod/boot.dol"
3. Run the homebrew through the homebrew channel and follow the on-screen instructions

## Compilation
1. Run `make` in this repository with libogc and devkitPPC installed

## TODO
- [ ] USB support

## Credits
* Original written by [nickasa](https://code.google.com/archive/p/fstoolbox/source)
* Modified to disable HW_AHBPROT and use IOS58 by [Lupo96](https://code.google.com/archive/p/fstoolbox-ahbprot-mod/)
* Nano for [libruntimeiospatch](https://nanolx.org/wiihomebrew/libruntimeiospatch) - included is a slightly modified version which removes unnecessary patches
