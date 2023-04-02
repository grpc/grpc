// Copyright 2021 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package io.grpc.binder.cpp;

import static android.content.Intent.URI_ANDROID_APP_SCHEME;
import static android.content.Intent.URI_INTENT_SCHEME;

import android.annotation.TargetApi;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.IBinder;
import android.util.Log;
import java.net.URISyntaxException;

/* Handles the binder connection state with OnDeviceServer server */
public class GrpcBinderConnection implements ServiceConnection {
  private static final String logTag = "GrpcBinderConnection";

  private Context mContext;
  private IBinder mService;

  // A string that identifies this service connection
  private final String mConnId;

  public GrpcBinderConnection(Context context, String connId) {
    mContext = context;
    mConnId = connId;
  }

  @Override
  public void onNullBinding(ComponentName className) {
    // TODO(mingcl): Notify C++ that the connection is never going to happen
    Log.e(logTag, "Service returned null IBinder. mConnId = " + mConnId);
  }

  @Override
  public void onServiceConnected(ComponentName className, IBinder service) {
    Log.e(logTag, "Service has connected. mConnId = " + mConnId);
    if (service == null) {
      // This should not happen since onNullBinding should be invoked instead
      throw new IllegalArgumentException("service was null");
    }
    synchronized (this) {
      mService = service;
    }
    notifyConnected(mConnId, mService);
  }

  @Override
  public void onServiceDisconnected(ComponentName className) {
    Log.e(logTag, "Service has disconnected. mConnId = " + mConnId);
  }

  public void tryConnect(String pkg, String cls, String action_name) {
    Intent intent = new Intent(action_name);
    ComponentName compName = new ComponentName(pkg, cls);
    intent.setComponent(compName);
    tryConnect(intent);
  }

  @TargetApi(22)
  public void tryConnect(String uri) {
    // Try connect with an URI that can be parsed as intent.
    try {
      tryConnect(Intent.parseUri(uri, URI_ANDROID_APP_SCHEME | URI_INTENT_SCHEME));
    } catch (URISyntaxException e) {
      Log.e(logTag, "Unable to parse the Uri: " + uri);
    }
  }

  private void tryConnect(Intent intent) {
    synchronized (this) {
      // Will return true if the system is in the process of bringing up a service that your client
      // has permission to bind to; false if the system couldn't find the service or if your client
      // doesn't have permission to bind to it
      boolean result = mContext.bindService(intent, this, Context.BIND_AUTO_CREATE);
      if (result) {
        Log.e(logTag, "bindService returns ok");
      } else {
        Log.e(
            logTag,
            "bindService failed. Maybe the system couldn't find the service or the"
                + " client doesn't have permission to bind to it.");
      }
    }
  }

  // Calls a function defined in endpoint_binder_pool.cc
  private static native void notifyConnected(String connId, IBinder service);
}
