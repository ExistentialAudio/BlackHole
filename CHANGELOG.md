#  BlackHole Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](http://keepachangelog.com/en/1.1.0/)
and this project adheres to [Semantic Versioning](http://semver.org/spec/v2.0.0.html).

### Feature Requests

- Add support for additional virtual formats. 24-bit, 16-bit.
- Sync BlackHole audio clock with any audio device.
- Create multi-output / aggregate device with installer.
- Keep track of which apps are connected to the driver.


## [Unreleased]

### Changed

## [0.6.1] - 2025-02-06
- Updated installer to force a computer reboot as recommended by Apple.
- Updated create_installer.sh script.

### Changed 


## [0.6.0] - 2024-03-22

## Added
- Added precompiler constant for kCanBeDefaultDevice and kCanBeDefaultSystemDevice.

## Changed
- Updated postinstall script to use 'kill' instead of 'kickstart'.
- Updated strings for model name, 


## [0.5.1] - 2023-11-06

### Changed

- Improved installer build script.
- Bumped minimum required operating system to macOS 10.10 Yosemite.
- Update kAudioDevicePropertyDeviceIsRunning separately for device1 and device2

## [0.5.0] - 2023-02-10

### Changed

- Various typo fixes.

### Added

- kObjectID_Pitch_Adjust and kObjectID_ClockSource to adjust clock speed.

## [0.4.1] - 2023-02-10

### Changed

- Merged BlackHole.h into BlackHole.c for easier testing.
- Fixed control size bugs.

### Added

- Added BlackHoleTests target and relevant files.

## [0.4.0] - 2021-06-10

### Added

- Hidden duplicate device.
- Ability to easily modify device streams. 
- Builds multiple versions.
- create_installer.sh to easily build multiple channel versions. 

### Changed

- Fix potential memory leak.
- Fix dropouts when experiencing minor loads. 

## [0.3.0] - 2021-12-07

### Added

- Sample rates: 8kHz, 16kHz, 352.8kHz, 384kHz, 705.6kHz, 768kHz.

### Changed

- Improved performance.
- Fixed various bugs.
- Renamed constants and variables for consistency.
- Connect input and output volume.
- Connect input and output mute. 

## [0.2.10] - 2021-08-21

### Changed

- Increased internal buffer size.
- Change kDataSource_NumberItems to zero.

## [0.2.9] - 2021-1-27

### Changed

- Fix clock bug. Fixes issues with BlackHole crashing on Apple Silicon.

## [0.2.8] - 2020-12-26

### Added

- Support for Apple Silicon.

### Changed

- Set deployment target to macOS 10.9. 
- Fixed bug where there is a loud pop when audio starts.
- Fix bug that caused crashes in certain situations. (Issue #206)
- Disable Volume and Mute controls on input. They are only needed on the output. 
- Fix clock bug.
- Automatically change UIDs to include the number of channels. Makes it easier to build and install multiple versions. Ex: BlackHole2ch_UID

## [0.2.7] - 2020-08-08

### Changed

- Improved Logarithmic Volume Control.
- Various updates to README. 

### Added

- Added IOMutex to IO operations.

## [0.2.6] - 2020-02-09

### Changed

- Fixed BlackHole buffer allocation error when switching audio devices from DAW.
- Fixed BlackHole buffer allocation error when sleeping.
- Audio Midi Setup speaker configuration now saves preferences.

## [0.2.5] - 2019-11-29

### Changed

- Set default volume to 1.0.

## [0.2.4] - 2019-11-28

### Added

- Ability to mute and changed volume on input and out of BlackHole. 

## [0.2.3] - 2019-11-22

### Changed

- Display number of channels in audio source name.

## [0.2.2] - 2019-10-02

### Fixed

- Fixed bugs when multiple devices are reading and writing simultaneously.

## [0.2.1] - 2019-09-30

### Changed

- Set deployment target to macOS 10.10 to include Yosemite and Sierra

## [0.2.0] - 2019-09-29

### Added

- Support for 88.2kHz, 96kHz, 176.4kHz and 192kHz.
- Sums audio from multiple sources.
- Changelog.
- Device Icon.

## [0.1.0] - 2019-09-27

### Added

- Ability to pass audio between applications.
- Support for 16 channels of audio.
- Support for 44.1kHz and 48kHz.
