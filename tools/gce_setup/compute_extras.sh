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


# Bash funcs shared that combine common gcutil actions into single commands

# remove_instance removes a named instance
#
# remove_instance <project> <instance_name> [<zone>="us-central1-b"]
remove_instance() {
  local project=$1
  [[ -n $project ]] || {
    echo "$FUNCNAME: missing arg: project" 1>&2
    return 1
  }
  local an_instance=$2
  [[ -n $an_instance ]] || {
    echo "$FUNCNAME: missing arg: an_instance" 1>&2
    return 1
  }
  local zone=$3
  [[ -n $zone ]] || zone="us-central1-b"

  gcloud --project $project --quiet \
    compute instances delete $an_instance  --zone=$zone
}

# has_instance checks if a project contains a named instance
#
# has_instance <project> <instance_name>
has_instance() {
  local project=$1
  [[ -n $project ]] || {
    echo "$FUNCNAME: missing arg: project" 1>&2
    return 1
  }
  local checked_instance=$2
  [[ -n $checked_instance ]] || {
    echo "$FUNCNAME: missing arg: checked_instance" 1>&2
    return 1
  }

  instances=$(gcloud --project $project compute instances list \
    | sed -e 's/ \+/ /g' | cut -d' ' -f 1)
  for i in $instances
  do
    if [[ $i == $checked_instance ]]
    then
      return 0
    fi
  done

  return 1
}

# find_network_ip finds the ip address of a instance if it is present in the project.
#
# find_network_ip <project> <instance_name>
find_network_ip() {
  local project=$1
  [[ -n $project ]] || {
    echo "$FUNCNAME: missing arg: project" 1>&2
    return 1
  }
  local checked_instance=$2
  [[ -n $checked_instance ]] || {
    echo "$FUNCNAME: missing arg: checked_instance" 1>&2
    return 1
  }

  has_instance $project $checked_instance || return 1
  gcloud --project $project compute instances list \
    | grep -e "$checked_instance\s" | sed -e 's/ \+/ /g' | cut -d' ' -f 4
}

# delete_disks deletes a bunch of disks matching a pattern
#
# delete_disks <project> <disk_pattern>
delete_disks() {
  local project=$1
  [[ -n $project ]] || {
    echo "$FUNCNAME: missing arg: project" 1>&2
    return 1
  }
  local disk_pattern=$2
  [[ -n $disk_pattern ]] || {
    echo "$FUNCNAME: missing arg: disk_pattern" 1>&2
    return 1
  }

  trash_disks=$(gcloud --project=$project compute disks list \
    | sed -e 's/ \+/ /g' | cut -d' ' -f 1 | grep $disk_pattern)
  [[ -n $trash_disks ]] && gcloud --project $project \
    --quiet compute disks delete $trash_disks
}

# has_firewall checks if a project contains a named firewall
#
# has_firewall <project> <checked_firewall>
has_firewall() {
  local project=$1
  [[ -n $project ]] || {
    echo "$FUNCNAME: missing arg: project" 1>&2
    return 1
  }
  local checked_firewall=$2
  [[ -n $checked_firewall ]] || {
    echo "$FUNCNAME: missing arg: checked_firewall" 1>&2
    return 1
  }

  instances=$(gcloud --project $project compute firewall-rules list \
    | sed -e 's/ \+/ /g' | cut -d' ' -f 1)
  for i in $instances
  do
    if [[ $i == $checked_firewall ]]
    then
      return 0
    fi
  done

  return 1
}

# remove_firewall removes a named firewall from a project.
#
# remove_firewall <project> <checked_firewall>
remove_firewall() {
  local project=$1
  [[ -n $project ]] || {
    echo "$FUNCNAME: missing arg: project" 1>&2
    return 1
  }
  local a_firewall=$2
  [[ -n $a_firewall ]] || {
    echo "$FUNCNAME: missing arg: a_firewall" 1>&2
    return 1
  }

  gcloud --project $project --quiet compute firewall-rules delete $a_firewall
}

# has_network checks if a project contains a named network
#
# has_network <project> <checked_network>
has_network() {
  local project=$1
  [[ -n $project ]] || {
    echo "$FUNCNAME: missing arg: project" 1>&2
    return 1
  }
  local checked_network=$2
  [[ -n $checked_network ]] || {
    echo "$FUNCNAME: missing arg: checked_network" 1>&2
    return 1
  }

  instances=$(gcloud --project $project compute networks list \
    | sed -e 's/ \+/ /g' | cut -d' ' -f 1)
  for i in $instances
  do
    if [[ $i == $checked_network ]]
    then
      return 0
    fi
  done

  return 1
}

# maybe_setup_dev_network adds a network with the given name with firewalls
# useful to development
#
# - All machines can accessed internally and externally over SSH (port 22)
# - All machines can access one another other the internal network
# - All machines can be accessed externally via port 80, 443, 8080 and 8443
maybe_setup_dev_network() {
  local name=$1
  [[ -n $name ]] || {
    echo "$FUNCNAME: missing arg: network name" 1>&2
    return 1
  }

  local project=$2
  [[ -n $project ]] || {
    echo "$FUNCNAME: missing arg: project" 1>&2
    return 1
  }

  has_network $project $name || {
    echo "creating network '$name'" 1>&2
    gcloud compute --project $project networks create $name || return 1
  }

  # allow instances on the network to connect to each other internally
  has_firewall $project "$name-ssh" || {
    echo "adding firewall '$name-ssh'" 1>&2
    gcloud compute --project $project firewall-rules create "$name-ssh" \
      --network $name  \
      --allow tcp:22 || return 1;
  }

 # allow instances on the network to connect to each other internally
  has_firewall $project "$name-internal" || {
    echo "adding firewall '$name-internal'" 1>&2
    gcloud compute --project $project firewall-rules create "$name-internal" \
      --network $name  \
      --source-ranges 10.0.0.0/16 --allow tcp udp icmp || return 1;
  }

  # allow instances on the network to be connected to from external ips on
  # specific ports
  has_firewall $project "$name-external" || {
    echo "adding firewall '$name-external'" 1>&2
    gcloud compute --project $project firewall-rules create "$name-external" \
      --network $name  \
      --allow tcp:80 tcp:8080 tcp:443 tcp:8443 || return 1;
  }
}

# maybe_remove_dev_network removes a network set up by maybe_setup_dev_network
maybe_remove_dev_network() {
  local name=$1
  [[ -n $name ]] || {
    echo "$FUNCNAME: missing arg: network name" 1>&2
    return 1
  }

  local project=$2
  [[ -n $project ]] || {
    echo "$FUNCNAME: missing arg: project" 1>&2
    return 1
  }

  has_network $project $name || {
    echo "network $name is not present"
    return 0
  }
  for i in $(gcloud compute firewall-rules list \
    | grep "$name-" | cut -d' ' -f 1)
  do
    gcloud compute --quiet firewall-rules delete $i || return 1;
  done
  gcloud compute --quiet networks delete $name
}

# find_named_ip finds the external ip address for a given name.
#
# find_named_ip <named-ip-address>
find_named_ip() {
  local name=$1
  [[ -n $name ]] || { echo "$FUNCNAME: missing arg: name" 1>&2; return 1; }
  [[ $name == 'none' ]] && return 0;

  gcloud compute addresses list | sed -e 's/ \+/ /g' \
    | grep $name | cut -d' ' -f 3
}
