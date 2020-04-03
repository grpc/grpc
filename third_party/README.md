# Third-party libraries

gRPC depends on several third-party libraries, their source code is available
(usually as a git submodule) in this directory.

## Guidelines on updating submodules

- IMPORTANT: whenever possible, try to only update to a stable release of a library (= not to master / random commit). Depending on unreleased revisions
  makes gRPC installation harder for users, as it forces them to always build the dependency from source and prevents them from using more
  convenient installation channels (linux packages, package managers etc.)

- bazel BUILD uses a different dependency model - whenever updating a submodule, also update the revision in `grpc_deps.bzl` so that bazel and
  non-bazel builds stay in sync (this is actually enforced by a sanity check in some cases)

## Considerations when adding a new third-party dependency

- gRPC C++ needs to stay buildable/installable even if the submodules are not present (e.g. the tar.gz archive with gRPC doesn't contain the submodules),
  assuming that the dependencies are already installed. This is a requirement for being able to provide a reasonable install process (e.g. using cmake)
  and to support package managers for gRPC C++.

- Adding a new dependency is a lot of work (both for us and for the users).
  We currently support multiple build systems (BAZEL, cmake, make, ...) so adding a new dependency usually requires updates in multiple build systems
  (often not trivial). The installation process also needs to continue to work (we do have distrib tests to test many of the possible installation scenarios,
  but they are not perfect). Adding a new dependency also usually affects the installation instructions that need to be updated.
  Also keep in mind that adding a new dependency can be quite disruptive 
  for the users and community - it means that all users will need to update their projects accordingly (for C++ projects often non-trivial) and 
  the community-provided C++ packages (e.g. vcpkg) will need to be updated as well.

## Instructions for updating dependencies

Usually the process is

1. update the submodule to selected commit (see guidance above)
2. update the dependency in `grpc_deps.bzl` to the same commit
3. update `tools/run_tests/sanity/check_submodules.sh` to make the sanity test pass
4. (when needed) run `tools/buildgen/generate_projects.sh` to regenerate the generated files 

Updating some dependencies requires extra care.

### Updating third_party/boringssl-with bazel

- Update the `third_party/boringssl-with-bazel` submodule to the latest `master-with-bazel` branch

- Update boringssl dependency in `grpc_deps.bzl` to the same commit

- Update `tools/run_tests/sanity/check_submodules.sh` with the same commit

- Run `tools/buildgen/generate_projects.sh` to regenerate the generated files

- Run `tools/distrib/generate_grpc_shadow_boringssl_symbol_list.sh`

- Increment the boringssl podspec version number in 
  `templates/src/objective-c/BoringSSL-GRPC.podspec.template` and `templates/gRPC-Core.podspec.template`.
  [example](https://github.com/grpc/grpc/pull/21527/commits/9d4411842f02f167209887f1f3d2b9ab5d14931a)

- Run `tools/buildgen/generate_projects.sh` (yes, again)

### Updating third_party/protobuf

See http://go/grpc-third-party-protobuf-update-instructions (internal only)
