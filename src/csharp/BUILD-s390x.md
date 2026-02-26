# Building gRPC C# Tools on s390x (IBM System z)

This document describes how to build gRPC C# Tools on s390x architecture.

> **Note**: s390x support requires building some components from source due to lack of prebuilt binaries for this architecture. This is a one-time setup process.

## Prerequisites

### 1. Install Build Dependencies
```bash
sudo dnf install -y git wget java-21-openjdk-devel python3 zip unzip gcc cmake gcc-c++ automake jq patch
```

### 2. Install .NET SDK
```bash
# Install .NET 6.0 or later
sudo dnf install -y dotnet-sdk-6.0
```

### 3. Build Bazel from Source

s390x does not have prebuilt Bazel binaries, so you must build Bazel from source:

```bash
# Get Bazel version required by gRPC
BAZEL_VERSION=$(cat .bazelversion)

# Create build directory
mkdir -p ../bazel-build
cd ../bazel-build

# Download Bazel source
wget https://github.com/bazelbuild/bazel/releases/download/${BAZEL_VERSION}/bazel-${BAZEL_VERSION}-dist.zip
unzip bazel-${BAZEL_VERSION}-dist.zip
rm bazel-${BAZEL_VERSION}-dist.zip

# Build Bazel
env EXTRA_BAZEL_ARGS="--tool_java_runtime_version=local_jdk" bash ./compile.sh

# Install Bazel system-wide or add to PATH
sudo cp output/bazel /usr/local/bin/
# OR add to PATH: export PATH=$PATH:$(pwd)/output/

cd ../grpc
```

## Building gRPC C# Tools

### 1. Build Native Components
```bash
# Build protoc and gRPC compiler plugins
bazel build @com_google_protobuf//:protoc //src/compiler:all
```

### 2. Build C# Package
```bash
cd src/csharp
OPENSSL_ENABLE_SHA1_SIGNATURES=1 ./build_nuget.sh
```

### 3. Verify Build
The build will create:
- `../../artifacts/Grpc.Tools.X.Y.Z-s390x.nupkg` - NuGet package for s390x
- Native binaries in `protoc_plugins/protoc_linux_s390x/`

## Architecture-Specific Behavior

### Bazel Wrapper
The `tools/bazel` wrapper automatically detects s390x and:
1. Shows informational messages about s390x limitations
2. Falls back to system `bazel` if available
3. Provides helpful links for building Bazel from source

### Build Scripts
The C# build process automatically:
1. Enables single-platform build mode on s390x
2. Uses locally built binaries instead of prebuilt artifacts
3. Creates fake artifacts for other architectures (for packaging)
4. Adds `-s390x` suffix to package version for identification

### Package Contents
The resulting NuGet package includes:
- `tools/linux_s390x/protoc` - Protocol buffer compiler
- `tools/linux_s390x/grpc_csharp_plugin` - gRPC C# code generator
- Placeholder files for other architectures

## Troubleshooting

### Build Fails with Missing Bazel
```
ERROR: No bazel found in PATH. Please install Bazel for s390x.
```
**Solution**: Follow the "Build Bazel from Source" steps above.

### Permission Denied Copying Files
```
cp: cannot create regular file 'protoc_plugins/protoc_linux_s390x/protoc': Permission denied
```
**Solution**: The build script automatically handles this by cleaning the directory first.

### Missing Protoc/Plugin Files
```
Warning: grpc_csharp_plugin not found in bazel-bin
```
**Solution**: Ensure the Bazel build completed successfully:
```bash
bazel build @com_google_protobuf//:protoc //src/compiler:all
ls -la bazel-bin/src/compiler/grpc_csharp_plugin*
ls -la bazel-bin/external/com_google_protobuf/protoc
```

## Integration with ASP.NET Core

When using this package with ASP.NET Core builds:
1. The package will be automatically selected on s390x systems
2. The `-s390x` suffix clearly identifies the architecture-specific package
3. All gRPC code generation will work normally

## Limitations

1. **Single-platform package**: The s390x package only works on s390x systems
2. **Manual Bazel build**: Requires building Bazel from source (one-time setup)
3. **Local compilation**: Must compile protoc/plugins locally (automated by build script)

These limitations are fundamental to the s390x architecture support landscape and follow the same patterns used by other architectures that lack prebuilt tooling.

## Contributing

If you encounter issues with s390x builds or have improvements:
1. Check existing issues tagged with `s390x` or `architecture:s390x`
2. When reporting issues, include:
   - Bazel version (`bazel version`)
   - .NET SDK version (`dotnet --version`)
   - System information (`uname -a`)
   - Build error output

## References

- [Bazel Installation Guide](https://bazel.build/install/compile-source)
- [gRPC Build Requirements](https://github.com/grpc/grpc/blob/master/BUILDING.md)
- [IBM System z Developer Resources](https://developer.ibm.com/mainframe/)
