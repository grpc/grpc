# Copyright 2017 gRPC authors.
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

FROM debian:jessie

# Install JDK 8 and Git
RUN echo oracle-java8-installer shared/accepted-oracle-license-v1-1 select true | /usr/bin/debconf-set-selections && \
  echo "deb http://ppa.launchpad.net/webupd8team/java/ubuntu trusty main" | tee /etc/apt/sources.list.d/webupd8team-java.list && \
  echo "deb-src http://ppa.launchpad.net/webupd8team/java/ubuntu trusty main" | tee -a /etc/apt/sources.list.d/webupd8team-java.list && \
  apt-key adv --keyserver hkp://keyserver.ubuntu.com:80 --recv-keys EEA14886
RUN apt-get update && apt-get -y install \
      git \
      libapr1 \
      oracle-java8-installer \
      && \
    apt-get clean && rm -r /var/cache/oracle-jdk8-installer/
ENV JAVA_HOME /usr/lib/jvm/java-8-oracle
ENV PATH $PATH:$JAVA_HOME/bin

# Install protobuf
RUN apt-get update && apt-get install -y \
      autoconf \
      build-essential \
      curl \
      gcc \
      libtool \
      unzip \
      && \
    apt-get clean
WORKDIR /
RUN git clone https://github.com/google/protobuf.git
WORKDIR /protobuf
RUN git checkout v3.3.1 && \
  ./autogen.sh && \
  ./configure && \
  make && \
  make check && \
  make install

# Install gcloud command line tools
ENV CLOUD_SDK_REPO "cloud-sdk-jessie"
RUN echo "deb http://packages.cloud.google.com/apt $CLOUD_SDK_REPO main" | tee -a /etc/apt/sources.list.d/google-cloud-sdk.list && \
  curl https://packages.cloud.google.com/apt/doc/apt-key.gpg | apt-key add - && \
  apt-get update && apt-get install -y google-cloud-sdk && apt-get clean && \
  gcloud config set component_manager/disable_update_check true

# Install Android SDK
WORKDIR /
RUN mkdir android-sdk
WORKDIR android-sdk
RUN wget -q https://dl.google.com/android/repository/tools_r25.2.5-linux.zip && \
  unzip -qq tools_r25.2.5-linux.zip && \
  rm tools_r25.2.5-linux.zip && \
  echo y | tools/bin/sdkmanager "platforms;android-22" && \
  echo y | tools/bin/sdkmanager "build-tools;25.0.2" && \
  echo y | tools/bin/sdkmanager "extras;android;m2repository" && \
  echo y | tools/bin/sdkmanager "extras;google;google_play_services" && \
  echo y | tools/bin/sdkmanager "extras;google;m2repository" && \
  echo y | tools/bin/sdkmanager "patcher;v4" && \
  echo y | tools/bin/sdkmanager "platform-tools"
ENV ANDROID_HOME "/android-sdk"

# Reset the working directory
WORKDIR /

# Define the default command.
CMD ["bash"]
