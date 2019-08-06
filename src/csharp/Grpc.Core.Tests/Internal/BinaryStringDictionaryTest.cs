#region Copyright notice and license

// Copyright 2019 The gRPC Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#endregion

using System;
using NUnit.Framework;

namespace Grpc.Core.Internal.Tests
{
    public class BinaryStringDictionaryTest
    {
        [Test]
        public void Lookup() 
        {
            var dict = new BinaryStringDictionary();
            dict.Add("abc");
            dict.Add("def");
            dict.Add("abcdef");

            Assert.AreEqual("abc", NativeLookup(dict, new byte[] {0x61, 0x62, 0x63} ));
            Assert.AreEqual("def", NativeLookup(dict, new byte[] {0x64, 0x65, 0x66} ));
            Assert.AreEqual("abcdef", NativeLookup(dict, new byte[] {0x61, 0x62, 0x63, 0x64, 0x65, 0x66} ));
        }

        [Test]
        public void Lookup_KeyNotFound() 
        {
            var dict = new BinaryStringDictionary();
            dict.Add("abc");

            Assert.IsNull(NativeLookup(dict, new byte[] {0x61, 0x62, 0x63, 0x64} ));
        }

        [Test]
        public void Lookup_ReturnsCachedString() 
        {
            var dict = new BinaryStringDictionary();
            var abcString = "abc";
            dict.Add(abcString);

            Assert.AreSame(abcString, NativeLookup(dict, new byte[] {0x61, 0x62, 0x63} ));
            Assert.AreSame(abcString, NativeLookup(dict, new byte[] {0x61, 0x62, 0x63} ));
        }

        [Test]
        public void Lookup_SpecialCharacters() 
        {
            var dict = new BinaryStringDictionary();
            dict.Add("\n\t");

            Assert.AreEqual("\n\t", NativeLookup(dict, new byte[] {0x0a, 0x09} ));
        }

        unsafe string NativeLookup(BinaryStringDictionary dict, byte[] bytes)
        {
            fixed (byte *ptr = bytes)
            {
                return dict.Lookup(new IntPtr(ptr), bytes.Length);
            }
        }
    }

}
