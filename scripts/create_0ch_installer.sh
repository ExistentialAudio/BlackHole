#!/bin/bash
set -euo pipefail

# Creates installer for BlackHole 0-channel variant only
# This is separate from the main installer to allow optional distribution
# Run this script from the BlackHole repo's root directory.

driverName="BlackHole"
devTeamID="Q5C99V536K" # ⚠️ Replace this with your own developer team ID  
notarize=false # Set to true if you have notarization setup
notarizeProfile="notarize" # ⚠️ Replace this with your own notarytool keychain profile name

############################################################################

# Basic Validation
if [ ! -d BlackHole.xcodeproj ]; then
    echo "This script must be run from the BlackHole repo root folder."
    echo "For example:"
    echo "  cd /path/to/BlackHole"
    echo "  ./scripts/create_0ch_installer.sh"
    exit 1
fi

version=`cat VERSION`

# Version Validation
if [ -z "$version" ]; then
    echo "Could not find version number. VERSION file is missing from repo root or is empty."
    exit 1
fi

echo "Building BlackHole 0ch installer (version $version)..."

channels=0
ch="${channels}ch"
driverVariantName="$driverName$ch"
bundleID="audio.existential.$driverVariantName"

# Build
echo "Building driver..."
xcodebuild \
  -project BlackHole.xcodeproj \
  -configuration Release \
  -target BlackHole CONFIGURATION_BUILD_DIR=build \
  PRODUCT_BUNDLE_IDENTIFIER=$bundleID \
  CODE_SIGN_IDENTITY="" \
  CODE_SIGNING_REQUIRED=NO \
  "GCC_PREPROCESSOR_DEFINITIONS=\$GCC_PREPROCESSOR_DEFINITIONS kNumber_Of_Channels=$channels kPlugIn_BundleID=\"$bundleID\" kDriver_Name=\"$driverName\""

# Generate a new UUID
uuid=$(uuidgen)
awk '{sub(/e395c745-4eea-4d94-bb92-46224221047c/,"'$uuid'")}1' build/BlackHole.driver/Contents/Info.plist > Temp.plist
mv Temp.plist build/BlackHole.driver/Contents/Info.plist

mkdir -p Installer/root
driverBundleName=$driverVariantName.driver
mv build/BlackHole.driver Installer/root/$driverBundleName
rm -rf build

echo "Creating installer package..."

# Create package with pkgbuild
chmod 755 Installer/Scripts/preinstall
chmod 755 Installer/Scripts/postinstall

pkgbuild \
  --root Installer/root \
  --scripts Installer/Scripts \
  --install-location /Library/Audio/Plug-Ins/HAL \
  "Installer/$driverName.pkg"
rm -rf Installer/root

# Create installer with productbuild
cd Installer

echo "<?xml version=\"1.0\" encoding='utf-8'?>
<installer-gui-script minSpecVersion='2'>
    <title>$driverName: Audio Termination Device ($ch) $version</title>
    <welcome file='welcome.html'/>
    <license file='../LICENSE'/>
    <conclusion file='conclusion.html'/>
    <domains enable_anywhere='false' enable_currentUserHome='false' enable_localSystem='true'/>
    <pkg-ref id=\"$bundleID\"/>
    <options customize='never' require-scripts='false' hostArchitectures='x86_64,arm64'/>
    <volume-check>
        <allowed-os-versions>
            <os-version min='10.10'/>
        </allowed-os-versions>
    </volume-check>
    <choices-outline>
        <line choice=\"$bundleID\"/>
    </choices-outline>
    <choice id=\"$bundleID\" visible='true' title=\"$driverName $ch (Audio Termination)\" start_selected='true'>
        <pkg-ref id=\"$bundleID\"/>
    </choice>
    <pkg-ref id=\"$bundleID\" version=\"$version\" onConclusion='RequireRestart'>$driverName.pkg</pkg-ref>
</installer-gui-script>" > distribution.xml

# Build
installerPkgName="$driverVariantName-$version.pkg"
productbuild \
  --distribution distribution.xml \
  --resources . \
  --package-path $driverName.pkg $installerPkgName
rm distribution.xml
rm -f $driverName.pkg

echo "✅ BlackHole 0ch installer created: Installer/$installerPkgName"
echo ""
echo "📋 This installer creates an audio termination device (/dev/null for audio)"
echo "   that appears as 'BlackHole 0ch' in Audio MIDI Setup"
echo ""
echo "⚠️  Note: This is an experimental feature for development and testing use"

cd ..