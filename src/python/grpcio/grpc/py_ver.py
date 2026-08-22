import sys

print(sys.version)
print(sys.executable)
print("GIL Enabled: ", getattr(sys, "_is_gil_enabled", lambda: True)())
