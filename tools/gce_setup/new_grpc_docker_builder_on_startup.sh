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

# Startup script that initializes a grpc-dev GCE machine.
#
# A grpc-docker GCE machine is based on docker container image.
#
# On startup, it copies the grpc dockerfiles to a local directory, and update its address.

# _load_metadata curls a metadata url
_load_metadata() {
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

_source_gs_script() {
  local script_attr=$1
  [[ -n $script_attr ]] || { echo "missing arg: script_attr" >&2; return 1; }

  local gs_uri=$(_load_metadata "attributes/$script_attr")
  [[ -n $gs_uri ]] || { echo "missing metadata: $script_attr" >&2; return 1; }

  local out_dir='/var/local/startup_scripts'
  local script_path=$out_dir/$(basename $gs_uri)
  mkdir -p $out_dir
  gsutil cp $gs_uri $script_path || {
    echo "could not cp $gs_uri -> $script_path"
    return 1
  }
  chmod a+rwx $out_dir $script_path
  source $script_path
}

# Args:
#   $1: numerator
#   $2: denominator
#   $3: threshold (optional; defaults to $THRESHOLD)
#
# Returns:
#   1 if (numerator / denominator > threshold)
#   0 otherwise
_gce_disk_cmp_ratio() {
  local DEFAULT_THRESHOLD="1.1"
  local numer="${1}"
  local denom="${2}"
  local threshold="${3:-${DEFAULT_THRESHOLD}}"

  if `which python > /dev/null 2>&1`; then
    python -c "print(1 if (1. * ${numer} / ${denom} > ${threshold}) else 0)"
  else
    echo "Can't find python; calculation not done." 1>&2
    return 1
  fi
}

# Repartitions the disk or resizes the file system, depending on the current
# state of the partition table.
#
# Automates the process described in
# - https://cloud.google.com/compute/docs/disks/persistent-disks#repartitionrootpd
_gce_disk_maybe_resize_then_reboot() {
  # Determine the size in blocks, of the whole disk and the first partition.
  local dev_sda="$(fdisk -s /dev/sda)"
  local dev_sda1="$(fdisk -s /dev/sda1)"
  local dev_sda1_start="$(sudo fdisk -l /dev/sda | grep /dev/sda1 | sed -e 's/ \+/ /g' | cut -d' ' -f 3)"

  # Use fdisk to
  # - first see if the partion 1 is using as much of the disk as it should
  # - then to resize the partition if it's not
  #
  # fdisk(1) flags:
  # -c: disable DOS compatibility mode
  # -u: change display mode to sectors (from cylinders)
  #
  # fdisk(1) commands:
  # d: delete partition (automatically selects the first one)
  # n: new partition
  # p: primary
  # 1: partition number
  # $dev_sda1_start: specify the value for the start sector, the default may be incorrect
  # <1 blank lines>: accept the defaults for end sectors
  # w: write partition table
  if [ $(_gce_disk_cmp_ratio "${dev_sda}" "${dev_sda1}") -eq 1 ]; then
    echo "$FUNCNAME: Updating the partition table to use full ${dev_sda} instead ${dev_sda1}"
    cat <<EOF | fdisk -c -u /dev/sda
d
n
p
1
$dev_sda1_start

w
EOF
    echo "$FUNCNAME: ... updated the partition table"
    shutdown -r now
    return 0
  fi

  # After repartitioning, use resize2fs to expand sda1.
  local df_size="$(df -B 1K / | grep ' /$' | sed -e 's/ \+/ /g' | cut -d' ' -f 2)"
  if [ $(_gce_disk_cmp_ratio "${dev_sda}" "${df_size}") -eq 1 ]; then
    echo "$FUNCNAME: resizing the partition to make full use of it"
    resize2fs /dev/sda1
    echo "$FUNCNAME: ... resize completed"
  fi
}

main() {
    _gce_disk_maybe_resize_then_reboot
    local script_attr='shared_startup_script_url'
    _source_gs_script $script_attr || {
      echo "halting, script 'attributes/$script_attr' could not be sourced"
      return 1
    }
    grpc_dockerfile_pull
    chmod -R a+rw /var/local/dockerfile

    # Install git and emacs
    apt-get update && apt-get install -y git emacs || return 1

    # Startup the docker registry
    grpc_docker_launch_registry && grpc_docker_pull_known

    # Add a sentinel file to indicate that startup has completed.
    local sentinel_file=/var/log/GRPC_DOCKER_IS_UP
    touch $sentinel_file
}

set -x
main "$@"
