#!/usr/bin/env bash

echo "<?xml version=\"1.0\" encoding='utf-8'?>
<installer-gui-script minSpecVersion='2'>
    <title>BlackHole: Virtual Audio Driver $ch $version</title>
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
        <line choice=\"audio.existential.BlackHole$ch\"/>
    </choices-outline>
    <choice id=\"audio.existential.BlackHole$ch\" visible='true' title=\"BlackHole $ch\" start_selected='true'>
        <pkg-ref id=\"audio.existential.BlackHole$ch\"/>
    </choice>
    <pkg-ref id=\"audio.existential.BlackHole$ch\" version=\"$version\" onConclusion='none'>BlackHole$ch.$version.pkg</pkg-ref>
</installer-gui-script>" >> distribution.xml