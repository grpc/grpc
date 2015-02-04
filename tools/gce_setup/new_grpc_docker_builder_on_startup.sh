#!/bin/bash
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

main() {
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
