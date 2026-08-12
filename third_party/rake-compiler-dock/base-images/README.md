# Building gRPC's own rake compiler docker images

** INTERNAL ONLY **

This doc explains how to build rake compiler docker images based on https://github.com/rake-compiler/rake-compiler-dock.

1. Setup environment variables

```
$ export GEM_ROOT=<path to clone rake-compiler-dock repo>
$ export GRPC_ROOT=<path to your grpc git repo>
```

1. Generate docker files for base images.

```
$ third_party/rake-compiler-dock/base-images/helpers.sh install_docker_files
```

1. Build base images

```
$ tools/dockerfile/push_testing_images.sh
```

This rewrites the `.current_version` files under `third_party/rake-compiler-dock/base-images/`

1. Rewrite references to base images in grpc's docker overlays.

```
$ third_party/rake-compiler-dock/base-images/helpers.sh rewrite_base_images_references
```

1. Build the gRPC CI images again

```
$ tools/dockerfile/push_testing_images.sh
```
