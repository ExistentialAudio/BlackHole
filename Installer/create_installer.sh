#!/usr/bin/env zsh



# create installer for different channel versions

for channels in 2 16 64 128 256
do

ch=$channels"ch"
driverName="BlackHole"
version=v$(head -n 1 VERSION)
bundleID="audio.existential.$driverName$ch"

# Build
xcodebuild \
-project BlackHole.xcodeproj \
-configuration Release \
-target BlackHole CONFIGURATION_BUILD_DIR=build \
PRODUCT_BUNDLE_IDENTIFIER=$bundleID \
GCC_PREPROCESSOR_DEFINITIONS='$GCC_PREPROCESSOR_DEFINITIONS kNumber_Of_Channels='$channels' kPlugIn_BundleID=\"'$bundleID'\" kDriver_Name=\"'$driverName'\"'

mkdir installer/root
mv build/BlackHole.driver installer/root/$driverName$ch.driver
rm -r build

# Sign
codesign --force --deep --options runtime --sign Q5C99V536K Installer/root/$driverName$ch.driver

# Create package with pkgbuild
chmod 755 Installer/Scripts/preinstall
chmod 755 Installer/Scripts/postinstall

pkgbuild --sign "Q5C99V536K" --root Installer/root --scripts Installer/Scripts --install-location /Library/Audio/Plug-Ins/HAL Installer/BlackHole.pkg
rm -r Installer/root

# Create installer with productbuild
cd Installer

echo "<?xml version=\"1.0\" encoding='utf-8'?>
<installer-gui-script minSpecVersion='2'>
    <title>$driverName: Virtual Audio Driver $ch $version</title>
    <welcome file='welcome.html'/>
    <license file='../LICENSE'/>
    <conclusion file='conclusion.html'/>
    <domains enable_anywhere='false' enable_currentUserHome='false' enable_localSystem='true'/>
    <pkg-ref id=\"$bundleID\"/>
    <options customize='never' require-scripts='false' hostArchitectures='x86_64,arm64'/>
    <volume-check>
        <allowed-os-versions>
            <os-version min='10.9'/>
        </allowed-os-versions>
    </volume-check>
    <choices-outline>
        <line choice=\"$bundleID\"/>
    </choices-outline>
    <choice id=\"$bundleID\" visible='true' title=\"$driverName $ch\" start_selected='true'>
        <pkg-ref id=\"$bundleID\"/>
    </choice>
    <pkg-ref id=\"$bundleID\" version=\"$version\" onConclusion='none'>BlackHole.pkg</pkg-ref>
</installer-gui-script>" >> distribution.xml


productbuild --sign "Q5C99V536K" --distribution distribution.xml --resources . --package-path BlackHole.pkg $driverName$ch.$version.pkg
rm distribution.xml
rm -f BlackHole.pkg

# Notarize
xcrun notarytool submit $driverName$ch.$version.pkg --team-id Q5C99V536K --progress --wait --keychain-profile "Notarize"

xcrun stapler staple $driverName$ch.$version.pkg

cd ..

done
