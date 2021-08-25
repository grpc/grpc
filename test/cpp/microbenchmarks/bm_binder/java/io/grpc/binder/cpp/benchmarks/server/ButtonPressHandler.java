package io.grpc.binder.cpp.benchmarks.server;

import android.app.Application;

public class ButtonPressHandler {
  static {
    System.loadLibrary("app");
  }

  public String onPressed(Application application) {
    return "Server Button Pressed";
  }
}
