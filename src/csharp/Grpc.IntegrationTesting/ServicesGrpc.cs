// Generated by the protocol buffer compiler.  DO NOT EDIT!
// source: src/proto/grpc/testing/services.proto
#region Designer generated code

using System;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core;

namespace Grpc.Testing {
  public static class BenchmarkService
  {
    static readonly string __ServiceName = "grpc.testing.BenchmarkService";

    static readonly Marshaller<global::Grpc.Testing.SimpleRequest> __Marshaller_SimpleRequest = Marshallers.Create((arg) => global::Google.Protobuf.MessageExtensions.ToByteArray(arg), global::Grpc.Testing.SimpleRequest.Parser.ParseFrom);
    static readonly Marshaller<global::Grpc.Testing.SimpleResponse> __Marshaller_SimpleResponse = Marshallers.Create((arg) => global::Google.Protobuf.MessageExtensions.ToByteArray(arg), global::Grpc.Testing.SimpleResponse.Parser.ParseFrom);

    static readonly Method<global::Grpc.Testing.SimpleRequest, global::Grpc.Testing.SimpleResponse> __Method_UnaryCall = new Method<global::Grpc.Testing.SimpleRequest, global::Grpc.Testing.SimpleResponse>(
        MethodType.Unary,
        __ServiceName,
        "UnaryCall",
        __Marshaller_SimpleRequest,
        __Marshaller_SimpleResponse);

    static readonly Method<global::Grpc.Testing.SimpleRequest, global::Grpc.Testing.SimpleResponse> __Method_StreamingCall = new Method<global::Grpc.Testing.SimpleRequest, global::Grpc.Testing.SimpleResponse>(
        MethodType.DuplexStreaming,
        __ServiceName,
        "StreamingCall",
        __Marshaller_SimpleRequest,
        __Marshaller_SimpleResponse);

    // service descriptor
    public static global::Google.Protobuf.Reflection.ServiceDescriptor Descriptor
    {
      get { return global::Grpc.Testing.ServicesReflection.Descriptor.Services[0]; }
    }

    // client interface
    [System.Obsolete("Client side interfaced will be removed in the next release. Use client class directly.")]
    public interface IBenchmarkServiceClient
    {
      global::Grpc.Testing.SimpleResponse UnaryCall(global::Grpc.Testing.SimpleRequest request, Metadata headers = null, DateTime? deadline = null, CancellationToken cancellationToken = default(CancellationToken));
      global::Grpc.Testing.SimpleResponse UnaryCall(global::Grpc.Testing.SimpleRequest request, CallOptions options);
      AsyncUnaryCall<global::Grpc.Testing.SimpleResponse> UnaryCallAsync(global::Grpc.Testing.SimpleRequest request, Metadata headers = null, DateTime? deadline = null, CancellationToken cancellationToken = default(CancellationToken));
      AsyncUnaryCall<global::Grpc.Testing.SimpleResponse> UnaryCallAsync(global::Grpc.Testing.SimpleRequest request, CallOptions options);
      AsyncDuplexStreamingCall<global::Grpc.Testing.SimpleRequest, global::Grpc.Testing.SimpleResponse> StreamingCall(Metadata headers = null, DateTime? deadline = null, CancellationToken cancellationToken = default(CancellationToken));
      AsyncDuplexStreamingCall<global::Grpc.Testing.SimpleRequest, global::Grpc.Testing.SimpleResponse> StreamingCall(CallOptions options);
    }

    // server-side interface
    [System.Obsolete("Service implementations should inherit from the generated abstract base class instead.")]
    public interface IBenchmarkService
    {
      Task<global::Grpc.Testing.SimpleResponse> UnaryCall(global::Grpc.Testing.SimpleRequest request, ServerCallContext context);
      Task StreamingCall(IAsyncStreamReader<global::Grpc.Testing.SimpleRequest> requestStream, IServerStreamWriter<global::Grpc.Testing.SimpleResponse> responseStream, ServerCallContext context);
    }

    // server-side abstract class
    public abstract class BenchmarkServiceBase
    {
      public virtual Task<global::Grpc.Testing.SimpleResponse> UnaryCall(global::Grpc.Testing.SimpleRequest request, ServerCallContext context)
      {
        throw new RpcException(new Status(StatusCode.Unimplemented, ""));
      }

      public virtual Task StreamingCall(IAsyncStreamReader<global::Grpc.Testing.SimpleRequest> requestStream, IServerStreamWriter<global::Grpc.Testing.SimpleResponse> responseStream, ServerCallContext context)
      {
        throw new RpcException(new Status(StatusCode.Unimplemented, ""));
      }

    }

