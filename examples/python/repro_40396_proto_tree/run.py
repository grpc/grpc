# import grpc


def print_proto_dir(name, proto):
    print(name)
    for p in dir(proto):
        print("-", p)


import inspect

import grpc

proto = grpc.protos("file1.proto")
print_proto_dir("proto1", proto)

#  # Output: ['DESCRIPTOR', '__builtins__', '__doc__', '__loader__', '__name__', '__package__', '__spec__', '_builder', '_descriptor', '_descriptor_pool', '_globals', '_runtime_version', '_sym_db', '_symbol_database', 'file2__pb2']

# inspect.getsource(proto.file2__pb2)

# import grpc
# proto3 = grpc.protos('file3.proto')
# proto2 = grpc.protos('file2.proto')
# proto1 = grpc.protos('file1.proto')

# for name,proto in zip(["proto3", "proto2", "proto1"], [proto3, proto2, proto1]):
#     print_proto_dir(name, proto)
#     print()
