# public_headers
bazel query 'deps(//:gpr)' | grep '^//:include/' | grep '\.h$' | sort

# headers
bazel query 'deps(//:gpr)' | grep '^//:' |  grep -v '^//:include/' | grep '\.h$' | sort

# src (only .cc files)
bazel query 'deps(//:gpr)' | grep '^//:' |  grep '\.cc$' | sort

# TODO: how about deps??/
bazel query 'deps(//:gpr)' --noimplicit_deps --output label_kind | grep 'cc_library rule //:'

# proto files
bazel query 'deps("//:grpcpp_channelz")' | grep '\.proto$' | sort


# cc_library in root
bazel query 'kind("cc_library", "//:*")'
# TODO:
#secure?
#zlib?
#ares?
#build: all, test, ...
#dll: only, true, false



# https://github.com/grpc/grpc/blob/master/templates/README.md



gpr
grpc
grpc_cronet (no corresponding target in BUILD)
grpc_csharp_ext
grpc_plugin_support (in src/compiler/BUILD)
grpc_unsecure
grpc++
grpc++_unsecure
grpc++_core_stats
grpc++_error_details
grpc++_proto_reflection_desc_db (no corresponding target in BUILD)
grpc++_reflection
grpcpp_channelz



--output label_kind
