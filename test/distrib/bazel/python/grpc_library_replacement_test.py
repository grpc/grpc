import sys

try:
    import grpc
except ImportError:
    pass
else:
    sys.exit("Unexpectedly able to import grpc")

import grpc_library_replacement
