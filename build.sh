#!/bin/sh

set -o pipefail
set -e

: ${REALM_CORE_VERSION:=0.95.5}

PATH=/usr/libexec:$PATH

download_core() {
    echo "Downloading dependency: core ${REALM_CORE_VERSION}"
    TMP_DIR="$TMPDIR/core_bin"
    mkdir -p "${TMP_DIR}"
    CORE_TMP_TAR="${TMP_DIR}/core-${REALM_CORE_VERSION}.tar.bz2.tmp"
    CORE_TAR="${TMP_DIR}/core-${REALM_CORE_VERSION}.tar.bz2"
    if [ ! -f "${CORE_TAR}" ]; then
        curl -f -L -s "https://static.realm.io/downloads/core/realm-core-${REALM_CORE_VERSION}.tar.bz2" -o "${CORE_TMP_TAR}" ||
          (echo "Downloading core failed. Please try again once you have an Internet connection." && exit 1)
        mv "${CORE_TMP_TAR}" "${CORE_TAR}"
    fi

    (
        cd "${TMP_DIR}"
        rm -rf core
        tar xjf "${CORE_TAR}"
        mv core core-${REALM_CORE_VERSION}
    )

    rm -rf core-${REALM_CORE_VERSION} core
    mv ${TMP_DIR}/core-${REALM_CORE_VERSION} core
}

case "$1" in

    ######################################
    # Versioning
    ######################################
    "get-version")
        version_file="Realm/Realm-Info.plist"
        echo "$(PlistBuddy -c "Print :CFBundleVersion" "$version_file")"
        exit 0
        ;;

    ######################################
    # CocoaPods
    ######################################
    "cocoapods-setup")
        download_core
        mv core/librealm.a core/librealm-osx.a
        mv core/librealm-ios-bitcode.a core/librealm-ios.a

        # CocoaPods doesn't support multiple header_mappings_dir, so combine
        # both sets of headers into a single directory
        rm -rf include
        # Create uppercase `Realm` header directory for a case-sensitive filesystem.
        # Both `Realm` and `realm` directories are required.
        if [ ! -e core/include/Realm ]; then
            cp -R core/include/realm core/include/Realm
        fi
        cp -R core/include include
        mkdir -p include/Realm
        cp Realm/*.{h,hpp} include/Realm
        cp Realm/ObjectStore/*.hpp include/Realm
        cp Realm/ObjectStore/impl/*.hpp include/Realm
        cp Realm/ObjectStore/impl/apple/*.hpp include/Realm
        # Create lowercase `realm` header directory for a case-sensitive filesystem.
        if [ ! -e include/realm ]; then
            cp -R include/Realm include/realm
        fi
        touch include/Realm/RLMPlatform.h
        ;;
esac