    // client stub
    #pragma warning disable 0618
    public class BenchmarkServiceClient : ClientBase<BenchmarkServiceClient>, IBenchmarkServiceClient
    #pragma warning restore 0618
    {
      public BenchmarkServiceClient(Channel channel) : base(channel)
      {
      }
      public BenchmarkServiceClient(CallInvoker callInvoker) : base(callInvoker)
      {
      }
      ///<summary>Protected parameterless constructor to allow creation of test doubles.</summary>
      protected BenchmarkServiceClient() : base()
      {
      }
      ///<summary>Protected constructor to allow creation of configured clients.</summary>
      protected BenchmarkServiceClient(ClientBaseConfiguration configuration) : base(configuration)
      {
      }

      public virtual global::Grpc.Testing.SimpleResponse UnaryCall(global::Grpc.Testing.SimpleRequest request, Metadata headers = null, DateTime? deadline = null, CancellationToken cancellationToken = default(CancellationToken))
      {
        return UnaryCall(request, new CallOptions(headers, deadline, cancellationToken));
      }
      public virtual global::Grpc.Testing.SimpleResponse UnaryCall(global::Grpc.Testing.SimpleRequest request, CallOptions options)
      {
        return CallInvoker.BlockingUnaryCall(__Method_UnaryCall, null, options, request);
      }
      public virtual AsyncUnaryCall<global::Grpc.Testing.SimpleResponse> UnaryCallAsync(global::Grpc.Testing.SimpleRequest request, Metadata headers = null, DateTime? deadline = null, CancellationToken cancellationToken = default(CancellationToken))
      {
        return UnaryCallAsync(request, new CallOptions(headers, deadline, cancellationToken));
      }
      public virtual AsyncUnaryCall<global::Grpc.Testing.SimpleResponse> UnaryCallAsync(global::Grpc.Testing.SimpleRequest request, CallOptions options)
      {
        return CallInvoker.AsyncUnaryCall(__Method_UnaryCall, null, options, request);
      }
      public virtual AsyncDuplexStreamingCall<global::Grpc.Testing.SimpleRequest, global::Grpc.Testing.SimpleResponse> StreamingCall(Metadata headers = null, DateTime? deadline = null, CancellationToken cancellationToken = default(CancellationToken))
      {
        return StreamingCall(new CallOptions(headers, deadline, cancellationToken));
      }
      public virtual AsyncDuplexStreamingCall<global::Grpc.Testing.SimpleRequest, global::Grpc.Testing.SimpleResponse> StreamingCall(CallOptions options)
      {
        return CallInvoker.AsyncDuplexStreamingCall(__Method_StreamingCall, null, options);
      }
      protected override BenchmarkServiceClient NewInstance(ClientBaseConfiguration configuration)
      {
        return new BenchmarkServiceClient(configuration);
      }
    }

    // creates service definition that can be registered with a server
    #pragma warning disable 0618
    public static ServerServiceDefinition BindService(IBenchmarkService serviceImpl)
    #pragma warning restore 0618
    {
      return ServerServiceDefinition.CreateBuilder(__ServiceName)
          .AddMethod(__Method_UnaryCall, serviceImpl.UnaryCall)
          .AddMethod(__Method_StreamingCall, serviceImpl.StreamingCall).Build();
    }

    // creates service definition that can be registered with a server
    #pragma warning disable 0618
    public static ServerServiceDefinition BindService(BenchmarkServiceBase serviceImpl)
    #pragma warning restore 0618
    {
      return ServerServiceDefinition.CreateBuilder(__ServiceName)
          .AddMethod(__Method_UnaryCall, serviceImpl.UnaryCall)
          .AddMethod(__Method_StreamingCall, serviceImpl.StreamingCall).Build();
    }

    // creates a new client
    public static BenchmarkServiceClient NewClient(Channel channel)
    {
      return new BenchmarkServiceClient(channel);
    }

  }
  public static class WorkerService
  {
    static readonly string __ServiceName = "grpc.testing.WorkerService";

