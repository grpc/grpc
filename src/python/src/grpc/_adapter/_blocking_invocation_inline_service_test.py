"""One of the tests of the Face layer of RPC Framework."""

import unittest

from grpc._adapter import _face_test_case
from grpc.framework.face.testing import blocking_invocation_inline_service_test_case as test_case


class BlockingInvocationInlineServiceTest(
    _face_test_case.FaceTestCase,
    test_case.BlockingInvocationInlineServiceTestCase,
    unittest.TestCase):
  pass


if __name__ == '__main__':
  unittest.main()
