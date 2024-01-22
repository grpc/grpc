import sys

try:
    import grpc
else:
    sys.exit("Unexpectedly able to import grpc")

import grpc_library_replacement
