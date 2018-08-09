# Overview

This directory contains scripts that facilitate building and running gRPC tests for combinations of language/runtimes (known as matrix).

The setup builds gRPC docker images for each language/runtime and upload it to Google Container Registry (GCR). These images, encapsulating gRPC stack
from specific releases/tag, are used to test version compatiblity between gRPC release versions.

## Step-by-step instructions for adding a GCR image for a new release for compatibility test
We have continuous nightly test setup to test gRPC backward compatibility between old clients and latest server.  When a gRPC developer creates a new gRPC release, s/he is also responsible to add the just-released gRPC client to the nightly test.  The steps are:
- Add (or update) an entry in `./client_matrix.py` file to reference the github tag for the release.
- Build new client docker image(s).  For example, for C and wrapper languages release `v1.9.9`, do
  - `tools/interop_matrix/create_matrix_images.py --git_checkout --release=v1.9.9 --upload_images --language cxx csharp python ruby php`
- Verify that the new docker image was built successfully and uploaded to GCR.  For example,
  - `gcloud beta container images list --repository gcr.io/grpc-testing` shows image repos.
  - `gcloud beta container images list-tags gcr.io/grpc-testing/grpc_interop_java_oracle8` should show an image entry with tag `v1.9.9`.
- Verify the just-created docker client image would pass backward compatibility test (it should).  For example,
  - `gcloud docker -- pull gcr.io/grpc-testing/grpc_interop_java_oracle8:v1.9.9` followed by
  - `docker_image=gcr.io/grpc-testing/grpc_interop_java_oracle8:v1.9.9 tools/interop_matrix/testcases/java__master`
- git commit the change and merge it to upstream/master.
- (Optional) clean up the tmp directory to where grpc source is cloned at `/export/hda3/tmp/grpc_matrix/`.
For more details on each step, refer to sections below.

## Instructions for adding new language/runtimes*
- Create new `Dockerfile.template`, `build_interop.sh.template` for the language/runtime under `template/tools/dockerfile/`.
- Run `tools/buildgen/generate_projects.sh` to create corresponding files under `tools/dockerfile/`.
- Add language/runtimes to `client_matrix.py` following existing language/runtimes examples.
- Run `tools/interop_matrix/create_matrix_images.py` which will build and upload images to GCR.  Unless you are also building images for a gRPC release, make sure not to set `--release` (the default release 'master' is used for testing).

*: Please delete your docker images at https://pantheon.corp.google.com/gcr/images/grpc-testing?project=grpc-testing afterwards.  Permissions to access GrpcTesting project is required for this step.

## Instructions for creating new test cases
- Create test cases by running `LANG=<lang> [RELEASE=<release>] ./create_testcases.sh`.  For example,
  - `LANG=go ./create_testcases.sh` will generate `./testcases/go__master`, which is also a functional bash script.
  - `LANG=go KEEP_IMAGE=1 ./create_testcases.sh` will generate `./testcases/go__master` and keep the local docker image so it can be invoked simply via `./testcases/go__master`.  Note: remove local docker images manually afterwards with `docker rmi <image_id>`.
- Stage and commit the generated test case file `./testcases/<lang>__<release>`.

## Instructions for running test cases against GCR images
- Run `tools/interop_matrix/run_interop_matrix_tests.py`.  Useful options:
  - `--release` specifies a git release tag.  Defaults to `--release=all`.  Make sure the GCR images with the tag have been created using `create_matrix_images.py` above.
  - `--language` specifies a language.  Defaults to `--language=all`.
  For example, To test all languages for all gRPC releases across all runtimes, do `tools/interop_matrix/run_interop_matrix_test.py --release=all`.
- The output for all the test cases is recorded in a junit style xml file (default to 'report.xml').

## Instructions for running test cases against a GCR image manually
- Download docker image from GCR.  For example: `gcloud docker -- pull gcr.io/grpc-testing/grpc_interop_go1.7:master`.
- Run test cases by specifying `docker_image` variable inline with the test case script created above.
For example:
  - `docker_image=gcr.io/grpc-testing/grpc_interop_go1.7:master ./testcases/go__master` will run go__master test cases against `go1.7` with gRPC release `master` docker image in GCR.

Note:
- File path starting with `tools/` or `template/` are relative to the grpc repo root dir.  File path starting with `./` are relative to current directory (`tools/interop_matrix`).
- Creating and referencing images in GCR require read and write permission to Google Container Registry path gcr.io/grpc-testing.
