#!/bin/bash
set -euo pipefail

# Exercise the release packaging script without building, signing, notarizing,
# or installing a driver. pkgbuild itself is real so its BOM is authoritative.
if [[ $(uname -s) != Darwin ]]; then
    echo "This packaging regression test requires macOS." >&2
    exit 1
fi

repository_root=$(cd "$(dirname "$0")/.." && pwd)
temporary_root=$(mktemp -d "${TMPDIR:-/tmp}/blackhole-package-permissions.XXXXXX")
fixture="$temporary_root/repository"
stubs="$temporary_root/bin"

cleanup() {
    status=$?
    if [[ $status -eq 0 ]]; then
        rm -rf "$temporary_root"
    else
        echo "Packaging test files retained at $temporary_root" >&2
        if [[ -f "$temporary_root/build.log" ]]; then
            tail -n 80 "$temporary_root/build.log" >&2
        fi
    fi
    exit "$status"
}
trap cleanup EXIT

mkdir -p "$fixture/BlackHole.xcodeproj" "$fixture/Installer" "$stubs"
cp "$repository_root/Installer/create_installer.sh" "$fixture/Installer/"
cp -R "$repository_root/Installer/scripts" "$fixture/Installer/"
cp "$repository_root/VERSION" "$fixture/"

cat > "$stubs/xcodebuild" <<'STUB'
#!/bin/bash
set -euo pipefail
for argument in "$@"; do
    case "$argument" in
        PRODUCT_BUNDLE_IDENTIFIER=*) bundle_id=${argument#*=} ;;
    esac
done
mkdir -p build/BlackHole.driver/Contents/MacOS
chmod 755 build build/BlackHole.driver build/BlackHole.driver/Contents \
    build/BlackHole.driver/Contents/MacOS
cat > build/BlackHole.driver/Contents/Info.plist <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>$bundle_id</string>
<key>CFBundleVersion</key><string>1.0</string>
<key>CFBundlePackageType</key><string>BNDL</string>
<key>CFBundleExecutable</key><string>BlackHole</string>
<key>CFPlugInFactoryID</key><string>e395c745-4eea-4d94-bb92-46224221047c</string>
</dict></plist>
PLIST
printf 'test driver fixture\n' > build/BlackHole.driver/Contents/MacOS/BlackHole
chmod 644 build/BlackHole.driver/Contents/Info.plist
chmod 755 build/BlackHole.driver/Contents/MacOS/BlackHole
STUB

cat > "$stubs/pkgbuild" <<'STUB'
#!/bin/bash
set -euo pipefail
arguments=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --sign) shift 2 ;;
        *) arguments+=("$1"); shift ;;
    esac
done
exec /usr/bin/pkgbuild "${arguments[@]}"
STUB

# Keep the component package for inspection instead of creating a signed product.
cat > "$stubs/productbuild" <<'STUB'
#!/bin/bash
set -euo pipefail
for argument in "$@"; do output=$argument; done
cp BlackHole.pkg "$output"
STUB

for command in codesign xcrun; do
    printf '#!/bin/sh\nexit 0\n' > "$stubs/$command"
done
chmod 755 "$stubs/"*

(
    cd "$fixture"
    umask 077
    PATH="$stubs:$PATH" /bin/sh Installer/create_installer.sh
) > "$temporary_root/build.log" 2>&1

version=$(cat "$fixture/VERSION")
for channels in 2 16 64 128 256; do
    package="$fixture/Installer/BlackHole${channels}ch-$version.pkg"
    expanded="$temporary_root/expanded-${channels}ch"
    /usr/sbin/pkgutil --expand "$package" "$expanded"
    root_mode=$(/usr/bin/lsbom -p fm "$expanded/Bom" | awk '$1 == "." { print $2 }')
    if [[ $root_mode != 40755 ]]; then
        echo "BlackHole${channels}ch payload root mode is $root_mode; expected 40755." >&2
        exit 1
    fi

    # pkgutil expands the script archive without executing either hook.
    for script in preinstall postinstall; do
        cmp "$fixture/Installer/scripts/$script" "$expanded/Scripts/$script"
        if [[ ! -x "$expanded/Scripts/$script" ]]; then
            echo "BlackHole${channels}ch $script is not executable in the package." >&2
            exit 1
        fi
    done
done

echo "All five installer payloads preserve HAL mode 755 under umask 077."
