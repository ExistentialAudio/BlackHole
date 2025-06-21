#!/bin/bash
set -e

echo "🔨 Building all BlackHole variants..."
echo ""

variants=(0 2 16 64 128 256)

for channels in "${variants[@]}"; do
    echo "📦 Building BlackHole ${channels}ch..."
    
    if [ "$channels" -eq 0 ]; then
        bundle_id="audio.existential.BlackHole0ch"
        build_dir="build_0ch"
        description="Audio termination device"
    else
        bundle_id="audio.existential.BlackHole${channels}ch"
        build_dir="build_${channels}ch"
        description="Audio loopback device"
    fi
    
    xcodebuild \
      -project BlackHole.xcodeproj \
      -configuration Release \
      -target BlackHole \
      CONFIGURATION_BUILD_DIR="$build_dir" \
      PRODUCT_BUNDLE_IDENTIFIER="$bundle_id" \
      CODE_SIGN_IDENTITY="" \
      CODE_SIGNING_REQUIRED=NO \
      "GCC_PREPROCESSOR_DEFINITIONS=\$GCC_PREPROCESSOR_DEFINITIONS kNumber_Of_Channels=$channels kPlugIn_BundleID=\"$bundle_id\" kDriver_Name=\"BlackHole\"" \
      > "$build_dir.log" 2>&1
    
    if [ -d "$build_dir/BlackHole.driver" ]; then
        echo "✅ BlackHole ${channels}ch built successfully ($description)"
    else
        echo "❌ BlackHole ${channels}ch build failed"
        echo "   Check $build_dir.log for details"
    fi
done

echo ""
echo "🎉 Build complete! Built variants:"
for channels in "${variants[@]}"; do
    build_dir="build_${channels}ch"
    if [ -d "$build_dir/BlackHole.driver" ]; then
        echo "   ✅ BlackHole ${channels}ch: $build_dir/BlackHole.driver"
    fi
done

echo ""
echo "📋 To install a specific variant:"
echo "   sudo cp -R build_Xch/BlackHole.driver /Library/Audio/Plug-Ins/HAL/BlackHoleXch.driver"
echo "   sudo launchctl kickstart -kp system/com.apple.audio.coreaudiod"