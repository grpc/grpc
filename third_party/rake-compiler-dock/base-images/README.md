# Building gRPC's Custom Rake Compiler Docker Images

**INTERNAL ONLY**

This document describes how to build custom `rake-compiler-dock` Docker images for gRPC, based on the upstream [rake-compiler-dock repository](https://github.com/rake-compiler/rake-compiler-dock).

## Prerequisites

Set up the required environment variables:

```bash
export GEM_ROOT=<path_to_clone_rake_compiler_dock_repo>
export GRPC_ROOT=<path_to_your_grpc_git_repo>
```

## Step-by-Step Instructions

1. **Generate Dockerfiles for the base images:**

   ```bash
   "${GRPC_ROOT}/third_party/rake-compiler-dock/base-images/helpers.sh" install_docker_files
   ```

2. **Build and publish the base images:**

   ```bash
   "${GRPC_ROOT}/tools/dockerfile/push_testing_images.sh"
   ```

   *Note: This process automatically rewrites the `.current_version` files located under `third_party/rake-compiler-dock/base-images/`.*

3. **Rewrite base image references in gRPC's Docker overlays:**

   ```bash
   "${GRPC_ROOT}/third_party/rake-compiler-dock/base-images/helpers.sh" rewrite_base_images_references
   ```

4. **Rebuild the gRPC CI images:**

   ```bash
   "${GRPC_ROOT}/tools/dockerfile/push_testing_images.sh"
   ```
