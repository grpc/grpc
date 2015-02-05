using System;
using Google.GRPC.Core.Internal;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using System.Collections.Generic;

namespace Google.GRPC.Core.Internal
{
    /// <summary>
    /// Pool of threads polling on the same completion queue.
    /// </summary>
    internal class GrpcThreadPool
    {
        readonly object myLock = new object();
        readonly List<Thread> threads = new List<Thread>();
        readonly int poolSize;
        readonly Action<EventSafeHandle> eventHandler;

        CompletionQueueSafeHandle cq;

        public GrpcThreadPool(int poolSize) {
            this.poolSize = poolSize;
        }

        internal GrpcThreadPool(int poolSize, Action<EventSafeHandle> eventHandler) {
            this.poolSize = poolSize;
            this.eventHandler = eventHandler;
        }

        public void Start() {

            lock (myLock)
            {
                if (cq != null)
                {
                    throw new InvalidOperationException("Already started.");
                }

                cq = CompletionQueueSafeHandle.Create();

                for (int i = 0; i < poolSize; i++)
                {
                    threads.Add(CreateAndStartThread(i));
                }
            }
        }

        public void Stop() {

            lock (myLock)
            {
                cq.Shutdown();

                Console.WriteLine("Waiting for GPRC threads to finish.");
                foreach (var thread in threads)
                {
                    thread.Join();
                }

                cq.Dispose();

            }
        }

        internal CompletionQueueSafeHandle CompletionQueue
        {
            get
            {
                return cq;
            }
        }

        private Thread CreateAndStartThread(int i) {
            Action body;
            if (eventHandler != null)
            {
                body = ThreadBodyWithHandler;
            }
            else
            {
                body = ThreadBodyNoHandler;
            }
            var thread = new Thread(new ThreadStart(body));
            thread.IsBackground = false;
            thread.Start();
            if (eventHandler != null)
            {
                thread.Name = "grpc_server_newrpc " + i;
            }
            else
            {
                thread.Name = "grpc " + i;
            }
            return thread;
        }

        /// <summary>
        /// Body of the polling thread.
        /// </summary>
        private void ThreadBodyNoHandler()
        {
            GRPCCompletionType completionType;
            do
            {
                completionType = cq.NextWithCallback();
            } while(completionType != GRPCCompletionType.GRPC_QUEUE_SHUTDOWN);
            Console.WriteLine("Completion queue has shutdown successfully, thread " + Thread.CurrentThread.Name + " exiting.");
        }

        /// <summary>
        /// Body of the polling thread.
        /// </summary>
        private void ThreadBodyWithHandler()
        {
            GRPCCompletionType completionType;
            do
            {
                using (EventSafeHandle ev = cq.Next(Timespec.InfFuture)) {
                    completionType = ev.GetCompletionType();
                    eventHandler(ev);
                }
            } while(completionType != GRPCCompletionType.GRPC_QUEUE_SHUTDOWN);
            Console.WriteLine("Completion queue has shutdown successfully, thread " + Thread.CurrentThread.Name + " exiting.");
        }
    }

}

