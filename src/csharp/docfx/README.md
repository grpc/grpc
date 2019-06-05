DocFX-generated C# API Reference
--------------------------------

## Generating docs manually (on Windows)

Install docfx based on instructions here: https://github.com/dotnet/docfx

```
# generate docfx documentation into ./html directory
$ docfx
```

## Release process: script for regenerating the docs automatically

After each gRPC C# release, the docs need to be regenerated
and updated on the grpc.io site. The automated script will
re-generate the docs (using dockerized docfx installation)
and make everything ready for creating a PR to update the docs.

```
# 1. Run the script on Linux with docker installed
$ ./generate_reference_docs.sh

# 2. Enter the git repo with updated "gh-pages" branch
$ cd grpc-gh-pages

# 3. Review the changes and create a pull request
```
