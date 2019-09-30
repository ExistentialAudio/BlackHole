# BlackHole Virtual Audio Driver v0.2

## Description
BlackHole is a modern MacOS virtual audio driver that allows applications to pass audio to other applications with zero latency.

## Features
- Supports 16 audio channels.
- Customizable to 256+ channels if you're computer can handle it.
- Supports 44.1kHz, 48kHz, 88.2kHz, 96kHz, 176.4kHz, and 192kHz sample rates.
- No additional driver latency. 

## Installation Instructions
1. Download [BlackHole.vx.x.x.pkg](https://github.com/ExistentialAudio/BlackHole/releases/)
2. Close all running audio applications
3. Right-click on package and select open
4. Open and install package

## Build Installation Instructions
1. Build driver in Xcode
2. Copy BlackHole.driver to "/Library⁩/Audio⁩/Plug-Ins⁩/HAL"
3. Restart CoreAudio with terminal command "sudo killall coreaudiod"

## Customization
1. In "BlackHole.h" change "NUMBER_OF_CHANNELS" to the desired number of channels.
2. Follow Build Installation instructions

## Uninstallation Instructions
1. Delete BlackHole.driver from "/Library⁩/Audio⁩/Plug-Ins⁩/HAL"
2. Restart computer or restart CoreAudio with terminal command "sudo killall coreaudiod"

[Support us on Patreon](https://www.patreon.com/existentialaudio)
