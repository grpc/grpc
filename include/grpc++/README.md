# include/grpc++

This was the original directory name for all C++ header files but it
conflicted with the naming scheme required for some build systems. It
is superseded by `include/grpcpp` but the old directory structure is
still present to avoid breaking code that used the old include files.
All new include files are only in `include/grpcpp`.
