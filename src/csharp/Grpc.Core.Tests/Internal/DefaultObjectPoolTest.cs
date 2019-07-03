#region Copyright notice and license

// Copyright 2017 gRPC authors.
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
using Grpc.Core;
using Grpc.Core.Internal;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Grpc.Core.Internal.Tests
{
    public class DefaultObjectPoolTest
    {
        [Test]
        [TestCase(10, 2)]
        [TestCase(10, 1)]
        [TestCase(0, 2)]
        [TestCase(2, 0)]
        public void ObjectIsReused(int sharedCapacity, int threadLocalCapacity)
        {
            var pool = new DefaultObjectPool<TestPooledObject>(() => new TestPooledObject(), sharedCapacity, threadLocalCapacity);
            var origLeased = pool.Lease();
            pool.Return(origLeased);
            Assert.AreSame(origLeased, pool.Lease());
            Assert.AreNotSame(origLeased, pool.Lease());
        }

        [Test]
        public void ZeroCapacities()
        {
            var pool = new DefaultObjectPool<TestPooledObject>(() => new TestPooledObject(), 0, 0);
            var origLeased = pool.Lease();
            pool.Return(origLeased);
            Assert.AreNotSame(origLeased, pool.Lease());
        }

        [Test]
        public void DisposeCleansSharedPool()
        {
            var pool = new DefaultObjectPool<TestPooledObject>(() => new TestPooledObject(), 10, 0);
            var origLeased = pool.Lease();
            pool.Return(origLeased);
            pool.Dispose();
            Assert.AreNotSame(origLeased, pool.Lease());
        }

        [Test]
        public void LeaseSetsReturnAction()
        {
            var pool = new DefaultObjectPool<TestPooledObject>(() => new TestPooledObject(), 10, 0);
            var origLeased = pool.Lease();
            origLeased.ReturnAction(origLeased);
            pool.Dispose();
            Assert.AreNotSame(origLeased, pool.Lease());
        }

        [Test]
        public void Constructor()
        {
            Assert.Throws<ArgumentNullException>(() => new DefaultObjectPool<TestPooledObject>(null, 10, 2));
            Assert.Throws<ArgumentException>(() => new DefaultObjectPool<TestPooledObject>(() => new TestPooledObject(), -1, 10));
            Assert.Throws<ArgumentException>(() => new DefaultObjectPool<TestPooledObject>(() => new TestPooledObject(), 10, -1));
        }

        class TestPooledObject : IPooledObject<TestPooledObject>
        {
            public Action<TestPooledObject> ReturnAction;

            public void SetReturnToPoolAction(Action<TestPooledObject> returnAction)
            {
                this.ReturnAction = returnAction;
            }

            public void Dispose()
            {

            }
        }
    }
}
