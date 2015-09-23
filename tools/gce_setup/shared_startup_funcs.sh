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
# Contains common funcs shared by instance startup scripts.
#
# The funcs assume that the code is being run on a GCE instance during instance
# startup.

function die() {
  local msg="$0 failed"
  if [[ -n $1 ]]
  then
    msg=$1
  fi
  echo $msg
  exit 1
}

# umount_by_disk_id umounts a disk given its disk_id.
umount_by_disk_id() {
  local disk_id=$1
  [[ -n $disk_id ]] || { echo "missing arg: disk_id" >&2; return 1; }

  # Unmount the disk first
  sudo umount /dev/disk/by-id/google-$disk_id || { echo "Could not unmount /mnt/disk-by-id/google-$disk_id" >&2; return 1; }
}

# check_metadata confirms that the result of curling a metadata url does not
# contain 'Error 404'
check_metadata() {
  local curl_output=$1
  [[ -n $curl_output ]] || { echo "missing arg: curl_output" >&2; return 1; }

  if [[ $curl_output =~ "Error 404" ]]
  then
    return 1
  fi

  return 0
}

# name_this_instance determines the current instance name.
name_this_instance() {
  local the_full_host_name
  the_full_host_name=$(load_metadata "http://metadata/computeMetadata/v1/instance/hostname")
  check_metadata $the_full_host_name || return 1
  local the_instance
  the_instance=$(echo $the_full_host_name | cut -d . -f 1 -) || {
    echo "could not get the instance name from $the_full_host_name" >&2
    return 1
  }

  echo $the_instance
}

# delete_this_instance deletes this GCE instance. (it will shutdown as a result
# of running this cmd)
delete_this_instance() {
  local the_full_zone
  the_full_zone=$(load_metadata "http://metadata/computeMetadata/v1/instance/zone")
  check_metadata $the_full_zone || return 1
  local the_zone
  the_zone=$(echo $the_full_zone | cut -d / -f 4 -) || { echo "could not get zone from $the_full_zone" >&2; return 1; }

  local the_full_host_name
  the_full_host_name=$(load_metadata "http://metadata/computeMetadata/v1/instance/hostname")
  check_metadata $the_full_host_name || return 1
  local the_instance
  the_instance=$(echo $the_full_host_name | cut -d . -f 1 -) || { echo "could not get zone from $the_full_host_name" >&2; return 1; }

  echo "using gcloud compute instances delete to remove: ${the_instance}"
  gcloud compute --quiet instances delete --delete-disks boot --zone $the_zone $the_instance
}

# save_image_info updates the 'images' release info file on GCS.
save_image_info() {
  local image_id=$1
  [[ -n $image_id ]] || { echo "missing arg: image_id" >&2; return 1; }

  local repo_gs_uri=$2
  [[ -n $repo_gs_uri ]] || { echo "missing arg: repo_gs_uri" >&2; return 1; }

  local sentinel="/tmp/$image_id.txt"
  echo $image_id > $sentinel || { echo "could not create /tmp/$image_id.txt" >&2; return 1; }

  local gs_sentinel="$repo_gs_uri/images/info/LATEST"
  gsutil cp $sentinel $gs_sentinel  || { echo "failed to update $gs_sentinel" >&2; return 1; }
}

