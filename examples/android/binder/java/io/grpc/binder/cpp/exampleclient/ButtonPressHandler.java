package io.grpc.binder.cpp.exampleclient;

import android.app.Application;

public class ButtonPressHandler {
  static {
    System.loadLibrary("app");
  }

  public native String native_entry(Application application);

  public String onPressed(Application application) {
    return native_entry(application);
  }
}