    static readonly Marshaller<global::Grpc.Testing.ServerArgs> __Marshaller_ServerArgs = Marshallers.Create((arg) => global::Google.Protobuf.MessageExtensions.ToByteArray(arg), global::Grpc.Testing.ServerArgs.Parser.ParseFrom);
    static readonly Marshaller<global::Grpc.Testing.ServerStatus> __Marshaller_ServerStatus = Marshallers.Create((arg) => global::Google.Protobuf.MessageExtensions.ToByteArray(arg), global::Grpc.Testing.ServerStatus.Parser.ParseFrom);
    static readonly Marshaller<global::Grpc.Testing.ClientArgs> __Marshaller_ClientArgs = Marshallers.Create((arg) => global::Google.Protobuf.MessageExtensions.ToByteArray(arg), global::Grpc.Testing.ClientArgs.Parser.ParseFrom);
    static readonly Marshaller<global::Grpc.Testing.ClientStatus> __Marshaller_ClientStatus = Marshallers.Create((arg) => global::Google.Protobuf.MessageExtensions.ToByteArray(arg), global::Grpc.Testing.ClientStatus.Parser.ParseFrom);
    static readonly Marshaller<global::Grpc.Testing.CoreRequest> __Marshaller_CoreRequest = Marshallers.Create((arg) => global::Google.Protobuf.MessageExtensions.ToByteArray(arg), global::Grpc.Testing.CoreRequest.Parser.ParseFrom);
    static readonly Marshaller<global::Grpc.Testing.CoreResponse> __Marshaller_CoreResponse = Marshallers.Create((arg) => global::Google.Protobuf.MessageExtensions.ToByteArray(arg), global::Grpc.Testing.CoreResponse.Parser.ParseFrom);
    static readonly Marshaller<global::Grpc.Testing.Void> __Marshaller_Void = Marshallers.Create((arg) => global::Google.Protobuf.MessageExtensions.ToByteArray(arg), global::Grpc.Testing.Void.Parser.ParseFrom);

    static readonly Method<global::Grpc.Testing.ServerArgs, global::Grpc.Testing.ServerStatus> __Method_RunServer = new Method<global::Grpc.Testing.ServerArgs, global::Grpc.Testing.ServerStatus>(
        MethodType.DuplexStreaming,
        __ServiceName,
        "RunServer",
        __Marshaller_ServerArgs,
        __Marshaller_ServerStatus);

    static readonly Method<global::Grpc.Testing.ClientArgs, global::Grpc.Testing.ClientStatus> __Method_RunClient = new Method<global::Grpc.Testing.ClientArgs, global::Grpc.Testing.ClientStatus>(
        MethodType.DuplexStreaming,
        __ServiceName,
        "RunClient",
        __Marshaller_ClientArgs,
        __Marshaller_ClientStatus);

    static readonly Method<global::Grpc.Testing.CoreRequest, global::Grpc.Testing.CoreResponse> __Method_CoreCount = new Method<global::Grpc.Testing.CoreRequest, global::Grpc.Testing.CoreResponse>(
        MethodType.Unary,
        __ServiceName,
        "CoreCount",
        __Marshaller_CoreRequest,
        __Marshaller_CoreResponse);

    static readonly Method<global::Grpc.Testing.Void, global::Grpc.Testing.Void> __Method_QuitWorker = new Method<global::Grpc.Testing.Void, global::Grpc.Testing.Void>(
        MethodType.Unary,
        __ServiceName,
        "QuitWorker",
        __Marshaller_Void,
        __Marshaller_Void);

    // service descriptor
    public static global::Google.Protobuf.Reflection.ServiceDescriptor Descriptor
    {
      get { return global::Grpc.Testing.ServicesReflection.Descriptor.Services[1]; }
    }

    // client interface
    [System.Obsolete("Client side interfaced will be removed in the next release. Use client class directly.")]
    public interface IWorkerServiceClient
    {
      AsyncDuplexStreamingCall<global::Grpc.Testing.ServerArgs, global::Grpc.Testing.ServerStatus> RunServer(Metadata headers = null, DateTime? deadline = null, CancellationToken cancellationToken = default(CancellationToken));
      AsyncDuplexStreamingCall<global::Grpc.Testing.ServerArgs, global::Grpc.Testing.ServerStatus> RunServer(CallOptions options);
      AsyncDuplexStreamingCall<global::Grpc.Testing.ClientArgs, global::Grpc.Testing.ClientStatus> RunClient(Metadata headers = null, DateTime? deadline = null, CancellationToken cancellationToken = default(CancellationToken));
      AsyncDuplexStreamingCall<global::Grpc.Testing.ClientArgs, global::Grpc.Testing.ClientStatus> RunClient(CallOptions options);
      global::Grpc.Testing.CoreResponse CoreCount(global::Grpc.Testing.CoreRequest request, Metadata headers = null, DateTime? deadline = null, CancellationToken cancellationToken = default(CancellationToken));
      global::Grpc.Testing.CoreResponse CoreCount(global::Grpc.Testing.CoreRequest request, CallOptions options);
      AsyncUnaryCall<global::Grpc.Testing.CoreResponse> CoreCountAsync(global::Grpc.Testing.CoreRequest request, Metadata headers = null, DateTime? deadline = null, CancellationToken cancellationToken = default(CancellationToken));
      AsyncUnaryCall<global::Grpc.Testing.CoreResponse> CoreCountAsync(global::Grpc.Testing.CoreRequest request, CallOptions options);
      global::Grpc.Testing.Void QuitWorker(global::Grpc.Testing.Void request, Metadata headers = null, DateTime? deadline = null, CancellationToken cancellationToken = default(CancellationToken));
      global::Grpc.Testing.Void QuitWorker(global::Grpc.Testing.Void request, CallOptions options);
      AsyncUnaryCall<global::Grpc.Testing.Void> QuitWorkerAsync(global::Grpc.Testing.Void request, Metadata headers = null, DateTime? deadline = null, CancellationToken cancellationToken = default(CancellationToken));
      AsyncUnaryCall<global::Grpc.Testing.Void> QuitWorkerAsync(global::Grpc.Testing.Void request, CallOptions options);
    }

