package io.grpc.binder.cpp.exampleserver;

import android.app.Application;

public class ButtonPressHandler {
  static {
    System.loadLibrary("app");
  }

  public String onPressed(Application application) {
    return "Server Button Pressed";
  }
}
