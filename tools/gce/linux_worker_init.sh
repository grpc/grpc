#!/bin/bash
# Copyright 2015, gRPC authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

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
echo 'DOCKER_OPTS="${DOCKER_OPTS} --storage-driver=overlay"' | sudo tee --append /etc/default/docker

# Install RVM
# TODO(jtattermusch): why is RVM needed?
gpg --keyserver hkp://keys.gnupg.net --recv-keys 409B6B1796C275462A1703113804BB82D39DC0E3
curl -sSL https://get.rvm.io | bash -s stable --ruby

# Add pubkey of jenkins@grpc-jenkins-master to authorized keys of jenkins@
# This needs to happen as the last step to prevent Jenkins master from connecting
# to a machine that hasn't been properly setup yet.
cat jenkins_master.pub | sudo tee --append ~jenkins/.ssh/authorized_keys

# Restart for docker to pickup the config changes.
echo 'Successfully initialized the linux worker, going for reboot in 10 seconds'
sleep 10

sudo reboot
