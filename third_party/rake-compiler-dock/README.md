# Building gRPC's Custom Rake Compiler Docker Images

**INTERNAL ONLY**

This document describes how to build custom `rake-compiler-dock` Docker images for gRPC, based on the upstream [rake-compiler-dock repository](https://github.com/rake-compiler/rake-compiler-dock).

## Prerequisites

Set up the required environment variables:

```bash
export GEM_ROOT=<path_to_clone_rake_compiler_dock_repo>
export GIT_ROOT=<path_to_grpc_git_repo>
```

## Step-by-Step Instructions

### 1. Clone upstream repository

Clone the repository using `umask 0022` because the build process will execute
scripts under `build/` using a different Linux user inside Docker. Therefore, we
need to grant the necessary read/write permissions to other users.

```bash
(umask 0022; git clone https://github.com/rake-compiler/rake-compiler-dock -b v1.12.0 "$GEM_ROOT")
```

### 2. Build images

Apply the customization patch and build the images locally.

```bash
cd "$GEM_ROOT" && git apply "${GIT_ROOT}/third_party/rake-compiler-dock/update_cross_compilers.patch"
bundle config set --local path '.bundle/gems'
bundle install
bundle exec rake build:images
```

### 3. Re-tag customized images

Tag the locally built images from the upstream reference prefix (`ghcr.io/rake-compiler/rake-compiler-dock-image`) to the gRPC public testing images repository prefix (`us-docker.pkg.dev/grpc-testing/testing-images-public/rake-compiler-dock-image`).

```bash
docker image ls --filter "reference=ghcr.io/rake-compiler/rake-compiler-dock-image" --format "{{.Repository}}:{{.Tag}}" | grep '1\.12\.0' | sed -E 's@^[^:]+:@@' | xargs -r -n1 -I{} docker tag ghcr.io/rake-compiler/rake-compiler-dock-image:{} us-docker.pkg.dev/grpc-testing/testing-images-public/rake-compiler-dock-image:{}
```

### 4. Upload customized images

Push the newly tagged images to the remote Google Artifact Registry repository.

```bash
docker image ls --filter "reference=ghcr.io/rake-compiler/rake-compiler-dock-image" --format "{{.Repository}}:{{.Tag}}" | grep '1\.12\.0' | sed -E 's@^[^:]+:@@' | xargs -r -n1 -I{} docker push us-docker.pkg.dev/grpc-testing/testing-images-public/rake-compiler-dock-image:{}
```

### 5. Rebuild CI docker images

Once the images are pushed to upstream, docker will generate a sha256 digest for each image, include it into Dockerfiles to pick up the new base images.

```bash
docker image ls --format "{{.Tag}} {{.Repository}}:{{.Tag}}@{{.Digest}}" \
    us-docker.pkg.dev/grpc-testing/testing-images-public/rake-compiler-dock-image \
    | rg '^1.12.0-mri' \
    | while read -r tag image; do
dockerfile="${GIT_ROOT}/third_party/rake-compiler-dock/rake_${tag#1.12.0-mri-}/Dockerfile"
if [[ -f "$dockerfile" ]]; then
    sed -E -i "s|^FROM [^ ]+\$|FROM ${image}|" "$dockerfile"
fi
done
```

Finally run `push_testing_images.sh` to push new images.

```bash
tools/dockerfile/push_testing_images.sh
```
