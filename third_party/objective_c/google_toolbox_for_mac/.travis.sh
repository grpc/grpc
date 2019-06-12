#!/usr/bin/env bash

set -eu

if [[ "$#" -ne 2 ]]; then
  echo "Usage: $0 {iOS|OSX} {Debug|Release|Both}"
  exit 10
fi

BUILD_MODE="$1"
BUILD_CFG="$2"

# Report then run the build
RunXcodeBuild() {
  echo xcodebuild "$@"
  xcodebuild "$@"
}

CMD_BUILDER=(
)
XCODE_ACTIONS=(
  build test
)

case "${BUILD_MODE}" in
  iOS)
    CMD_BUILDER+=(
        -project GTMiPhone.xcodeproj
        -scheme "GTMiPhone"
        -destination "platform=iOS Simulator,name=iPhone 6,OS=latest"
    )
    ;;
  OSX)
    CMD_BUILDER+=(
        -project GTM.xcodeproj
        -scheme "GTM"
    )
    ;;
  *)
    echo "Unknown BUILD_MODE: ${BUILD_MODE}"
    exit 11
    ;;
esac

case "${BUILD_CFG}" in
  Debug|Release)
    RunXcodeBuild "${CMD_BUILDER[@]}" -configuration "${BUILD_CFG}" "${XCODE_ACTIONS[@]}"
    ;;
  Both)
    RunXcodeBuild "${CMD_BUILDER[@]}" -configuration Debug "${XCODE_ACTIONS[@]}"
    RunXcodeBuild "${CMD_BUILDER[@]}" -configuration Release "${XCODE_ACTIONS[@]}"
    ;;
  *)
    echo "Unknown BUILD_CFG: ${BUILD_CFG}"
    exit 12
    ;;
esac
