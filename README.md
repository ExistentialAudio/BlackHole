# BlackHole Virtual Audio Driver v0.1

[Support us on Patreon](https://www.patreon.com/existentialaudio)

## Description
BlackHole is a modern MacOS virtual audio driver that allows applications to pass audio to other applications with zero latency.

## Features
- Supports 256+ audio channels if your computer can handle it.
- Supports 44.1kHz, 48kHz, 88.2kHz, 96kHz, 176.4kHz, and 192kHz sample rates.
- No additional driver latency. 

## Installation Instructions
1. Build driver in Xcode
2. Copy BlackHole.driver to "/Library⁩/Audio⁩/Plug-Ins⁩/HAL"
3. Restart computer or restart CoreAudio with terminal command "sudo killall coreaudiod"

## Customization
1. In "BlackHole.h" change "NUMBER_OF_CHANNELS" to the desired number of channels.
2. Follow installation instructions
