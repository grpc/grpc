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

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.IBinder;
import android.util.Log;

/* Connects to a service synchronously */
public class SyncServiceConnection implements ServiceConnection {
  private final String logTag = "SyncServiceConnection";

  private Context mContext;
  private IBinder mService;

  public SyncServiceConnection(Context context) {
    mContext = context;
  }

  @Override
  public void onServiceConnected(ComponentName className, IBinder service) {
    Log.e(logTag, "Service has connected: ");
    synchronized (this) {
      mService = service;
    }
  }

  @Override
  public void onServiceDisconnected(ComponentName className) {
    Log.e(logTag, "Service has disconnected: ");
  }

  public void tryConnect(String pkg, String cls) {
    synchronized (this) {
      Intent intent = new Intent("grpc.io.action.BIND");
      ComponentName compName = new ComponentName(pkg, cls);
      intent.setComponent(compName);
      // Will return true if the system is in the process of bringing up a service that your client
      // has permission to bind to; false if the system couldn't find the service or if your client
      // doesn't have permission to bind to it
      boolean result = mContext.bindService(intent, this, Context.BIND_AUTO_CREATE);
      if (result) {
        Log.e(logTag, "bindService ok");
      } else {
        Log.e(logTag, "bindService not ok");
      }
    }
  }

  public IBinder getIBinder() {
    return mService;
  }
}
