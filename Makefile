
build:
	xcodebuild -project BlackHole.xcodeproj -configuration Release PRODUCT_BUNDLE_IDENTIFIER=$bundleID GCC_PREPROCESSOR_DEFINITIONS='$GCC_PREPROCESSOR_DEFINITIONS'
