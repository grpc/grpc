# Developer documentation

## Synchronize generated files

Run the following command to update the generated files and commit them with your change:

```sh
bazel build //...
tools/generate_go_protobuf.py
```
