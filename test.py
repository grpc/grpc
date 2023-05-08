import logging
import re
import random

logger = logging.getLogger(__name__)

SPAN_ID_PATTERN = re.compile('[0-9a-f]{16}?')

match = SPAN_ID_PATTERN.match("8f86580885e5fb8")
if match:
    print("match")

# print(random.getrandbits(64))
py_res = '{:016x}'.format(3308092737178027556)
print(py_res)

match = SPAN_ID_PATTERN.match(py_res)
if match:
    print("match")