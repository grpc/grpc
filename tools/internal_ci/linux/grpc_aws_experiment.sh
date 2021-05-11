#!/usr/bin/env bash
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

# This script is responsible for remotely running tests on an ARM instance.
# It should return a status code useful to the kokoro infrastructure.

# TODO(jtattermusch): make the script safe to run under "set -ex"
set -e

if [ -z "$KOKORO_KEYSTORE_DIR" ]; then
    echo "KOKORO_KEYSTORE_DIR is unset. This must be run from kokoro"
    exit 1
fi

AWS_CREDENTIALS=${KOKORO_KEYSTORE_DIR}/73836_grpc_aws_ec2_credentials

# Setup aws cli
curl "https://awscli.amazonaws.com/awscli-exe-linux-x86_64.zip" -o "awscliv2.zip"
unzip -q awscliv2.zip
sudo ./aws/install 
aws --version

# authenticate with aws cli
mkdir ~/.aws/
echo "[default]" >> ~/.aws/config
ln -s $AWS_CREDENTIALS ~/.aws/credentials

# setup instance 
sudo apt update && sudo apt install -y jq 

# ubuntu 18.04 lts(arm64)
# https://aws.amazon.com/amazon-linux-ami/
AWS_MACHINE_IMAGE=ami-026141f3d5c6d2d0c
AWS_INSTANCE_TYPE=t4g.xlarge
AWS_SECURITY_GROUP=sg-021240e886feba750

ssh-keygen -N '' -t rsa -b 4096 -f ~/.ssh/temp_client_key
ssh-keygen -N '' -t ecdsa -b 256 -f ~/.ssh/temp_server_key
SERVER_PRIVATE_KEY=$(cat ~/.ssh/temp_server_key | sed 's/\(.*\)/    \1/')
SERVER_PUBLIC_KEY=$(cat ~/.ssh/temp_server_key.pub | awk '{print $1 " " $2 " root@localhost"}')
SERVER_HOST_KEY_ENTRY=$(cat ~/.ssh/temp_server_key.pub | awk '{print $1 " " $2}')
CLIENT_PUBLIC_KEY=$(cat ~/.ssh/temp_client_key.pub)

echo '#cloud-config' > userdata
echo 'ssh_authorized_keys:' >> userdata
echo " - $CLIENT_PUBLIC_KEY" >> userdata
echo 'ssh_keys:' >> userdata
echo '  ecdsa_private: |' >> userdata
echo "$SERVER_PRIVATE_KEY" >> userdata
echo "  ecdsa_public: $SERVER_PUBLIC_KEY" >> userdata
echo '' >> userdata
echo 'runcmd:' >> userdata
echo ' - sleep 120m' >> userdata
echo ' - shutdown' >> userdata

ID=$(aws ec2 run-instances --image-id $AWS_MACHINE_IMAGE --instance-initiated-shutdown-behavior=terminate \
    --instance-type $AWS_INSTANCE_TYPE \
    --security-group-ids $AWS_SECURITY_GROUP \
    --user-data file://userdata \
    --region us-east-2 | jq .Instances[0].InstanceId | sed 's/"//g')
echo "instance-id=$ID"
echo "Waiting 1m for instance ip..."
sleep 1m
IP=$(aws ec2 describe-instances \
    --instance-id=$ID \
    --region us-east-2 | jq .Reservations[0].Instances[0].NetworkInterfaces[0].Association.PublicIp | sed 's/"//g')
SERVER_HOST_KEY_ENTRY="$IP $SERVER_HOST_KEY_ENTRY"
echo $SERVER_HOST_KEY_ENTRY >> ~/.ssh/known_hosts
echo "Waiting 2m for instance to initialize..."
sleep 2m

WORKLOAD=grpc_aws_experiment_remote.sh
REMOTE_SCRIPT_FAILURE=0
echo "Copying to remote instance..."
scp -i ~/.ssh/temp_client_key -r github/grpc ubuntu@$IP:
echo "Beginning CI workload..."
ssh -i ~/.ssh/temp_client_key ubuntu@$IP "uname -a; ls -l; bash grpc/tools/internal_ci/linux/$WORKLOAD" || REMOTE_SCRIPT_FAILURE=$?

# Match return value
echo "returning $REMOTE_SCRIPT_FAILURE based on script output"
exit $REMOTE_SCRIPT_FAILURE
