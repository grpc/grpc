# Third-party libraries

gRPC depends on several third-party libraries, their source code is available
(usually as a git submodule) in this directory.

## Guidelines on updating dependencies

- IMPORTANT: whenever possible, try to only update to a stable release of a library (= not to master / random commit). Depending on unreleased revisions
  makes gRPC installation harder for users, as it forces them to always build the dependency from source and prevents them from using more
  convenient installation channels (linux packages, package managers etc.)

- To update the dependencies of gRPC, you need to modify the following three key areas.
  - First, for non-Bazel build, update the Git submodule under the `third_party` directory to point to the new commit.
  - Next, for the legacy Bazel WORKSPACE, update the `grpc_deps.bzl` with the new version's URL, prefix, and checksum.
    (This step will be removed once Bazel module migration is complete.)
  - Finally, for modern Bazel, update the `MODULE.bazel` file to have new version.

- Note that all versions used in three area should point to the same commit. There are CI tests enforcing this.

## Considerations when adding a new third-party dependency

- gRPC C++ needs to stay buildable/installable even if the submodules are not present (e.g. the tar.gz archive with gRPC doesn't contain the submodules),
  assuming that the dependencies are already installed. This is a requirement for being able to provide a reasonable install process (e.g. using cmake)
  and to support package managers for gRPC C++.
    - CAVEAT: upb is an exception here because of its lack of cmake support. Therefore, third_party/upb should be present to build gRPC until upb supports it.

- Adding a new dependency is a lot of work (both for us and for the users).
  We currently support multiple build systems (BAZEL, cmake, make, ...) so adding a new dependency usually requires updates in multiple build systems
  (often not trivial). The installation process also needs to continue to work (we do have distrib tests to test many of the possible installation scenarios,
  but they are not perfect). Adding a new dependency also usually affects the installation instructions that need to be updated.
  Also keep in mind that adding a new dependency can be quite disruptive
  for the users and community - it means that all users will need to update their projects accordingly (for C++ projects often non-trivial) and
  the community-provided C++ packages (e.g. vcpkg) will need to be updated as well.

## Checklist for adding a new third-party dependency

**READ THIS BEFORE YOU ADD A NEW DEPENDENCY**

- [ ] Make sure you understand the hidden costs of adding a dependency (see section above) and that you understand the     complexities of updating the build files. Maintenance of the build files isn't for free, so expect to be involved in maintenance tasks, cleanup and support (e.g resolving user bugs) of the build files in the future.

- [ ] Once your change is ready, start an [adhoc run of artifact - packages - distribtests flow](https://fusion.corp.google.com/projectanalysis/summary/KOKORO/prod%3Agrpc%2Fcore%2Fexperimental%2Fgrpc_build_artifacts_multiplatform) and make sure everything passes (for technical reasons, not all the distribtests can run on each PR automatically).

- [ ] Check the impact of the new dependency on the size of our distribution packages (compare BEFORE and AFTER) and post the comparison on your PR (it should not be approved without checking the impact sizes of packages first). The package sizes AFTER can be obtained from the adhoc package build from bullet point above.

## Instructions for updating dependencies

Usually the process is

1. update the submodule to selected commit (see guidance above)
2. update the dependency in `grpc_deps.bzl` to the same commit
3. update the dependency in `MODULE.bazel` (generated from `templates/MODULE.bazel.inja`) to the same version.
   - Bazel modules are versioned by release, not by commit hash. If a specific commit is needed but lacks a corresponding release, you have three options:
     1. Ask the project's maintainer to create a new release.
     2. Request a BCR volunteer to add a new release by filing an issue at [Github](https://github.com/bazelbuild/bazel-central-registry/issues).
        (See [example](https://github.com/bazelbuild/bazel-central-registry/issues/3702))
     3. Do it yourself. This can often be done by copying the configuration from an existing release.
        (See [example](https://github.com/bazelbuild/bazel-central-registry/pull/3683))

4. update `tools/run_tests/sanity/check_submodules.sh` to make the sanity test pass
5. (when needed) run `tools/buildgen/generate_projects.sh` to regenerate the generated files
6. populate the bazel download mirror by running `bazel/update_mirror.sh`

Updating some dependencies requires extra care.

### Updating third_party/abseil-cpp

- Two additional steps should be done before running `generate_projects.sh` above.
  - Running `src/abseil-cpp/preprocessed_builds.yaml.gen.py`.
  - Updating `abseil_version =` scripts in `templates/gRPC-C++.podspec.template` and
    `templates/gRPC-Core.podspec.template`.
- You can see an example of previous [upgrade](https://github.com/grpc/grpc/pull/24270).

### Updating third_party/boringssl-with-bazel (TODO)

NOTE: updating the boringssl dependency is now part of the internal grpc release tooling (see [go/grpc-release](http://go/grpc-release)).
Prefer using the release tooling when possible. The instructions below are provided as a reference and aren't guaranteed to be up-to-date.

- Update the `third_party/boringssl-with-bazel` submodule to the latest [`master-with-bazel`](https://github.com/google/boringssl/tree/master-with-bazel) branch
```
git submodule update --init      # just to start in a clean state
cd third_party/boringssl-with-bazel
git fetch origin   # fetch what's new in the boringssl repository
git checkout origin/master-with-bazel  # checkout the current state of master-with-bazel branch in the boringssl repo
# Note the latest commit SHA on master-with-bazel branch
cd ../..   # go back to grpc repo root
git status   #  will show that there are new commits in third_party/boringssl-with-bazel
git add  third_party/boringssl-with-bazel     # we actually want to update the changes to the submodule
git commit -m "update submodule boringssl-with-bazel with origin/master-with-bazel"   # commit
```

- Update boringssl dependency in `bazel/grpc_deps.bzl` to the same commit SHA as master-with-bazel branch
    - Update `http_archive(name = "boringssl",` section by updating the sha in `strip_prefix` and `urls` fields.
    - Also, set `sha256` field to "" as the existing value is not valid. This will be added later once we know what that value is.

- Update `tools/run_tests/sanity/check_submodules.sh` with the same commit

- Commit these changes `git commit -m "update boringssl dependency to master-with-bazel commit SHA"`

- Run `tools/buildgen/generate_projects.sh` to regenerate the generated files
    - Because `sha256` in `bazel/grpc_deps.bzl` was left empty, you will get a DEBUG msg like this one:
      ```
      Rule 'boringssl' indicated that a canonical reproducible form can be obtained by modifying arguments sha256 = "SHA value"
      ```
    - Commit the regenrated files `git commit -m "regenerate files"`
    - Update `bazel/grpc_deps.bzl` with the SHA value shown in the above debug msg. Commit again `git commit -m "Updated sha256"`

- Run `tools/distrib/generate_boringssl_prefix_header.sh`
    - Commit again `git commit -m "generate boringssl prefix headers"`

- Increment the boringssl podspec version number in
  `templates/src/objective-c/BoringSSL-GRPC.podspec.template` and `templates/gRPC-Core.podspec.template`.
  [example](https://github.com/grpc/grpc/pull/21527/commits/9d4411842f02f167209887f1f3d2b9ab5d14931a)
    - Commit again `git commit -m "Increment podspec version"`

- Run `tools/buildgen/generate_projects.sh` (yes, again)
    - Commit again `git commit -m "Second regeneration"`

- Create a PR with all the above commits.

- Run `bazel/update_mirror.sh` to update GCS mirror.

### Updating third_party/protobuf

Before running `generate_projects`, you need to complete the following preparations.

1. Update the Protobuf Version
  - Modify `third_party/protobuf.patch` with the current version.
  - Update the `protobuf_version` field in `build_handwritten.yaml`.

2. Run Build Scripts

```
tools/distrib/python/make_grpcio_tools.py --cleanup_third_party
tools/bazel build @com_google_protobuf//:protoc //src/compiler:all
src/ruby/pb/generate_proto_ruby.sh
src/php/bin/generate_proto_php.sh
```

After `generate_projects` has finished, run the following updates.

1. Update `third_party/upb`

```
rm -rf third_party/upb/upb
cp -r third_party/protobuf/upb third_party/upb
tools/codegen/core/gen_upb_api.sh
```

2. Update `third_party/utf8_range`

```
rm -rf third_party/utf8_range
cp -r third_party/protobuf/third_party/utf8_range third_party/utf8_range/
```

### Updating third_party/envoy-api or third_party/xds or third_party/cel-spec

Apart from the above steps, please run `tools/codegen/core/gen_upb_api.sh` to regenerate upb files.

### Updating third_party/xxhash

TODO(https://github.com/Cyan4973/xxHash/issues/548): revisit LICENSE
instructions if upstream splits library and CLI.

The upstream xxhash repository contains a bunch of files that we don't want, so
we employ a rather manual update flow to pick up just the bits we care about:

```
git remote add xxhash https://github.com/Cyan4973/xxHash.git
git fetch xxhash
git show xxhash/dev:xxhash.h > third_party/xxhash/xxhash.h
git show xxhash/dev:LICENSE | sed -nE '/^-+$/q;p' > third_party/xxhash/LICENSE
```
