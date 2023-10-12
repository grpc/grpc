# Docker images used for gRPC testing

Most of our linux tests on the CI run under a docker container, since that makes it easier
to maintain the test environment and the dependencies. Having an easily reproducible test
environment also make it easier to reproduce issues we see on CI locally.

The docker image definitions we use live under `tools/dockerfile` directory (with the
exception of `third_party/rake-compiler-dock` docker images).

## Version management

The docker images we use for testing evolve over time (and newer/older versions of it
might not work with newer/older revisions of our code).

For each dockerfile (which is identified by the directory in which is it located),
the "current version" that's being used by testing is determined by the
corresponding `.current_version` file, which contains the full docker image name,
including artifact registry location, docker image name, the current tag and the
SHA256 image digest.

Example:
For `tools/dockerfile/test/cxx_debian11_x64/Dockerfile`, there is a
`tools/dockerfile/test/cxx_debian11_x64.current_version` file which contains info
as follows:
```
us-docker.pkg.dev/grpc-testing/testing-images-public/cxx_debian11_x64:[CURRENT_CHECKSUM]@sha256:[CURRENT_SHA256_DIGEST]
```
This info can be passed directly to `docker run` command to get an environment
that's identical what what we use when testing on CI.

## Updating the images

The authoritative version of docker images we use for testing is stored in artifact registry,
under the repository `us-docker.pkg.dev/grpc-testing/testing-images-public`.

If you've made modifications to a dockerfile, you can upload the new version of the artifact
registry as follows:

If you haven't configured authentication in Docker for us-docker.pkg.dev previously, run:
```
gcloud auth configure-docker us-docker.pkg.dev
gcloud auth login
```

Rebuild the docker images that have been modified locally and upload the docker images to
artifact registry (note that this won't overwrite the "old" versions of the docker image
that are already in artifact registry)
```
# Install qemu, binformat, and configure binfmt interpreters
sudo apt-get install binfmt-support qemu-user-static

# Enable different multi-architecture containers by QEMU with Docker
docker run --rm --privileged multiarch/qemu-user-static --reset -p yes

tools/dockerfile/push_testing_images.sh
```

Build modified docker images locally and don't push to artifact registry. This option is
very useful for quick local experiments. The script is much faster if it doesn't have to
interact with artifact registry:
```
# very useful for local experiments
LOCAL_ONLY_MODE=true tools/dockerfile/push_testing_images.sh
```

## Migrating from dockerhub

In the past, our testing docker images were [hosted on dockerhub](https://hub.docker.com/u/grpctesting),
but we are in the process of migrating them artifact registry now.

This temporary feature might simplify the migration:
```
# try pull existing images from dockerhub instead of building the from scratch locally.
TRANSFER_FROM_DOCKERHUB=true tools/dockerfile/push_testing_images.sh
```