# creates an image, getting the name and cloud storage uri from the supplied
# instance metadata.
create_image() {
  local image_id
  image_id=$(load_metadata "attributes/image_id")
  [[ -n $image_id ]] || { echo "missing metadata: image_id" >&2; return 1; }

  local repo_gs_uri
  repo_gs_uri=$(load_metadata "attributes/repo_gs_uri")
  [[ -n $repo_gs_uri ]] || { echo "missing metadata: repo_gs_uri" >&2; return 1; }

  local the_project
  the_project=$(load_metadata "http://metadata/computeMetadata/v1/project/project-id")
  check_metadata $the_project || return 1

  sudo gcimagebundle -d /dev/sda -o /tmp/ --log_file=/tmp/$image_id.log || { echo "image creation failed" >&2; return 1; }
  image_path=$(ls /tmp/*.tar.gz)
  image_gs_uri="$repo_gs_uri/images/$image_id.tar.gz"

  # copy the image to cloud storage
  gsutil cp $image_path $image_gs_uri || { echo "failed to save image to $repo_gs_uri/$image_path " >&2; return 1; }
  gcloud compute --project=$the_project images create \
    $image_id --source-uri $image_gs_uri || { echo "failed to register $image_gs_uri as $image_id" >&2; return 1; }

  save_image_info $image_id $repo_gs_uri
}

# load_metadata curls a metadata url
load_metadata() {
  local metadata_root=http://metadata/computeMetadata/v1
  local uri=$1
  [[ -n $uri ]] || { echo "missing arg: uri" >&2; return 1; }

  if [[ $uri =~ ^'attributes/' ]]
  then
    for a in $(curl -H "X-Google-Metadata-Request: True" $metadata_root/instance/attributes/)
    do
      [[ $uri =~ "/$a"$ ]] && { curl $metadata_root/instance/$uri -H "X-Google-Metadata-Request: True"; return; }
    done
  fi

  # if the uri is a full request uri
  [[ $uri =~ ^$metadata_root ]] && { curl $uri -H "X-Google-Metadata-Request: True"; return; }
}

install_python_module() {
  local mod=$1
  [[ -z $mod ]] && { echo "missing arg: mod" >&2; return 1; }

  echo '------------------------------------'
  echo 'Installing: $mod'
  echo '------------------------------------'
  echo
  install_with_apt_get gcc python-dev python-setuptools
  sudo apt-get install -y gcc python-dev python-setuptools
  sudo easy_install -U pip
  sudo pip uninstall -y $mod
  sudo pip install -U $mod
}

install_with_apt_get() {
  local pkgs=$@
  echo '---------------------------'
  echo 'Installing: $pkgs'
  echo '---------------------------'
  echo
  sudo apt-get install -y $pkgs
}

# pulls code from a git repo @HEAD to a local directory, removing the current version if present.
setup_git_dir() {
  local git_http_repo=$1
  [[ -n $git_http_repo ]] || { echo "missing arg: git_http_repo" >&2; return 1; }

  local git_dir=$2
  [[ -n $git_dir ]] || { echo "missing arg: git_dir" >&2; return 1; }

  if [[ -e $git_dir ]]
  then
    rm -fR $git_dir || { echo "could not remove existing repo at $git_dir" >&2; return 1; }
  fi

  local git_user
  git_user=$(load_metadata "http://metadata/computeMetadata/v1/instance/service-accounts/default/email")
  check_metadata $git_user || return 1
  urlsafe_git_user=$(echo $git_user | sed -e s/@/%40/g) || return 1

  local access_token=$(load_metadata "http://metadata/computeMetadata/v1/instance/service-accounts/default/token?alt=text")
  check_metadata $access_token || return 1
  local git_pwd=$(echo $access_token | cut -d' ' -f 2) || return 1

  git clone https://$urlsafe_git_user:$git_pwd@$git_http_repo $git_dir
}

# network_copy copies a file to another gce instance.
network_copy() {
  local the_node=$1
  [[ -n $the_node ]] || { echo "missing arg: the_node" >&2; return 1; }

  local src=$2
  [[ -n $src ]] || { echo "missing arg: src" >&2; return 1; }

  local dst=$3
  [[ -n $dst ]] || { echo "missing arg: dst" >&2; return 1; }

  gcloud compute copy-files --zone=us-central1-b $src $node:$dst
}

# gcs_copy copies a file to a location beneath a root gcs object path.
gcs_copy() {
  local gce_root=$1
  [[ -n $gce_root ]] || { echo "missing arg: gce_root" >&2; return 1; }

  local src=$2
  [[ -n $src ]] || { echo "missing arg: src" >&2; return 1; }

  local dst=$3
  [[ -n $dst ]] || { echo "missing arg: dst" >&2; return 1; }

  gsutil cp $src $gce_root/$dst
}

# find_named_ip finds the external ip address for a given name.
find_named_ip() {
  local name=$1
  [[ -n $name ]] || { echo "missing arg: name" >&2; return 1; }

  gcloud compute addresses list | sed -e 's/ \+/ /g' | grep $name | cut -d' ' -f 3
}

# update_address_to updates this instances ip address to the reserved ip address with a given name
update_address_to() {
  local name=$1
  [[ -n $name ]] || { echo "missing arg: name" >&2; return 1; }

  named_ip=$(find_named_ip $name)
  [[ -n $named_ip ]] || { echo "did not find an address corresponding to $name" >&2; return 1; }

  local the_full_zone
  the_full_zone=$(load_metadata "http://metadata/computeMetadata/v1/instance/zone")
  check_metadata $the_full_zone || return 1
  local the_zone
  the_zone=$(echo $the_full_zone | cut -d / -f 4 -) || {
    echo "could not get zone from $the_full_zone" >&2
    return 1
  }

  local the_full_host_name
  the_full_host_name=$(load_metadata "http://metadata/computeMetadata/v1/instance/hostname")
  check_metadata $the_full_host_name || return 1
  local the_instance
  the_instance=$(echo $the_full_host_name | cut -d . -f 1 -) || {
    echo "could not determine the instance from $the_full_host_name" >&2
    return 1
  }

  gcloud compute instances delete-access-config --zone $the_zone $the_instance || {
    echo "could not delete the access config for $the_instance" >&2
    return 1
  }
  gcloud compute instances add-access-config --zone $the_zone $the_instance --address $named_ip || {
    echo "could not update the access config for $the_instance to $named_ip" >&2
    return 1
  }
}

# grpc_docker_add_docker_group
#
# Adds a docker group, restarts docker, relaunches the docker registry
grpc_docker_add_docker_group() {
  [[ -f /var/log/GRPC_DOCKER_IS_UP ]] || {
    echo "missing file /var/log/GRPC_DOCKER_IS_UP; either wrong machine or still starting up" >&2;
    return 1
  }
  sudo groupadd docker

  local user=$(id -un)
  [[ -n ${user} ]] || { echo 'could not determine the user' >&2; return 1; }
  sudo gpasswd -a ${user} docker
  sudo service docker restart || return 1;
  grpc_docker_launch_registry
}

# grpc_dockerfile_pull <local_docker_parent_dir>
#
# requires: attributes/gs_dockerfile_root is set to cloud storage directory
# containing the dockerfile directory
grpc_dockerfile_pull() {
  local dockerfile_parent=$1
  [[ -n $dockerfile_parent ]] || dockerfile_parent='/var/local'

  local gs_dockerfile_root=$(load_metadata "attributes/gs_dockerfile_root")
  [[ -n $gs_dockerfile_root ]] || { echo "missing metadata: gs_dockerfile_root" >&2; return 1; }

  mkdir -p $dockerfile_parent
  gsutil cp -R $gs_dockerfile_root $dockerfile_parent || {
    echo "Did not copy docker files from $gs_dockerfile_root -> $dockerfile_parent"
    return 1
  }
 }

# grpc_docker_launch_registry
#
# requires: attributes/gs_docker_reg is set to the cloud storage directory to
# use to store docker images
grpc_docker_launch_registry() {
  local gs_docker_reg=$(load_metadata "attributes/gs_docker_reg")
  [[ -n $gs_docker_reg ]] || { echo "missing metadata: gs_docker_reg" >&2; return 1; }

  local gs_bucket=$(echo $gs_docker_reg | sed -r 's|gs://([^/]*?).*|\1|g')
  [[ -n $gs_bucket ]] || {
    echo "could not determine cloud storage bucket from $gs_bucket" >&2;
    return 1
  }

  local  storage_path_env=''
  local image_path=$(echo $gs_docker_reg | sed -r 's|gs://[^/]*(.*)|\1|g' | sed -e 's:/$::g')
  [[ -n $image_path ]] && {
    storage_path_env="-e STORAGE_PATH=$image_path"
  }

  sudo docker run -d -e GCS_BUCKET=$gs_bucket $storage_path_env -p 5000:5000 google/docker-registry
  # wait a couple of minutes max, for the registry to come up
  local is_up=0
  for i in {1..24}
  do
    local secs=`expr $i \* 5`
    echo "is docker registry up? waited for $secs secs ..."
    wget -q localhost:5000 && {
      echo 'docker registry is up!'
      is_up=1
      break
    }
    sleep 5
  done

  [[ $is_up == 0 ]] && {
    echo "docker registry not available after 120 seconds"; return 1;
  } || return 0
}

# grpc_docker_pull_known
#
# This pulls a set of known docker images from a private docker registry to
# the local image cache. It re-labels the images so that FROM in dockerfiles
# used in dockerfiles running on the docker instance can find the images OK.
#
# optional: address of a grpc docker registry, the default is 0.0.0.0:5000
grpc_docker_pull_known() {
  local addr=$1
  [[ -n $addr ]] || addr="0.0.0.0:5000"
  local known="base cxx php_base php ruby_base ruby java_base java go node_base node python_base python csharp_mono_base csharp_mono"
  echo "... pulling docker images for '$known'"
  for i in $known
  do
    echo "<--- grpc/$i"
    sudo docker pull ${addr}/grpc/$i > /dev/null 2>&1 \
      && sudo docker tag ${addr}/grpc/$i grpc/$i || {
      # log and continue
      echo "docker op error:  could not pull ${addr}/grpc/$i"
    }
  done
}

# grpc_dockerfile_build_install
#
# requires: $1 is the label to apply to the docker image
# requires: $2 is a local directory containing a Dockerfile
# requires: there is a docker registry running on 5000, e.g, grpc_docker_launch_registry was run
#
# grpc_dockerfile_install "grpc/image" /var/local/dockerfile/grpc_image
grpc_dockerfile_install() {
  local image_label=$1
  [[ -n $image_label ]] || { echo "$FUNCNAME: missing arg: image_label" >&2; return 1; }
  local docker_img_url=0.0.0.0:5000/$image_label

  local dockerfile_dir=$2
  [[ -n $dockerfile_dir ]] || { echo "missing arg: dockerfile_dir" >&2; return 1; }

  local cache_opt='--no-cache'
  local cache=$3
  [[ $cache == "cache=yes" ]] && { cache_opt=''; }
  [[ $cache == "cache=1" ]] && { cache_opt=''; }
  [[ $cache == "cache=true" ]] && { cache_opt=''; }

  [[ -d $dockerfile_dir ]] || { echo "$FUNCNAME: not a valid dir: $dockerfile_dir"; return 1; }

  # For specific base images, sync private files.
  #
  # - the ssh key, ssh certs and/or service account info.
  [[ $image_label == "grpc/base" ]] && {
    grpc_docker_sync_github_key $dockerfile_dir/.ssh 'base_ssh_key' || return 1;
  }
  [[ $image_label == "grpc/go" ]] && {
    grpc_docker_sync_github_key $dockerfile_dir/.ssh 'go_ssh_key' || return 1;
    grpc_docker_sync_service_account $dockerfile_dir/service_account || return 1;
  }
  [[ $image_label == "grpc/java_base" ]] && {
    grpc_docker_sync_github_key $dockerfile_dir/.ssh 'java_base_ssh_key' || return 1;
  }
  [[ $image_label == "grpc/java" ]] && {
    grpc_docker_sync_service_account $dockerfile_dir/service_account || return 1;
  }
  [[ $image_label == "grpc/ruby" ]] && {
    grpc_docker_sync_roots_pem $dockerfile_dir/cacerts || return 1;
    grpc_docker_sync_service_account $dockerfile_dir/service_account || return 1;
  }
  [[ $image_label == "grpc/node" ]] && {
    grpc_docker_sync_roots_pem $dockerfile_dir/cacerts || return 1;
    grpc_docker_sync_service_account $dockerfile_dir/service_account || return 1;
  }
  [[ $image_label == "grpc/php" ]] && {
    grpc_docker_sync_roots_pem $dockerfile_dir/cacerts || return 1;
    grpc_docker_sync_service_account $dockerfile_dir/service_account || return 1;
  }
  [[ $image_label == "grpc/cxx" ]] && {
    grpc_docker_sync_roots_pem $dockerfile_dir/cacerts || return 1;
    grpc_docker_sync_service_account $dockerfile_dir/service_account || return 1;
  }
  [[ $image_label == "grpc/python" ]] && {
    grpc_docker_sync_roots_pem $dockerfile_dir/cacerts || return 1;
    grpc_docker_sync_service_account $dockerfile_dir/service_account || return 1;
  }
  [[ $image_label == "grpc/csharp_mono" ]] && {
    grpc_docker_sync_roots_pem $dockerfile_dir/cacerts || return 1;
    grpc_docker_sync_service_account $dockerfile_dir/service_account || return 1;
  }

  # For deb builds, copy the distpackages folder into the docker directory so
  # that it can be installed using ADD distpackages distpackages.
  [[ $image_label == "grpc/build_deb" ]] && {
    cp -vR ~/distpackages $dockerfile_dir
  }

  # TODO(temiola): maybe make cache/no-cache a func option?
  sudo docker build --force-rm=true $cache_opt -t $image_label $dockerfile_dir || {
    echo "$FUNCNAME:: build of $image_label <- $dockerfile_dir"
    return 1
  }
  sudo docker tag $image_label $docker_img_url || {
    echo "$FUNCNAME: failed to tag $docker_img_url as $image_label"
    return 1
  }
  sudo docker push $docker_img_url || {
    echo "$FUNCNAME: failed to push $docker_img_url"
    return 1
  }
}

# grpc_dockerfile_refresh
#
# requires: $1 is the label to apply to the docker image
# requires: $2 is a local directory containing a Dockerfile
# requires: there is a docker registry running on 5000, e.g, grpc_docker_launch_registry was run
#
# call-seq:
#   grpc_dockerfile_refresh "grpc/mylabel" /var/local/dockerfile/dir_containing_my_dockerfile
grpc_dockerfile_refresh() {
  grpc_dockerfile_install "$@"
}

# grpc_docker_sync_github_key.
#
# Copies the docker github key from GCS to the target dir
#
# call-seq:
#   grpc_docker_sync_github_key <target_dir>
grpc_docker_sync_github_key() {
  local target_dir=$1
  [[ -n $target_dir ]] || { echo "$FUNCNAME: missing arg: target_dir" >&2; return 1; }

  local key_file=$2
  [[ -n $key_file ]] || { echo "$FUNCNAME: missing arg: key_file" >&2; return 1; }

  # determine the admin root; the parent of the dockerfile root,
  local gs_dockerfile_root=$(load_metadata "attributes/gs_dockerfile_root")
  [[ -n $gs_dockerfile_root ]] || {
    echo "$FUNCNAME: missing metadata: gs_dockerfile_root" >&2
    return 1
  }
  local gcs_admin_root=$(dirname $gs_dockerfile_root)

  # cp the file from gsutil to a known local area
  local gcs_key_path=$gcs_admin_root/github/$key_file
  local local_key_path=$target_dir/github.rsa
  mkdir -p $target_dir || {
    echo "$FUNCNAME: could not create dir: $target_dir" 1>&2
    return 1
  }
  gsutil cp $src $gcs_key_path $local_key_path
}

# grpc_docker_sync_roots_pem.
#
# Copies the root pems from GCS to the target dir
#
# call-seq:
#   grpc_docker_sync_roots_pem <target_dir>
grpc_docker_sync_roots_pem() {
  local target_dir=$1
  [[ -n $target_dir ]] || { echo "$FUNCNAME: missing arg: target_dir" >&2; return 1; }

  # determine the admin root; the parent of the dockerfile root,
  local gs_dockerfile_root=$(load_metadata "attributes/gs_dockerfile_root")
  [[ -n $gs_dockerfile_root ]] || {
    echo "$FUNCNAME: missing metadata: gs_dockerfile_root" >&2
    return 1
  }
  local gcs_admin_root=$(dirname $gs_dockerfile_root)

  # cp the file from gsutil to a known local area
  local gcs_certs_path=$gcs_admin_root/cacerts/roots.pem
  local local_certs_path=$target_dir/roots.pem
  mkdir -p $target_dir || {
    echo "$FUNCNAME: could not create dir: $target_dir" 1>&2
    return 1
  }
  gsutil cp $src $gcs_certs_path $local_certs_path
}

# grpc_docker_sync_service_account.
#
# Copies the service account from GCS to the target dir
#
# call-seq:
#   grpc_docker_sync_service_account <target_dir>
grpc_docker_sync_service_account() {
  local target_dir=$1
  [[ -n $target_dir ]] || { echo "$FUNCNAME: missing arg: target_dir" >&2; return 1; }

  # determine the admin root; the parent of the dockerfile root,
  local gs_dockerfile_root=$(load_metadata "attributes/gs_dockerfile_root")
  [[ -n $gs_dockerfile_root ]] || {
    echo "$FUNCNAME: missing metadata: gs_dockerfile_root" >&2
    return 1
  }
  local gcs_admin_root=$(dirname $gs_dockerfile_root)

  # cp the file from gsutil to a known local area
  local gcs_acct_path=$gcs_admin_root/service_account/stubbyCloudTestingTest-ee3fce360ac5.json
  local local_acct_path=$target_dir/stubbyCloudTestingTest-ee3fce360ac5.json
  mkdir -p $target_dir || {
    echo "$FUNCNAME: could not create dir: $target_dir" 1>&2
    return 1
  }
  gsutil cp $src $gcs_acct_path $local_acct_path
}
