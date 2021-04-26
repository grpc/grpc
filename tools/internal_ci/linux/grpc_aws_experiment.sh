#!/bin/bash -e

# This script is responsible for remotely running tests on an ARM instance.
# It should return a status code useful to the kokoro infrastructure.
# It currently assumes an instance will be selected by the time this script begins running.

if [ -z "$KOKORO_KEYSTORE_DIR" ]; then
    echo "KOKORO_KEYSTORE_DIR is unset. This must be run from kokoro"
    exit 1
fi

AWS_CREDENTIALS=${KOKORO_KEYSTORE_DIR}/73836_grpc_aws_ec2_credentials

## Setup aws cli
curl "https://awscli.amazonaws.com/awscli-exe-linux-x86_64.zip" -o "awscliv2.zip"
unzip awscliv2.zip
sudo ./aws/install 
aws --version

# authenticate with aws cli
mkdir ~/.aws/
echo "[default]" >> ~/.aws/config
ln -s $AWS_CREDENTIALS ~/.aws/credentials

# setup instance 
sudo apt update && sudo apt install -y jq 

# ubuntu 18.04 lts(arm64)
AMI=ami-026141f3d5c6d2d0c
INSTANCE_TYPE=t4g.xlarge
SG=sg-021240e886feba750

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

ID=$(aws ec2 run-instances --image-id $AMI --instance-initiated-shutdown-behavior=terminate \
    --instance-type $INSTANCE_TYPE \
    --security-group-ids $SG \
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

# Sync back sponge_log artifacts (wip)
# echo "looking for sponge logs..."
# find . | grep sponge_log


# Match return value
echo "returning $REMOTE_SCRIPT_FAILURE based on script output"
exit $REMOTE_SCRIPT_FAILURE
