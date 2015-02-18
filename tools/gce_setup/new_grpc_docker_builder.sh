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


# Triggers the build of a GCE 'grpc-docker' instance.
#
# Usage:
# /path/to/new_grpc_docker_builder.sh \
#   [--project <cloud-project-id> | -p<cloud-project-id>] \
#   [--instance <instance-to-create> | -i<instance-to-create>] \
#   [--address <named_cloud_static_ip> | -a<named_cloud_static_ip>]
#
# To run a new docker builder instance.
# $ /path/to/new_grpc_docker_builder.sh -pmy-project -imy-instance -amy-ip
#
# See main() for the full list of flags

function this_dir() {
  SCRIPT_PATH="${BASH_SOURCE[0]}";
  if ([ -h "${SCRIPT_PATH}" ]) then
    while([ -h "${SCRIPT_PATH}" ]) do SCRIPT_PATH=`readlink "${SCRIPT_PATH}"`; done
  fi
  pushd . > /dev/null
  cd `dirname ${SCRIPT_PATH}` > /dev/null
  SCRIPT_PATH=`pwd`;
  popd  > /dev/null
  echo $SCRIPT_PATH
}

source $(this_dir)/compute_extras.sh
source $(this_dir)/grpc_docker.sh

cp_startup_script() {
  local script_dir=$1
  [[ -n $script_dir ]] || { echo "missing arg: script_dir" 1>&2; return 1; }

  local gs_script_root=$2
  [[ -n $gs_script_root ]] || { echo "missing arg: gs_script_root" 1>&2; return 1; }

  local script_path=$3
  [[ -n $script_path ]] || { echo "missing arg: script_name" 1>&2; return 1; }

  local startup_script=$script_dir/$script_path
  local gs_startup_uri=$gs_script_root/$script_path
  gsutil cp $startup_script $gs_startup_uri
}

# add_instance adds a generic instance that runs
# new_grpc_docker_builder_on_startup.sh on startup
add_instance() {
  local project=$1
  [[ -n $project ]] || { echo "missing arg: project" 1>&2; return 1; }
  local gs_admin_root=$2
  [[ -n $gs_admin_root ]] || { echo "missing arg: gs_admin_root" 1>&2; return 1; }
  local instance=$3
  [[ -n $instance ]] || { echo "missing arg: instance" 1>&2; return 1; }
  local zone=$4
  [[ -n $zone ]] || { echo "missing arg: zone" 1>&2; return 1; }
  local address=$5
  [[ -n $address ]] || { echo "missing arg: address" 1>&2; return 1; }

  local script_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
  local gs_script_root="$gs_admin_root/startup"

  local on_startup=new_grpc_docker_builder_on_startup.sh
  local gs_on_startup=$gs_script_root/$on_startup
  cp_startup_script $script_dir $gs_script_root $on_startup || {
    echo "Could not save script to $gs_on_startup" 1>&2
    return 1
  }
  startup_md="startup-script-url=$gs_on_startup"

  local shared_startup=shared_startup_funcs.sh
  local gs_shared_startup=$gs_script_root/$shared_startup
  cp_startup_script $script_dir $gs_script_root $shared_startup || {
    echo "Could not save script to $gs_shared_startup" 1>&2
    return 1
  }
  startup_md+=" shared_startup_script_url=$gs_shared_startup"

  local docker_dir=$(this_dir)/../dockerfile
  grpc_push_dockerfiles $docker_dir $gs_admin_root || return 1;
  startup_md+=" gs_dockerfile_root=$gs_admin_root/dockerfile"
  startup_md+=" gs_docker_reg=$gs_admin_root/docker_images"

  local address_flag=""
  local the_address=$(find_named_ip $address)
  [[ -n $the_address ]] && address_flag="--address $the_address"
  local the_image='container-vm-v20140925'
  local scopes='compute-rw storage-full'
  scopes+=' https://www.googleapis.com/auth/xapi.zoo'
  gcloud --project $project compute instances create $instance \
    $address_flag \
    --image $the_image \
    --image-project google-containers \
    --metadata $startup_md  \
    --machine-type='n1-standard-1' \
    --scopes $scopes \
    --tags grpc testing \
    --zone $zone \
    --boot-disk-size 500GB
}

main() {
    local INSTANCE_NAME="grpc-docker-builder"
    local PROJECT="stoked-keyword-656"
    local GS_ADMIN_ROOT="gs://tmp-grpc-dev/admin"
    local ZONE='asia-east1-a'
    local ADDRESS_NAME='grpc-php-dev-static-1'  # use 'none' if no static ip is needed

    # Parse the options
    opts=`getopt -o a::p::g::i::z:: --long address_name::,project::,gs_admin_root::,instance_name::,zone:: -n $0 -- "$@"`
    eval set -- "$opts"
    while true ; do
      case "$1" in
        -p|--project)
          case "$2" in
            "") shift 2  ;;
             *) PROJECT=$2; shift 2  ;;
          esac ;;
        -a|--address_name)
          case $2 in
            "") shift 2 ;;
            *) ADDRESS_NAME=$2; shift 2 ;;
          esac ;;
        -g|--gs_admin_root)
          case "$2" in
            "") shift 2  ;;
            *) GS_ADMIN_ROOT=$2; shift 2  ;;
          esac ;;
        -i|--instance_name)
          case "$2" in
            "") shift 2  ;;
            *) INSTANCE_NAME=$2; shift 2  ;;
          esac ;;
        -z|--zone)
          case "$2" in
            "") shift 2  ;;
            *) ZONE=$2; shift 2  ;;
          esac ;;
        --) shift ; break ;;
        *) echo "Internal error!" ; exit 1 ;;
      esac
    done

    # verify that the instance does not currently exist
    has_instance $PROJECT $INSTANCE_NAME && remove_instance $PROJECT $INSTANCE_NAME $ZONE
    has_instance $PROJECT $INSTANCE_NAME && { echo "$INSTANCE_NAME already exists" 1>&2; return 1; }

    # N.B the quotes around are necessary to allow cmds with spaces
    add_instance $PROJECT $GS_ADMIN_ROOT $INSTANCE_NAME $ZONE $ADDRESS_NAME
}

set -x
main "$@"
