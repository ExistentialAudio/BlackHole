#!/bin/bash
set -euo pipefail

devTeamID="Q5C99V536K" # ⚠️ Replace this with your own developer team ID
notarize=true # To skip notarization, set this to false
notarizeProfile="notarize" # ⚠️ Replace this with your own notarytool keychain profile name

# Basic Validation
if [ ! -d BlackHole.xcodeproj ]; then
    echo "This script must be run from the BlackHole repo root folder."
    echo "For example:"
    echo "  cd /path/to/BlackHole"
    echo "  ./Uninstaller/create_uninstaller.sh"
    exit 1
fi

for channels in 2 16 64 128 256; do

	# create script
	echo \
	'#!/bin/bash

file="/Library/Audio/Plug-Ins/HAL/BlackHole'$channels'ch.driver"

if [ -d "$file" ] ; then
    sudo rm -R "$file"
    sudo killall coreaudiod
fi' > Uninstaller/Scripts/postinstall

	chmod 755 Uninstaller/Scripts/postinstall

	# Build .pkg
    packageName='Uninstaller/BlackHole'$channels'ch-Uninstaller.pkg'

    pkgbuild --nopayload --scripts Uninstaller/Scripts --sign $devTeamID --identifier 'audio.existential.BlackHole'$channels'ch.Uninstaller' $packageName

    # Notarize and Staple
    if [ "$notarize" = true ]; then

        # Submit the package for notarization and capture output, also displaying it simultaneously
        output=$(xcrun notarytool submit "$packageName" --progress --wait --keychain-profile "notarize" 2>&1 | tee /dev/tty)

        # Extract the submission ID
        submission_id=$(echo "$output" | grep -o -E 'id: [a-f0-9-]+' | awk '{print $2}' | head -n1)

        if [ -z "$submission_id" ]; then
          echo "Failed to extract submission ID. ❌"
          exit 1
        fi

        # Check the captured output for the "status: Invalid" indicator
        if echo "$output" | grep -q "status: Invalid"; then
          echo "Error detected during notarization: Submission Invalid ❌"

          # Fetch and display notarization logs
          echo -e "\nFetching logs for submission ID: $submission_id"
          xcrun notarytool log --keychain-profile "notarize" "$submission_id"
          exit 1
        else
          echo "Notarization submitted successfully ✅"
        fi

        xcrun stapler staple $packageName
    fi

done

rm Uninstaller/Scripts/postinstall
