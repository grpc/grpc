#!/bin/bash -e

# This script is responsible for remotely running tests on an ARM instance.
# It should return a status code useful to the kokoro infrastructure.
# It currently assumes an instance will be selected by the time this script begins running.

if [ -z "$KOKORO_KEYSTORE_DIR" ]; then
    echo "KOKORO_KEYSTORE_DIR is unset. This must be run from kokoro"
    exit 1
fi

IDENTITY=${KOKORO_KEYSTORE_DIR}/73836_grpc_arm_instance_ssh_private_test_key1

if [ -z "$INSTANCE" ]; then
    echo "no INSTANCE set, aborting"
    exit 1
fi

FILE=grpc_aws_experiment_remote.sh
chmod 700 $IDENTITY
REMOTE_SCRIPT_FAILURE=0
ssh -i $IDENTITY -o StrictHostKeyChecking=no ubuntu@$INSTANCE "rm -rf grpc"
scp -i $IDENTITY -o StrictHostKeyChecking=no -r github/grpc ubuntu@$INSTANCE:
ssh -i $IDENTITY -o StrictHostKeyChecking=no ubuntu@$INSTANCE "uname -a; ls -l; bash grpc/tools/internal_ci/linux/$FILE" || REMOTE_SCRIPT_FAILURE=$?

# Sync back sponge_log artifacts (wip)
# echo "looking for sponge logs..."
# find . | grep sponge_log


# Match return value
echo "returning $REMOTE_SCRIPT_FAILURE based on script output"
exit $REMOTE_SCRIPT_FAILURE
