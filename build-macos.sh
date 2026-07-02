#!/usr/bin/env bash

set -o xtrace -o errexit -o nounset -o pipefail
readonly currentScriptDir=`cd "$(dirname "${BASH_SOURCE[0]}")" && pwd`
readonly buildDir="${currentScriptDir}/build"
readonly version=`v=\`git describe --tags --exact-match HEAD 2>/dev/null || echo "0.0"\`; h=\`git rev-parse --short HEAD\`; echo "${v#v}~${h}"`

build_and_test() {
  local build_type="$1" # Debug or Release
  local target="$2" # macos-arm64 or macos-x86_64

  echo -e "\n\nBuild and test ${target}-${build_type}"

  cmake -S "$currentScriptDir" \
        -B "$buildDir/${target}-${build_type}" \
        -G "Ninja" \
        -D CMAKE_BUILD_TYPE="$build_type" \
        -D CONSOLE_APP_VERSION="$version"
  cmake --build "$buildDir/${target}-${build_type}"
  ctest --test-dir "$buildDir/${target}-$build_type"
}

build_and_test Debug "macos-arm64"
build_and_test Release "macos-arm64"

tar --create \
    --gzip \
    --file "${buildDir}/TrId2Console-macos-arm64.tar.gz" \
    --directory "${currentScriptDir}/build/macos-arm64-Release" \
    TrId2Console

echo "Success"
