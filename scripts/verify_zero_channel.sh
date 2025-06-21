#!/bin/bash

echo "🔍 BlackHole Zero-Channel Verification"
echo "====================================="
echo ""

echo "1. Checking if BlackHole 0ch driver is installed:"
if [ -d "/Library/Audio/Plug-Ins/HAL/BlackHole0ch.driver" ]; then
    echo "   ✅ BlackHole0ch.driver found in system"
    bundle_id=$(grep -A1 CFBundleIdentifier /Library/Audio/Plug-Ins/HAL/BlackHole0ch.driver/Contents/Info.plist | tail -n1 | sed 's/.*<string>\(.*\)<\/string>.*/\1/' 2>/dev/null)
    echo "   📦 Bundle ID: $bundle_id"
else
    echo "   ❌ BlackHole0ch.driver not found"
    echo "   📋 To install: sudo cp -R build/BlackHole.driver /Library/Audio/Plug-Ins/HAL/BlackHole0ch.driver"
    exit 1
fi

echo ""
echo "2. Checking Core Audio recognition:"
if system_profiler SPAudioDataType | grep -q "BlackHole 0ch"; then
    echo "   ✅ BlackHole 0ch is recognized by Core Audio"
    echo ""
    echo "   Device details:"
    system_profiler SPAudioDataType | grep -A8 -B2 "BlackHole 0ch" | sed 's/^/   /'
else
    echo "   ❌ BlackHole 0ch is NOT recognized by Core Audio"
    echo "   🔄 Try restarting Core Audio: sudo launchctl kickstart -kp system/com.apple.audio.coreaudiod"
    echo "   📋 If problems persist, check Console.app for error messages"
fi

echo ""
echo "3. Verification complete!"
echo ""
echo "📖 Usage:"
echo "   - Open Audio MIDI Setup (Applications > Utilities)"
echo "   - Look for 'BlackHole 0ch' device"
echo "   - Applications can select it as output (audio will be discarded)"
echo ""
echo "🎯 Use cases:"
echo "   - Audio software testing without sound output"
echo "   - Screen recording with silent audio track"
echo "   - Applications requiring audio device but no playback needed"