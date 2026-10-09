#!/bin/bash
# Packages an existing Astralay build into a macOS installer.
# Usage: installer/mac/build-pkg.sh [build-dir] [config]
#   build-dir defaults to <repo>/build, config to Release.
# Writes <build-dir>/installer/Astralay-<version>-macOS.pkg.
#
# This only packages: build first, so that every format is present and ad-hoc signed. The package
# itself is unsigned, so Gatekeeper blocks a downloaded copy until its quarantine flag is cleared
# (xattr -c). What it installs carries no quarantine flag and needs nothing further.

set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
build_dir="${1:-$repo/build}"
config="${2:-Release}"
artefacts="$build_dir/Astralay_artefacts/$config"
identifier="me.ironlabs.astralay"

# Package name | bundle, relative to the artefacts folder | where it's installed.
components=(
    "vst3|VST3/Astralay.vst3|/Library/Audio/Plug-Ins/VST3"
    "au|AU/Astralay.component|/Library/Audio/Plug-Ins/Components"
    "clap|CLAP/Astralay.clap|/Library/Audio/Plug-Ins/CLAP"
    "standalone|Standalone/Astralay.app|/Applications"
)

fail() {
    echo "build-pkg: $*" >&2
    exit 1
}

plist() {
    /usr/libexec/PlistBuddy -c "$1" "$2"
}

[ -d "$artefacts" ] || fail "no build found at $artefacts"

# Check everything before packaging anything: a bundle left empty by an interrupted build, or one
# whose signature is broken, would otherwise only show up on the user's machine.
version=""
for component in "${components[@]}"; do
    IFS='|' read -r name bundle location <<< "$component"
    src="$artefacts/$bundle"

    [ -f "$src/Contents/MacOS/Astralay" ] || fail "$bundle is missing or incomplete; build every format first"
    codesign --verify --deep --strict "$src" || fail "$bundle has no valid signature"

    bundle_version="$(plist "Print :CFBundleShortVersionString" "$src/Contents/Info.plist")"
    [ -z "$version" ] || [ "$bundle_version" = "$version" ] || fail "$bundle is version $bundle_version, the others are $version"
    version="$bundle_version"
done

# The oldest macOS the binaries run on, so the installer can refuse anything older.
min_os="$(sed -n 's/^CMAKE_OSX_DEPLOYMENT_TARGET:[A-Z]*=//p' "$build_dir/CMakeCache.txt" 2>/dev/null || true)"
[ -n "$min_os" ] || fail "can't read CMAKE_OSX_DEPLOYMENT_TARGET from $build_dir/CMakeCache.txt"

out_dir="$build_dir/installer"
out="$out_dir/Astralay-$version-macOS.pkg"
work="$out_dir/work"
rm -rf "$work"
mkdir -p "$work/packages" "$work/resources"

for component in "${components[@]}"; do
    IFS='|' read -r name bundle location <<< "$component"
    bundle_name="$(basename "$bundle")"
    root="$work/root/$name"
    scripts="$work/scripts/$name"
    component_plist="$work/$name.plist"
    mkdir -p "$root" "$scripts"

    # ditto keeps the ad-hoc signature intact.
    ditto "$artefacts/$bundle" "$root/$bundle_name"

    # Installer treats bundles as relocatable unless told otherwise: it looks for a copy with the
    # same bundle identifier anywhere on the disk, such as a build folder, and updates that one
    # instead of installing to the plugin folder. All four bundles share one identifier, too.
    # Version checking is off so that an older release can be installed over a newer one.
    pkgbuild --analyze --root "$root" "$component_plist" > /dev/null
    plist "Print :0:RootRelativeBundlePath" "$component_plist" > /dev/null 2>&1 \
        || fail "pkgbuild doesn't see $bundle_name as a bundle"
    # pkgbuild only writes the relocatable key for applications, so replace the keys, not set them.
    for key in BundleIsRelocatable BundleIsVersionChecked; do
        plist "Delete :0:$key" "$component_plist" > /dev/null 2>&1 || true
        plist "Add :0:$key bool false" "$component_plist"
    done

    # Installer merges into a bundle that's already there, so a file dropped from a later release
    # would linger and break the signature. Remove the old copy first.
    {
        echo "#!/bin/sh"
        echo "rm -rf \"$location/$bundle_name\""
        echo "exit 0"
    } > "$scripts/preinstall"
    chmod +x "$scripts/preinstall"

    # macOS caches the list of Audio Units; restarting the registrar makes it notice the new one.
    if [ "$name" = "au" ]; then
        {
            echo "#!/bin/sh"
            echo "killall -9 AudioComponentRegistrar > /dev/null 2>&1"
            echo "exit 0"
        } > "$scripts/postinstall"
        chmod +x "$scripts/postinstall"
    fi

    pkgbuild --quiet \
        --root "$root" \
        --component-plist "$component_plist" \
        --scripts "$scripts" \
        --identifier "$identifier.pkg.$name" \
        --version "$version" \
        --install-location "$location" \
        "$work/packages/Astralay-$name.pkg"
done

# Display the full licence for distributed builds. Other notices are bundled separately.
cp "$repo/LICENSE-AGPL-3.0.txt" "$work/resources/License.txt"

sed -e "s/@VERSION@/$version/g" -e "s/@MIN_OS@/$min_os/g" -e "s/@IDENTIFIER@/$identifier/g" \
    "$here/distribution.xml.in" > "$work/distribution.xml"

rm -f "$out"
productbuild --quiet \
    --distribution "$work/distribution.xml" \
    --package-path "$work/packages" \
    --resources "$work/resources" \
    "$out"

rm -rf "$work"

echo "build-pkg: wrote $out"
