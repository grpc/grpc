Required elements of gRPC Core: Each module in this directory is required to
build gRPC. If it's possible to envisage a configuration where code is not
required, then that code belongs in ext/ instead.

NOTE: The movement of code between lib and ext is an ongoing effort, so this
directory currently contains too much of the core library.
