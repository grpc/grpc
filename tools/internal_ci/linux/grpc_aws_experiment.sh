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
# Max allowed lifespan of the AWS instance. After this period of time, the instance will
# self-terminate (delete itself).
AWS_INSTANCE_MAX_LIFESPAN_MINS=120
# increase the size of the root volume so that builds don't run out of disk space
AWS_STORAGE_SIZE_GB=75
AWS_DEVICE_MAPPING="DeviceName='/dev/sda1',Ebs={VolumeSize=${AWS_STORAGE_SIZE_GB}}"

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
echo " - sleep ${AWS_INSTANCE_MAX_LIFESPAN_MINS}m" >> userdata
echo ' - shutdown' >> userdata

ID=$(aws ec2 run-instances --image-id $AWS_MACHINE_IMAGE --instance-initiated-shutdown-behavior=terminate \
    --instance-type $AWS_INSTANCE_TYPE \
    --security-group-ids $AWS_SECURITY_GROUP \
    --user-data file://userdata \
    --block-device-mapping "$AWS_DEVICE_MAPPING" \
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
REMOTE_SCRIPT_EXITCODE=0
echo "Copying workspace to remote instance..."
# use rsync over ssh since it's much faster than scp
time rsync -e "ssh -i ~/.ssh/temp_client_key" -a github/grpc ubuntu@$IP:~/workspace
echo "Beginning CI workload..."
ssh -i ~/.ssh/temp_client_key ubuntu@$IP "uname -a; cd ~/workspace; ls -l; bash grpc/tools/internal_ci/linux/$WORKLOAD" || REMOTE_SCRIPT_EXITCODE=$?

# Regardless of the remote script's result (success or failure), initiate shutdown of AWS instance a minute from now.
# The small delay is useful to make sure the ssh session doesn't hang up on us if shutdown happens too quickly.
echo "Shutting down instance $ID."
ssh -i ~/.ssh/temp_client_key ubuntu@$IP "shutdown +1" || echo "WARNING: Failed to initiate AWS instance shutdown."

# Match exitcode
echo "Exiting with exitcode $REMOTE_SCRIPT_EXITCODE based on remote script output."
exit $REMOTE_SCRIPT_EXITCODE
