#!/usr/bin/env bash

ch=2ch
version=v0.4.0

chmod 755 generate_distribution.sh
./generate_distribution.sh

codesign --force --deep --options runtime --sign Q5C99V536K root/BlackHole$ch.driver

chmod 755 Scripts/preinstall
chmod 755 Scripts/postinstall

sudo pkgbuild --sign "Q5C99V536K" --root root --scripts Scripts --install-location /Library/Audio/Plug-Ins/HAL BlackHole$ch.$version.pkg

productbuild --sign "Q5C99V536K" --distribution distribution.xml --resources . --package-path BlackHole$ch.$version.pkg build/BlackHole$ch.$version.pkg 

xcrun altool --notarize-app -f build/BlackHole$ch.$version.pkg --primary-bundle-id audio.existential.BlackHole$ch --username devinroth@existential.audio -progress -wait

sudo xcrun stapler staple build/BlackHole$ch.$version.pkg

rm distribution.xml
rm BlackHole$ch.$version.pkg
