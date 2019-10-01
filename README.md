# BlackHole Virtual Audio Driver v0.2

## Description
BlackHole is a modern MacOS virtual audio driver that allows applications to pass audio to other applications with zero latency.

## Features
- Supports 16 audio channels.
- Customizable to 256+ channels if you think your computer can handle it.
- Supports 44.1kHz, 48kHz, 88.2kHz, 96kHz, 176.4kHz, and 192kHz sample rates.
- No additional driver latency. 

## Easy Installation Instructions
1. Download [BlackHole.vx.x.x.pkg](https://github.com/ExistentialAudio/BlackHole/releases/)
2. Close all running audio applications
3. Right-click on package and select open
4. Open and install package

## Usage
### Record System Audio
1. Open Audio MIDI Setup
2. Right-click on "BlackHole" and select "Use This Device For Sound Output"
3. Open DAW and set input device to "BlackHole" 
4. Set track to input from channel 1-2
5. Play audio from another application and monitor or record in your DAW.

### Record Audio from Standalone Virtual Instrument
1. Set output driver to "BlackHole" in VI
2. Output to any channel
3. Open DAW and set input device to "BlackHole" 
4. Input audio from the corresponding outputs in VI

## Advanced Installation Instruction
1. Build driver in Xcode
2. Copy BlackHole.driver to "/Library⁩/Audio⁩/Plug-Ins⁩/HAL"
3. Restart CoreAudio with terminal command "sudo killall coreaudiod"

## Advanced Customization
1. In "BlackHole.h" change "NUMBER_OF_CHANNELS" to the desired number of channels.
2. Follow Build Installation instructions

## Uninstallation Instructions
1. Delete BlackHole.driver from "/Library⁩/Audio⁩/Plug-Ins⁩/HAL"
2. Restart computer or restart CoreAudio with terminal command "sudo killall coreaudiod"

[Support us on Patreon](https://www.patreon.com/existentialaudio)
