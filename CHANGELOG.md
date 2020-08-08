#  BlackHole Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](http://keepachangelog.com/en/1.0.0/)
and this project adheres to [Semantic Versioning](http://semver.org/spec/v2.0.0.html).

## [0.2.7] - 2020-08-08
### Changed
- Improved Logarithmic Volume Control
- Various updates to README. 

### Added
- Added IOMutex to IO operations

## [0.2.6] - 2020-02-09
### Changed
- Fixed BlackHole buffer allocation error when switching audio devices from DAW.
- Fixed BlackHole buffer allocation error when sleeping.
- Audio Midi Setup speaker configuration now saves preferences.

## [0.2.5] - 2019-11-29
### Changed
- Set default volume to 1.0

## [0.2.4] - 2019-11-28
### Added
- Ability to mute and changed volume on input and out of BlackHole. 

## [0.2.3] - 2019-11-22
### Changed
- Display number of channels in audio source name.

## [0.2.2] - 2019-10-02
### Fixed
- Fixed bugs when multiple devices are reading and writing simulaniously.


## [0.2.1] - 2019-09-30
### Changed
- Set deployment target to macOS 10.10 to include Yosemite and Sierra

## [0.2.0] - 2019-09-29
### Added
- Support for 88.2kHz, 96kHz, 176.4kHz and 192kHz
- Sums audio from mutiple sources
- Changelog
- Device Icon

## [0.1.0] - 2019-09-27
### Added
- Ability to pass audio between applications
- Support for 16 channels of audio
- Support for 44.1kHz and 48kHz
