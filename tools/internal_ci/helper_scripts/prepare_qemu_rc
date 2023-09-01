#!/bin/bash
# Copyright 2021 The gRPC Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Source this rc script to setup and configure qemu userspace emulator on kokoro worker so that we can seamlessly emulate processes running
# inside docker containers.

# show pre-existing qemu registration
cat /proc/sys/fs/binfmt_misc/qemu-aarch64

# Kokoro ubuntu1604 workers have already qemu-user and qemu-user-static packages installed, but it's and old version that:
# * prints warning about some syscalls (e.g "qemu: Unsupported syscall: 278")
# * doesn't register with binfmt_misc with the persistent ("F") flag we need (see below)
#
# To overcome the above limitations, we use the https://github.com/multiarch/qemu-user-static
# docker image to provide a new enough version of qemu-user-static and register it with
# the desired binfmt_misc flags. The most important flag we need is "F" (set by "--persistent yes"),
# which allows the qemu-aarch64-static binary to be loaded eagerly at the time of registration with binfmt_misc.
# That way, we can emulate aarch64 binaries running inside docker containers transparently, without needing the emulator
# binary to be accessible from the docker image we're emulating.
# Note that on newer distributions (such as glinux), simply "apt install qemu-user-static" is sufficient
# to install qemu-user-static with the right flags.
docker run --rm --privileged multiarch/qemu-user-static:5.2.0-2 --reset --credential yes --persistent yes

# Print current qemu reqistration to make sure everything is setup correctly.
cat /proc/sys/fs/binfmt_misc/qemu-aarch64
