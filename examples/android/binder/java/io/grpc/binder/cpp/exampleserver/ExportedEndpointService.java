package io.grpc.binder.cpp.exampleserver;

import android.app.Service;
import android.os.IBinder;
import android.content.Intent;

/** Exposes gRPC services running in the main process */
public final class ExportedEndpointService extends Service {
  private final IBinder binder;

  static {
    System.loadLibrary("app");
  }

  public ExportedEndpointService() {
    init_grpc_server();
    binder = get_endpoint_binder();
  }

  @Override
  public IBinder onBind(Intent intent) {
    return binder;
  }

  public native void init_grpc_server();

  public native IBinder get_endpoint_binder();
}
