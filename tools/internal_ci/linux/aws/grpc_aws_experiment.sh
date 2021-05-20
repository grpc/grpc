#!/bin/bash -e

# This script is responsible for remotely running tests on an ARM instance.
# It should return a status code useful to the kokoro infrastructure.

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
# https://aws.amazon.com/amazon-linux-ami/
AWS_MACHINE_IMAGE=ami-026141f3d5c6d2d0c
AWS_INSTANCE_TYPE=t4g.xlarge
AWS_SECURITY_GROUP=sg-021240e886feba750
AWS_STORAGE_SIZE_GB=60
AWS_DEVICE_MAPPING="DeviceName='/dev/sdb',VirtualName='ephemeral0',Ebs={DeleteOnTermination=True,VolumeSize=${AWS_STORAGE_SIZE_GB},VolumeType='standard'}"

KOKORO_JOB_TAG="{Key='kokoro_job_name',Value='${KOKORO_JOB_NAME}'}"
KOKORO_BUILD_NUM="{Key='kokoro_build_number',Value='${KOKORO_BUILD_NUMBER}'}"
AWS_INSTANCE_TAGS="ResourceType='instance',Tags=[${KOKORO_JOB_TAG},${KOKORO_BUILD_NUM}]"

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
    --block-device-mapping=$AWS_DEVICE_MAPPING \
    --tag-specifications $AWS_INSTANCE_TAGS \
    --region us-east-2 | jq .Instances[0].InstanceId | sed 's/"//g')
echo "instance-id=$ID"
echo "Polling (1m) for instance metadata..."
for i in $(seq 1 6); do
    result=$(aws ec2 describe-instances \
        --instance-id=$ID \
        --region us-east-2 | jq .Reservations[0].Instances[0].NetworkInterfaces[0].Association.PublicIp | sed 's/"//g')
    # Check for an ip address vs error. Finish waiting if an ip address is found.
    echo $result | grep -E "[[:digit:]]+\.[[:digit:]]+\.[[:digit:]]+\.[[:digit:]]+"
    if [[ $? -eq 0 ]]; then
        IP=$result
        break
    fi
    sleep 10s
done
if [ -z "$IP" ]; then
    echo "No instance found after waiting. Exiting now."
    exit 1
fi

SERVER_HOST_KEY_ENTRY="$IP $SERVER_HOST_KEY_ENTRY"
echo $SERVER_HOST_KEY_ENTRY >> ~/.ssh/known_hosts

echo "Polling (2m) for ssh availability..."
for i in $(seq 1 12); do 
    result=$(ssh -i ~/.ssh/temp_client_key ubuntu@$IP "uname -a")
    if [[ $? -eq 0 ]]; then
        SSH_AVAILABLE=1
        break
    fi
    sleep 10s
done
if [ -z "$SSH_AVAILABLE" ]; then
    echo "Instance not available for ssh after waiting. Exiting now."
    exit 1
fi

WORKLOAD=grpc_aws_experiment_remote.sh
REMOTE_SCRIPT_FAILURE=0
echo "Copying to remote instance..."
scp -i ~/.ssh/temp_client_key -r github/grpc ubuntu@$IP:
echo "Beginning CI workload..."
ssh -i ~/.ssh/temp_client_key ubuntu@$IP "bash grpc/tools/internal_ci/linux/aws/$WORKLOAD" || REMOTE_SCRIPT_FAILURE=$?

# Match return value
echo "returning $REMOTE_SCRIPT_FAILURE based on script output"
exit $REMOTE_SCRIPT_FAILURE