    // server-side interface
    [System.Obsolete("Service implementations should inherit from the generated abstract base class instead.")]
    public interface IWorkerService
    {
      Task RunServer(IAsyncStreamReader<global::Grpc.Testing.ServerArgs> requestStream, IServerStreamWriter<global::Grpc.Testing.ServerStatus> responseStream, ServerCallContext context);
      Task RunClient(IAsyncStreamReader<global::Grpc.Testing.ClientArgs> requestStream, IServerStreamWriter<global::Grpc.Testing.ClientStatus> responseStream, ServerCallContext context);
      Task<global::Grpc.Testing.CoreResponse> CoreCount(global::Grpc.Testing.CoreRequest request, ServerCallContext context);
      Task<global::Grpc.Testing.Void> QuitWorker(global::Grpc.Testing.Void request, ServerCallContext context);
    }

    // server-side abstract class
    public abstract class WorkerServiceBase
    {
      public virtual Task RunServer(IAsyncStreamReader<global::Grpc.Testing.ServerArgs> requestStream, IServerStreamWriter<global::Grpc.Testing.ServerStatus> responseStream, ServerCallContext context)
      {
        throw new RpcException(new Status(StatusCode.Unimplemented, ""));
      }

      public virtual Task RunClient(IAsyncStreamReader<global::Grpc.Testing.ClientArgs> requestStream, IServerStreamWriter<global::Grpc.Testing.ClientStatus> responseStream, ServerCallContext context)
      {
        throw new RpcException(new Status(StatusCode.Unimplemented, ""));
      }

      public virtual Task<global::Grpc.Testing.CoreResponse> CoreCount(global::Grpc.Testing.CoreRequest request, ServerCallContext context)
      {
        throw new RpcException(new Status(StatusCode.Unimplemented, ""));
      }

      public virtual Task<global::Grpc.Testing.Void> QuitWorker(global::Grpc.Testing.Void request, ServerCallContext context)
      {
        throw new RpcException(new Status(StatusCode.Unimplemented, ""));
      }

    }

