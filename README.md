# BlackHole Virtual Audio Driver v0.2

[![Build Status](https://travis-ci.com/ExistentialAudio/BlackHole.svg?branch=master)](https://travis-ci.com/ExistentialAudio/BlackHole)

## Description
BlackHole is a modern MacOS virtual audio driver that allows applications to pass audio to other applications with zero additional latency.

- [Features](#features)
- [Easy Installation Instructions](#easy-installation-instructions)
- [Usage Examples]()
- [Advanced Installation Instructions](#advanced-installation-instructions)
- [Advanced Customization](#advanced-customization)
- [FAQ](#faq)
- [Wiki](https://github.com/ExistentialAudio/BlackHole/wiki)

## Features
- Supports 16 audio channels.
- Customizable to 256+ channels if you think your computer can handle it.
- Supports 44.1kHz, 48kHz, 88.2kHz, 96kHz, 176.4kHz, and 192kHz sample rates.
- No additional driver latency. 
- Works on macOS 10.10+ including macOS 10.15 Catalina

![Image of BlackHole Audio Driver](Images/BlackHole.png)

## Easy Installation Instructions
1. Download [BlackHole.vx.x.x.pkg](https://github.com/ExistentialAudio/BlackHole/releases/)
2. Close all running audio applications
3. Right-click on package and select open
4. Open and install package
5. Restart Computer (Catalina Only)

BlackHole is also available using `brew cask install blackhole`

Visit the [Wiki](https://github.com/ExistentialAudio/BlackHole/wiki) for application specific setup.

## Usage Examples
### Record System Audio
1. Open Audio MIDI Setup
2. Right-click on "BlackHole" and select "Use This Device For Sound Output"
3. Open DAW and set input device to "BlackHole" 
4. Set track to input from channel 1-2
5. Play audio from another application and monitor or record in your DAW.

### Route Audio Between Applications
1. Set output driver to "BlackHole" in sending application
2. Output audio to any channel
3. Open receiving application and set input device to "BlackHole" 
4. Input audio from the corresponding output channels

## Advanced Installation Instructions
1. Build driver in Xcode
2. Copy BlackHole.driver to `/Library⁩/Audio⁩/Plug-Ins⁩/HAL`
3. Restart CoreAudio with terminal command `sudo killall coreaudiod`

## Advanced Customization
1. In "BlackHole.h" change `NUMBER_OF_CHANNELS` to the desired number of channels.
2. Follow [Advanced Installation Instructions](#advanced-installation-instructions)

## Uninstallation Instructions
1. Delete BlackHole.driver from `/Library⁩/Audio⁩/Plug-Ins⁩/HAL`
2. Restart computer or restart CoreAudio with terminal command `sudo killall coreaudiod`

## FAQ

### How can I listen to the audio and use BlackHole at the same time?
[Setup a Multi-Output Device](https://github.com/ExistentialAudio/BlackHole/wiki/Multi-Output-Device)

### How can I change the volume of a Multi-Output device?
Unfortunately macOS does not support changing the volume of a Multi-Output device but you can set the volume of individual devices in Audio MIDI Setup. 

### How can I use BlackHole with Audacity in Catalina?
Audacity is not compatible with Catalina. But there might be a [work around](https://www.audacityteam.org/macos-10-15-catalina-is-not-yet-supported-by-audacity/).

### Why is nothing is playing through BlackHole? 
Check `System Preferences` -> `Security & Privacy` -> `Privacy` -> `Microphone` to make sure your DAW has microphone access. 

### You are awesome. Where can I donate?
[Support us on Patreon](https://www.patreon.com/existentialaudio)

Bitcoin:  1DxkhWHfRUBezMNbRM3rDKLbxEi1GVZRXz

Litecoin: LchR249L8aXnDEDToLpPVSJotuvV381Yka
