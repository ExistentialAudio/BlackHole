# BlackHole: Virtual Audio Driver
![Platform:macOS](https://img.shields.io/badge/platform-macOS-lightgrey)
![GitHub](https://img.shields.io/github/v/release/ExistentialAudio/BlackHole)
[![GitHub](https://img.shields.io/github/license/ExistentialAudio/BlackHole)](LICENSE)
[![Twitter](https://img.shields.io/badge/Follow%20on%20Twitter-1da1f2)](https://twitter.com/ExistentialAI)
[![Facebook](https://img.shields.io/badge/Like%20on%20Facebook-4267B2)](https://www.facebook.com/Existential-Audio-103423234434751)

BlackHole is a modern MacOS virtual audio driver that allows applications to pass audio to other applications with zero additional latency.

### [Download Installer](https://existential.audio/blackhole/?pk_campaign=github&pk_kwd=readme) 

## Funding
Sponsor: https://github.com/sponsors/ExistentialAudio

## Table of Contents
- [Features](#features)
- [Installation Instructions](#installation-instructions)
- [Uninstallation Instructions](#uninstallation-instructions)
- [User Guides](#user-guides)
- [Developer Guides](#developer-guides)
- [Feature Requests](#feature-requests)
- [FAQ](#faq)
- [Wiki](https://github.com/ExistentialAudio/BlackHole/wiki)

## Features
- Builds 2, 16, 64, 128, 256 audio channels versions
- Customizable channel count, latency, hidden devices
- Customizable mirror device to allow for a hidden input or output
- Supports 8kHz, 16kHz, 44.1kHz, 48kHz, 88.2kHz, 96kHz, 176.4kHz, 192kHz, 352.8kHz, 384kHz, 705.6kHz and 768kHz sample rates
- Zero driver latency
- Compatible with macOS Mavericks (10.9) and newer.
- Builds for Intel and Apple Silicon

![Image of BlackHole Audio Driver](Images/BlackHole.png)

## Installation Instructions

### Option 1: Download Installer
1. [Download the latest installer](https://existential.audio/blackhole/?pk_campaign=github&pk_kwd=readme)
2. Close all running audio applications
3. Open and install package

### Option 2: Install via Homebrew

- 2ch: `brew install blackhole-2ch`
- 16ch: `brew install blackhole-16ch`
- 64ch: `brew install blackhole-64ch`

## Uninstallation Instructions
### Option 1: Use Uninstaller
- [Download BlackHole2ch Uninstaller](https://existential.audio/downloads/BlackHole2chUninstaller.pkg)
- [Download BlackHole16ch Uninstaller](https://existential.audio/downloads/BlackHole16chUninstaller.pkg)
- [Download BlackHole64ch Uninstaller](https://existential.audio/downloads/BlackHole64chUninstaller.pkg)

### Option 2: Manually Uninstall

1. Delete the BlackHold driver with the terminal command:
   
    `rm -R /Library/Audio/Plug-Ins/HAL/BlackHoleXch.driver` 
   
   Be sure to replace `X` with either `2`, `16`, or `64`.
   
   Note that the directory is the root `/Library` not `/Users/user/Library`.
2. Restart CoreAudio with the terminal command:

    `sudo launchctl kickstart -kp system/com.apple.audio.coreaudiod`

For more specific details [visit the Wiki.](https://github.com/ExistentialAudio/BlackHole/wiki/Uninstallation)

## User Guides

### Logic Pro X
- [Logic Pro X to FaceTime](https://existential.audio/howto/StreamFromLogicProXtoFaceTime.php)
- [Logic Pro X to Google Meet](https://existential.audio/howto/StreamFromLogicProXtoGoogleMeet.php)
- [Logic Pro X to Skype](https://existential.audio/howto/StreamFromLogicProXtoSkype.php)
- [Logic Pro X to Zoom](https://existential.audio/howto/StreamFromLogicProXtoZoom.php)

### GarageBand
- [GarageBand to FaceTime](https://existential.audio/howto/StreamFromGarageBandToFaceTime.php)
- [GarageBand to Google Meet](https://existential.audio/howto/StreamFromGarageBandToGoogleMeet.php)
- [GarageBand to Skype](https://existential.audio/howto/StreamFromGarageBandToSkype.php)
- [GarageBand to Zoom](https://existential.audio/howto/StreamFromGarageBandToZoom.php)

### Reaper
- [Reaper to Zoom](https://noahliebman.net/2020/12/telephone-colophon-or-how-i-overengineered-my-call-audio/) by Noah Liebman

### Record System Audio
1. [Setup Multi-Output Device](https://github.com/ExistentialAudio/BlackHole/wiki/Multi-Output-Device)
2. In `Audio Midi Setup` → `Audio Devices` right-click on the newly created Multi-Output and select "Use This Device For Sound Output"
3. Open digital audio workstation (DAW) such as GarageBand and set input device to "BlackHole" 
4. Set track to input from channel 1-2
5. Play audio from another application and monitor or record in your DAW

### Route Audio Between Applications
1. Set output driver to "BlackHole" in sending application
2. Output audio to any channel
3. Open receiving application and set input device to "BlackHole" 
4. Input audio from the corresponding output channels

## Developer Guides

### A license is required for all non GPL-3.0 projects. 
Please support our hard work and continued development. To request a license [contact Existential Audio](mailto:devinroth@existential.audio).

### Installation
To install BlackHole copy the `BlackHoleXch.driver` folder to `/Library/Audio/Plug-Ins/HAL` and restart CoreAudio using `sudo launchctl kickstart -kp system/com.apple.audio.coreaudiod`.

### Customizing BlackHole

The following pre-compiler constants may be used to easily customize a build of BlackHole.

```
kDriver_Name
kPlugIn_BundleID
kPlugIn_Icon

kDevice_Name
kDevice_IsHidden
kDevice_HasInput
kDevice_HasOutput

kDevice2_Name
kDevice2_IsHidden
kDevice2_HasInput
kDevice2_HasOutput

kLatency_Frame_Size
kNumber_Of_Channels
kSampleRates
```

They can be specified at build time with `xcodebuild` using `GCC_PREPROCESSOR_DEFINITIONS`. 

Example. 

```
xcodebuild \
-project BlackHole.xcodeproj \
GCC_PREPROCESSOR_DEFINITIONS='$GCC_PREPROCESSOR_DEFINITIONS 
kSomeConstant=value
```

Be sure to escape any quotation marks when using strings. 

### Renaming BlackHole

To customize BlackHole it is required to change the following constants. 
- kDriver_Name
- kPlugIn_BundleID (note that this must match the target bundleID)
- kPlugIn_Icon

These can specified as pre-compiler constants using ```xcodebuild```.

```
driverName = "BlackHole"
bundleID = "audio.existential.BlackHole"
icon = "BlackHole.icns"

xcodebuild \
-project BlackHole.xcodeproj \
-configuration Release \
PRODUCT_BUNDLE_IDENTIFIER=$bundleID \
GCC_PREPROCESSOR_DEFINITIONS='$GCC_PREPROCESSOR_DEFINITIONS
kDriver_Name=\"'$driverName'\"
kPlugIn_BundleID=\"'$bundleID'\"
kPlugIn_Icon=\"'$icon'\"'
```

### Customizing Channels, Latency, and Sample Rates
`kNumber_Of_Channels` is used to set the number of channels. Be careful when specifying high channel counts. Although BlackHole is designed to be extremely efficient at higher channel counts it's possible that your computer might not be able to keep up. Sample rates play a roll as well. Don't use high sample rates with a high number of channels. Some applications don't know how to handle high channel counts. Proceed with caution.

`kLatency_Frame_Size` is how much time in frames that the driver has to process incoming and outgoing audio. It can be used to delay the audio inside of BlackHole up to a maximum of 65536 frames. This may be helpful if using BlackHole with a high channel count. 

`kSampleRates` set the sample rate or sample rates of the audio device. If using multiple sample rates separate each with a  `,`. Example: `44100, 48000`.

### Mirror Device
By default BlackHole has a hidden mirrored audio device. The devices may be customized using the following constants. 

```
// Original Device
kDevice_IsHidden
kDevice_HasInput
kDevice_HasOutput

// Mirrored Device
kDevice2_IsHidden
kDevice2_HasInput
kDevice2_HasOutput
```

When all are set to true a 2nd BlackHole will show up that works exactly the same. The inputs and outputs are mirrored so the outputs from both devices go to the inputs of both devices.

This is useful if you need a separate device for input and output.

Example

```
// Original Device
kDevice_IsHidden=false
kDevice_HasInput=true
kDevice_HasOutput= false

// Mirrored Device
kDevice2_IsHidden=false
kDevice2_HasInput= false
kDevice2_HasOutput=true
```

In this situation we have two BlackHole devices. One will have inputs only and the other will have outputs only.

One way to use this in projects is to hide the mirrored device and use it behind the scenes. That way the user will see an input only device while routing audio through to the output behind them scenes. 

Hidden audio devices can be accessed using `kAudioHardwarePropertyTranslateUIDToDevice`.

### Continuous Integration / Continuous Deployment
BlackHole can be integrated into your CI/CD. Take a look at https://github.com/ExistentialAudio/BlackHole/blob/master/Installer/create_installer.sh to see how the installer is built, signed and notarized.



## Feature Requests

If you are interested in any of the following features please leave a comment in the linked issue. To request a features not listed please create a new issue.

- [Sync Clock with other Audio Devices](https://github.com/ExistentialAudio/BlackHole/issues/27) in development see v0.3.0
- [Output Blackhole to other Audio Device](https://github.com/ExistentialAudio/BlackHole/issues/40)
- [Add Support for AU Plug-ins](https://github.com/ExistentialAudio/BlackHole/issues/18)
- [Inter-channel routing](https://github.com/ExistentialAudio/BlackHole/issues/13)
- [Record Directly to File](https://github.com/ExistentialAudio/BlackHole/issues/8)
- [Configuration Options Menu](https://github.com/ExistentialAudio/BlackHole/issues/7)
- [Support for Additional Bit Depths](https://github.com/ExistentialAudio/BlackHole/issues/42)

## FAQ

### Why isn't BlackHole showing up in the Applications folder?
BlackHole is a virtual audio loopback driver. It only shows up in `Audio MIDI Setup`, `Sound Preferences`, or other audio applications.

### How can I listen to the audio and use BlackHole at the same time?
[Setup a Multi-Output Device](https://github.com/ExistentialAudio/BlackHole/wiki/Multi-Output-Device).

### How can I change the volume of a Multi-Output device?
Unfortunately macOS does not support changing the volume of a Multi-Output device but you can set the volume of individual devices in Audio MIDI Setup. 

### Why is nothing playing through BlackHole? 
- Check `System Preferences` -> `Security & Privacy` -> `Privacy` -> `Microphone` to make sure your digital audio workstation (DAW) has microphone access. 

- Check that the volume is all the way up on BlackHole input and output in ``Audio Midi Setup``.

- If you are using a multi-output device, due to issues with macOS the Built-in Output must be enabled and listed as the top device in the Multi-Output. [See here for details](https://github.com/ExistentialAudio/BlackHole/wiki/Multi-Output-Device#4-select-output-devices).

### Why is audio glitching after X minutes when using a multi-output or an aggregate?
- You need to enable drift correction for all devices except the clock source device. 

### What Apps Don't Work with Multi-Outputs?
Unfortunately multi-outputs are pretty buggy and some apps just won't work with them at all. Here is a list of known ones. If you have more to add please let me know.
- Apple Podcasts
- Apple Messages
- HDHomeRun

### AirPods with an Aggregate/Multi-Output is not working.
The microphone from AirPods runs at a lower sample rate which means it should not be used as the primary/clock device in an Aggregate or Multi-Output device. The solution is to use your built-in speakers (and just mute them) or BlackHole 2ch as the primary/clock device. BlackHole 16ch will not work as the primary since the primary needs to have 2ch. 

Read the discussion. https://github.com/ExistentialAudio/BlackHole/issues/146

### Can I integrate BlackHole into my app?
BlackHole is licensed under GPL-3.0. You can use BlackHole as long as your app is also licensed as GPL-3.0. For all other applications please [contact Existential Audio directly](mailto:devinroth@existential.audio).