    // client stub
    #pragma warning disable 0618
    public class WorkerServiceClient : ClientBase<WorkerServiceClient>, IWorkerServiceClient
    #pragma warning restore 0618
    {
      public WorkerServiceClient(Channel channel) : base(channel)
      {
      }
      public WorkerServiceClient(CallInvoker callInvoker) : base(callInvoker)
      {
      }
      ///<summary>Protected parameterless constructor to allow creation of test doubles.</summary>
      protected WorkerServiceClient() : base()
      {
      }
      ///<summary>Protected constructor to allow creation of configured clients.</summary>
      protected WorkerServiceClient(ClientBaseConfiguration configuration) : base(configuration)
      {
      }

      public virtual AsyncDuplexStreamingCall<global::Grpc.Testing.ServerArgs, global::Grpc.Testing.ServerStatus> RunServer(Metadata headers = null, DateTime? deadline = null, CancellationToken cancellationToken = default(CancellationToken))
      {
        return RunServer(new CallOptions(headers, deadline, cancellationToken));
      }
      public virtual AsyncDuplexStreamingCall<global::Grpc.Testing.ServerArgs, global::Grpc.Testing.ServerStatus> RunServer(CallOptions options)
      {
        return CallInvoker.AsyncDuplexStreamingCall(__Method_RunServer, null, options);
      }
      public virtual AsyncDuplexStreamingCall<global::Grpc.Testing.ClientArgs, global::Grpc.Testing.ClientStatus> RunClient(Metadata headers = null, DateTime? deadline = null, CancellationToken cancellationToken = default(CancellationToken))
      {
        return RunClient(new CallOptions(headers, deadline, cancellationToken));
      }
      public virtual AsyncDuplexStreamingCall<global::Grpc.Testing.ClientArgs, global::Grpc.Testing.ClientStatus> RunClient(CallOptions options)
      {
        return CallInvoker.AsyncDuplexStreamingCall(__Method_RunClient, null, options);
      }
      public virtual global::Grpc.Testing.CoreResponse CoreCount(global::Grpc.Testing.CoreRequest request, Metadata headers = null, DateTime? deadline = null, CancellationToken cancellationToken = default(CancellationToken))
      {
        return CoreCount(request, new CallOptions(headers, deadline, cancellationToken));
      }
      public virtual global::Grpc.Testing.CoreResponse CoreCount(global::Grpc.Testing.CoreRequest request, CallOptions options)
      {
        return CallInvoker.BlockingUnaryCall(__Method_CoreCount, null, options, request);
      }
      public virtual AsyncUnaryCall<global::Grpc.Testing.CoreResponse> CoreCountAsync(global::Grpc.Testing.CoreRequest request, Metadata headers = null, DateTime? deadline = null, CancellationToken cancellationToken = default(CancellationToken))
      {
        return CoreCountAsync(request, new CallOptions(headers, deadline, cancellationToken));
      }
      public virtual AsyncUnaryCall<global::Grpc.Testing.CoreResponse> CoreCountAsync(global::Grpc.Testing.CoreRequest request, CallOptions options)
      {
        return CallInvoker.AsyncUnaryCall(__Method_CoreCount, null, options, request);
      }
      public virtual global::Grpc.Testing.Void QuitWorker(global::Grpc.Testing.Void request, Metadata headers = null, DateTime? deadline = null, CancellationToken cancellationToken = default(CancellationToken))
      {
        return QuitWorker(request, new CallOptions(headers, deadline, cancellationToken));
      }
      public virtual global::Grpc.Testing.Void QuitWorker(global::Grpc.Testing.Void request, CallOptions options)
      {
        return CallInvoker.BlockingUnaryCall(__Method_QuitWorker, null, options, request);
      }
      public virtual AsyncUnaryCall<global::Grpc.Testing.Void> QuitWorkerAsync(global::Grpc.Testing.Void request, Metadata headers = null, DateTime? deadline = null, CancellationToken cancellationToken = default(CancellationToken))
      {
        return QuitWorkerAsync(request, new CallOptions(headers, deadline, cancellationToken));
      }
      public virtual AsyncUnaryCall<global::Grpc.Testing.Void> QuitWorkerAsync(global::Grpc.Testing.Void request, CallOptions options)
      {
        return CallInvoker.AsyncUnaryCall(__Method_QuitWorker, null, options, request);
      }
      protected override WorkerServiceClient NewInstance(ClientBaseConfiguration configuration)
      {
        return new WorkerServiceClient(configuration);
      }
    }

    // creates service definition that can be registered with a server
    #pragma warning disable 0618
    public static ServerServiceDefinition BindService(IWorkerService serviceImpl)
    #pragma warning restore 0618
    {
      return ServerServiceDefinition.CreateBuilder(__ServiceName)
          .AddMethod(__Method_RunServer, serviceImpl.RunServer)
          .AddMethod(__Method_RunClient, serviceImpl.RunClient)
          .AddMethod(__Method_CoreCount, serviceImpl.CoreCount)
          .AddMethod(__Method_QuitWorker, serviceImpl.QuitWorker).Build();
    }

    // creates service definition that can be registered with a server
    #pragma warning disable 0618
    public static ServerServiceDefinition BindService(WorkerServiceBase serviceImpl)
    #pragma warning restore 0618
    {
      return ServerServiceDefinition.CreateBuilder(__ServiceName)
          .AddMethod(__Method_RunServer, serviceImpl.RunServer)
          .AddMethod(__Method_RunClient, serviceImpl.RunClient)
          .AddMethod(__Method_CoreCount, serviceImpl.CoreCount)
          .AddMethod(__Method_QuitWorker, serviceImpl.QuitWorker).Build();
    }

    // creates a new client
    public static WorkerServiceClient NewClient(Channel channel)
    {
      return new WorkerServiceClient(channel);
    }

  }
}
#endregion
