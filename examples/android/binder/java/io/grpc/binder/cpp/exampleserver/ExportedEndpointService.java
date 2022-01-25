package io.grpc.binder.cpp.exampleserver;

import android.app.Service;
import android.content.Intent;
import android.os.IBinder;
import android.content.Context;
import io.grpc.binder.cpp.GrpcCppServerBuilder;

/** Exposes gRPC services running in the main process */
public final class ExportedEndpointService extends Service {
  static {
    System.loadLibrary("app");
  }

  public ExportedEndpointService() {
    init_grpc_server(this);
  }

  @Override
  public IBinder onBind(Intent intent) {
    // The argument should match the URI passed into grpc::ServerBuilder::AddListeningPort
    return GrpcCppServerBuilder.GetEndpointBinder("binder:example.service");
  }

  public native void init_grpc_server(Context context);
}
