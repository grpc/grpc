#!/bin/bash

main() {
  # restart builder vm and wait for images to sync to it
  source grpc_docker.sh
  ./new_grpc_docker_builder.sh -igrpc-docker-builder-alt-2 -anone
  cd ../../
  sleep 3600

  # build images for all languages
  languages=(cxx java go ruby node)
  for lan in "${languages[@]}"
  do
    grpc_update_image $lan
  done

  # restart client and server vm and wait for images to sync to them
  cd tools/gce_setup
  ./new_grpc_docker_builder.sh -igrpc-docker-testclients -anone
  ./new_grpc_docker_builder.sh -igrpc-docker-server -anone
  sleep 3600

  # launch images for all languages on  server
  grpc_launch_servers grpc-docker-server
  
}

set -x
main "$@"
