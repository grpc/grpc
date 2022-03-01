# Copyright (c) 2009-2021, Google LLC
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#     * Neither the name of Google LLC nor the
#       names of its contributors may be used to endorse or promote products
#       derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL Google LLC BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

from google.protobuf.internal import reflection_test
import unittest

# These tests depend on a specific iteration order for extensions, which is not
# reasonable to guarantee.
reflection_test.Proto2ReflectionTest.testExtensionIter.__unittest_expecting_failure__ = True

# These tests depend on a specific serialization order for extensions, which is
# not reasonable to guarantee.
reflection_test.SerializationTest.testCanonicalSerializationOrder.__unittest_expecting_failure__ = True
reflection_test.SerializationTest.testCanonicalSerializationOrderSameAsCpp.__unittest_expecting_failure__ = True

# This test relies on the internal implementation using Python descriptors.
# This is an implementation detail that users should not depend on.
reflection_test.SerializationTest.testFieldDataDescriptor.__unittest_expecting_failure__ = True

reflection_test.ReflectionTest.testDeepCopy_proto2.__unittest_expecting_failure__ = True
reflection_test.ReflectionTest.testDeepCopy_proto3.__unittest_expecting_failure__ = True
reflection_test.SerializationTest.testFieldProperties.__unittest_expecting_failure__ = True

if __name__ == '__main__':
  unittest.main(module=reflection_test, verbosity=2)
