using System.Text;
using Grpc.Core.Internal;
using NUnit.Framework;

namespace Grpc.Core.Tests.Internal
{
    public class WellKnownStringsTest
    {
        [Test]
        [TestCase("", true)]
        [TestCase("u", false)]
        [TestCase("us", false)]
        [TestCase("use", false)]
        [TestCase("user", false)]
        [TestCase("user-", false)]
        [TestCase("user-a", false)]
        [TestCase("user-ag", false)]
        [TestCase("user-age", false)]
        [TestCase("user-agent", true)]
        [TestCase("user-agent ", false)]
        [TestCase("useragent ", false)]
        [TestCase("User-Agent", false)]
        [TestCase("sdlkfjlskjfdlkjs;lfdksflsdfkh skjdfh sdkfhskdhf skjfhk sdhjkjh", false)]

        // test for endianness snafus (reversed in segments)
        [TestCase("ega-resutn", false)]
        public unsafe void TestWellKnownStrings(string input, bool expected)
        {
            // create a copy of the data; no cheating!
            byte[] bytes = Encoding.ASCII.GetBytes(input);
            fixed(byte* ptr = bytes)
            {
                string result = WellKnownStrings.TryIdentify(ptr, bytes.Length);
                if (expected) Assert.AreEqual(input, result);
                else Assert.IsNull(result);

                if (expected)
                {
                    // try again, and check we get the same instance
                    string again = WellKnownStrings.TryIdentify(ptr, bytes.Length);
                    Assert.AreSame(result, again);
                }
            }
        }
    }
}
