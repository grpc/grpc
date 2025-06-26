import sys

if sys.version_info >= (3, 9):
    from _thread import *
else:
    from _dummy_thread import *
