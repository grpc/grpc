import sys
import inspect
import os

from tests.harness.harness_pb2 import TestCase, TestResult
from tests.harness.cases.bool_pb2 import *
from tests.harness.cases.bytes_pb2 import *
from tests.harness.cases.enums_pb2 import *
from tests.harness.cases.enums_pb2 import *
from tests.harness.cases.filename_with_dash_pb2 import *
from tests.harness.cases.messages_pb2 import *
from tests.harness.cases.numbers_pb2 import *
from tests.harness.cases.oneofs_pb2 import *
from tests.harness.cases.repeated_pb2 import *
from tests.harness.cases.strings_pb2 import *
from tests.harness.cases.maps_pb2 import *
from tests.harness.cases.wkt_any_pb2 import *
from tests.harness.cases.wkt_duration_pb2 import *
from tests.harness.cases.wkt_wrappers_pb2 import *
from tests.harness.cases.wkt_timestamp_pb2 import *
from tests.harness.cases.kitchen_sink_pb2 import *

sys.path.append(os.environ['GOPATH']+'/src/github.com/envoyproxy/protoc-gen-validate/validate')
from validator import validate, ValidationFailed, UnimplementedException, print_validate

class_list = []
for k, v in inspect.getmembers(sys.modules[__name__], inspect.isclass):
    if 'DESCRIPTOR' in dir(v):
        class_list.append(v)

def unpack(message):
    for cls in class_list:
        if message.Is(cls.DESCRIPTOR):
            test_class = cls()
            message.Unpack(test_class)
            return test_class

if __name__ == "__main__":
    if sys.version_info[0] >= 3:
        message = sys.stdin.buffer.read()
    else:
        message = sys.stdin.read()
    testcase = TestCase()
    try:
        testcase.ParseFromString(message)
    except TypeError:
        testcase.ParseFromString(message.encode(errors='surrogateescape'))
    test_class = unpack(testcase.message)
    try:
        result = TestResult()
        valid = validate(test_class)
        valid(test_class)
        result.Valid = True
    except ValidationFailed as e:
        result.Valid = False
        result.Reason = repr(e)
    except UnimplementedException as e:
        result.Error = False
        result.AllowFailure = True
        result.Reason = repr(e)
    sys.stdout = open(sys.stdout.fileno(), mode='w', encoding='utf8')
    try:
        sys.stdout.write(result.SerializeToString().decode("utf-8"))
    except TypeError:
        sys.stdout.write(result.SerializeToString().decode("utf-8", errors='surrogateescape'))
