using System;
using System.Threading;
using System.Threading.Tasks;
using System.Collections.Generic;
using System.Reactive.Linq;
using Google.GRPC.Core.Utils;

namespace math
{
    /// <summary>
    /// Implementation of MathService server
    /// </summary>
    public class MathServiceImpl : MathGrpc.IMathService
    {
        public void Div(DivArgs request, IObserver<DivReply> responseObserver)
        {
            var response = DivInternal(request);
            responseObserver.OnNext(response);
            responseObserver.OnCompleted();
        }

        public void Fib(FibArgs request, IObserver<Num> responseObserver)
        {
            if (request.Limit <= 0)
            {
                // TODO: support cancellation....
                throw new NotImplementedException("Not implemented yet");
            }
                                  
            if (request.Limit > 0)
            {
                foreach (var num in FibInternal(request.Limit))
                {
                    responseObserver.OnNext(num);
                }
                responseObserver.OnCompleted();
            }
        }

        public IObserver<Num> Sum(IObserver<Num> responseObserver)
        {
            var recorder = new RecordingObserver<Num>();
            Task.Factory.StartNew(() => {

                List<Num> inputs = recorder.ToList().Result;

                long sum = 0;
                foreach (Num num in inputs)
                {
                    sum += num.Num_;
                }

                responseObserver.OnNext(Num.CreateBuilder().SetNum_(sum).Build());
                responseObserver.OnCompleted();
            });
            return recorder;
        }

        public IObserver<DivArgs> DivMany(IObserver<DivReply> responseObserver)
        {
            return new DivObserver(responseObserver);
        }

        static DivReply DivInternal(DivArgs args)
        {
            long quotient = args.Dividend / args.Divisor;
            long remainder = args.Dividend % args.Divisor;
            return new DivReply.Builder { Quotient = quotient, Remainder = remainder }.Build();
        }

        static IEnumerable<Num> FibInternal(long n)
        {
            long a = 1;
            yield return new Num.Builder { Num_=a }.Build();

            long b = 1;
            for (long i = 0; i < n - 1; i++)
            {
                long temp = a;
                a = b;
                b = temp + b;
                yield return new Num.Builder { Num_=a }.Build();
            }
        }

        private class DivObserver : IObserver<DivArgs> {

            readonly IObserver<DivReply> responseObserver;

            public DivObserver(IObserver<DivReply> responseObserver)
            {
                this.responseObserver = responseObserver;
            }
            
            public void OnCompleted()
            {
                Task.Factory.StartNew(() =>
                    responseObserver.OnCompleted());
            }

            public void OnError(Exception error)
            {
                throw new NotImplementedException();
            }

            public void OnNext(DivArgs value)
            {
                // TODO: currently we need this indirection because
                // responseObserver waits for write to finish, this
                // callback is called from grpc threadpool which
                // currently only has one thread.
                // Same story for OnCompleted().
                Task.Factory.StartNew(() => 
                responseObserver.OnNext(DivInternal(value)));
            }
        }
    }
}

