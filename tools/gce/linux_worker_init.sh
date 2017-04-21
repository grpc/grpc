#!/bin/bash
# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Initializes a fresh GCE VM to become a jenkins linux worker.
# You shouldn't run this script on your own, use create_linux_worker.sh
# instead.

set -ex

# Create some swap space
sudo dd if=/dev/zero of=/swap bs=1024 count=10485760
sudo chmod 600 /swap
sudo mkswap /swap
sudo sed -i '$ a\/swap none swap sw 0 0' /etc/fstab
sudo swapon -a

# Typical apt-get maintenance
sudo apt-get update

# Install JRE
sudo apt-get install -y openjdk-8-jre
sudo apt-get install -y unzip lsof

# Install Docker
curl -sSL https://get.docker.com/ | sh

# Setup jenkins user (or the user will already exist bcuz magic)
sudo adduser jenkins --disabled-password || true

# Enable jenkins to use docker without sudo:
sudo usermod -aG docker jenkins

# Use "overlay" storage driver for docker
# see https://github.com/grpc/grpc/issues/4988
printf "{\n\t\"storage-driver\": \"overlay\"\n}" | sudo tee /etc/docker/daemon.json

# Install RVM
# TODO(jtattermusch): why is RVM needed?
gpg --keyserver hkp://keys.gnupg.net --recv-keys 409B6B1796C275462A1703113804BB82D39DC0E3
curl -sSL https://get.rvm.io | bash -s stable --ruby

# Upgrade Linux kernel to 4.9
wget \
  kernel.ubuntu.com/~kernel-ppa/mainline/v4.9.20/linux-headers-4.9.20-040920_4.9.20-040920.201703310531_all.deb \
  kernel.ubuntu.com/~kernel-ppa/mainline/v4.9.20/linux-headers-4.9.20-040920-generic_4.9.20-040920.201703310531_amd64.deb \
  kernel.ubuntu.com/~kernel-ppa/mainline/v4.9.20/linux-image-4.9.20-040920-generic_4.9.20-040920.201703310531_amd64.deb
sudo dpkg -i linux-headers-4.9*.deb linux-image-4.9*.deb
rm linux-*

# Add pubkey of jenkins@grpc-jenkins-master to authorized keys of jenkins@
# This needs to happen as the last step to prevent Jenkins master from connecting
# to a machine that hasn't been properly setup yet.
cat jenkins_master.pub | sudo tee --append ~jenkins/.ssh/authorized_keys

# Restart for docker to pick up the config changes.
echo 'Successfully initialized the linux worker, going for reboot in 10 seconds'
sleep 10

sudo reboot
