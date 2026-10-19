#!/usr/bin/env bash

set -o xtrace -o errexit -o nounset -o pipefail
readonly currentScriptDir=`dirname "$(realpath -s "${BASH_SOURCE[0]}")"`
readonly buildDir="${currentScriptDir}/build"
readonly version=`v=\`git describe --tags --exact-match HEAD 2>/dev/null || echo "0.0"\`; h=\`git rev-parse --short HEAD\`; echo "${v#v}~${h}"`

build_and_test() {
  local build_type="$1" # Debug or Release
  local target="$2" # linux-gcc or linux-clang

  echo -e "\n\nBuild and test ${target}-$build_type"

  cmake -S "$currentScriptDir" \
        -B "$buildDir/${target}-$build_type" \
        -G "Ninja" \
        -D CMAKE_BUILD_TYPE="$build_type" \
        -D CONSOLE_APP_VERSION="$version"
  cmake --build "$buildDir/${target}-$build_type"
  ctest --test-dir "$buildDir/${target}-$build_type" --output-on-failure
}

export CC=gcc
export CXX=g++
build_and_test Debug linux-gcc
build_and_test Release linux-gcc

export CC=clang
export CXX=clang++
build_and_test Debug linux-clang
build_and_test Release linux-clang

echo -e "\n\nBuild portable binary using Alpine linux"
docker run --rm \
           --pull=always \
           --volume "${currentScriptDir}:/repo" \
           --workdir /repo \
           --env HOST_UID=`id --user` \
           --env HOST_GID=`id --group` \
           --env CONSOLE_APP_VERSION="$version" \
           alpine:latest \
           sh -o xtrace -o errexit -o nounset -o pipefail -c '
             apk add --no-cache build-base linux-headers cmake ninja python3 py3-yaml git
             cmake -S /repo \
                   -B /repo/build/alpine-linux-release \
                   -G Ninja \
                   -D CMAKE_BUILD_TYPE=Release \
                   -D CONSOLE_APP_VERSION="$CONSOLE_APP_VERSION" \
                   -D BUILD_SHARED_LIBS=OFF \
                   -D CMAKE_EXE_LINKER_FLAGS="-static -static-libgcc -static-libstdc++"
             cmake --build /repo/build/alpine-linux-release
             ctest --test-dir /repo/build/alpine-linux-release --output-on-failure
             chown -R "${HOST_UID}:${HOST_GID}" /repo/build/alpine-linux-release'

tar --create \
    --gzip \
    --file "${buildDir}/TrId2Console-linux-x64.tar.gz" \
    --directory "${currentScriptDir}/build/alpine-linux-release" \
    TrId2Console TrId2Database.json

echo "Success"
