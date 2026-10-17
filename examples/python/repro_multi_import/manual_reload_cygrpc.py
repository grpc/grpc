import importlib

import grpc

# This import is just to make sure the name
# 'grpc._cython.cygrpc' is easily accessible.
# The 'import grpc' above already loaded it.
import grpc._cython.cygrpc

print("Initial import is done. Your function has been called once.")

# Now, we *specifically* reload the Cython module...
print("Attempting to reload grpc._cython.cygrpc...")

try:
    # This will re-run the init code in your .pyx file
    # and call your "once-only" function a second time.
    importlib.reload(grpc._cython.cygrpc)
except Exception as e:
    print(f"\nSuccessfully caught expected error on reload: {e}")  # 💥
