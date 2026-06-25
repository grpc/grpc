#!/bin/bash
_OS=$(grep '^ID=' /etc/os-release 2> /dev/null | sed -E 's/ID=(.*)/\1/')
_VERSION=0
if [[ ${_OS} == "debian" ]] ; then
    _VERSION=$(grep '^VERSION_ID=' /etc/os-release | sed -E 's/VERSION_ID="([^"]+)"/\1/')
fi

if [[ "${_VERSION}" -ge "12" ]]; then
    # Is Debian 12 or higher which comes with cmake >=3.25
    apt-get update && apt-get install -y cmake
elif [[ "${_VERSION}" -eq "11" ]]; then
    # For Debian 11 we use backport.
    echo "deb http://archive.debian.org/debian bullseye-backports main" >> /etc/apt/sources.list
    apt-get update && apt-get install -y cmake -t bullseye-backports cmake
else
    printf "Unsupported OS: (%s %s)\n" ${_OS} ${_VERSION}
    exit 1
fi
