# Overview

This directory contains scripts that facilitate building and running gRPC tests for combinations of language/runtimes (known as matrix).

The setup builds gRPC docker images for each language/runtime and upload it to Google Container Registry (GCR). These images, encapsulating gRPC stack
from specific releases/tag, are used to test version compatiblity between gRPC release versions.

## Instructions for creating GCR images
- Edit  `./client_matrix.py` to include desired gRPC release.
- Run `tools/interop_matrix/create_matrix_images.py`.  Useful options:
  - `--git_checkout` enables git checkout grpc release branch/tag.
  - `--release` specifies a git release tag.  Make sure it is a valid tag in the grpc github rep.
  - `--language` specifies a language.
  For example, To build all languages for all gRPC releases across all runtimes, do `tools/interop_matrix/create_matrix_images.py --git_checkout --release=all`.
- Verify the newly created docker images are uploaded to GCR.  For example:
  - `gcloud beta container images list --repository gcr.io/grpc-testing` shows image repos.
  - `gcloud beta container images list-tags gcr.io/grpc-testing/grpc_interop_go1.7` show tags for a image repo.

## Instructions for adding new language/runtimes*
- Create new `Dockerfile.template`, `build_interop.sh.template` for the language/runtime under `template/tools/dockerfile/`.
- Run `tools/buildgen/generate_projects.sh` to create corresponding files under `tools/dockerfile/`.
- Add language/runtimes to `client_matrix.py` following existing language/runtimes examples.
- Run `tools/interop_matrix/create_matrix_images.py` which will build and upload images to GCR.  Unless you are also building images for a gRPC release, make sure not to set `--gcr_tag` (the default tag 'master' is used for testing).

*: Please delete your docker images at https://pantheon.corp.google.com/gcr/images/grpc-testing?project=grpc-testing afterwards.  Permissions to access GrpcTesting project is required for this step.

## Instructions for creating new test cases
- Create test cases by running `LANG=<lang> [RELEASE=<release>] ./create_testcases.sh`.  For example,
  - `LANG=go ./create_testcases.sh` will generate `./testcases/go__master`, which is also a functional bash script.
  - `LANG=go KEEP_IMAGE=1 ./create_testcases.sh` will generate `./testcases/go__master` and keep the local docker image so it can be invoked simply via `./testcases/go__master`.  Note: remove local docker images manually afterwards with `docker rmi <image_id>`.
- Stage and commit the generated test case file `./testcases/<lang>__<release>`.

## Instructions for running test cases against GCR images
- Run `tools/interop_matrix/run_interop_matrix_tests.py`.  Useful options:
  - `--release` specifies a git release tag.  Defaults to `--release=master`.  Make sure the GCR images with the tag have been created using `create_matrix_images.py` above.
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
