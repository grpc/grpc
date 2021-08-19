package io.grpc.binder.cpp.benchmarks.server;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.widget.Button;
import android.widget.TextView;
import io.grpc.binder.cpp.benchmarks.server.R;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.os.Binder;
import android.os.IBinder;
import android.util.Log;

/** Main class for the example app. */
public class GrpcService extends Service {
  @Override
  public void onCreate() {
    super.onCreate();
    Log.v("Example", "hello, world");

    setContentView(R.layout.activity_main);

    Button clickMeButton = findViewById(R.id.clickMeButton);
    TextView exampleTextView = findViewById(R.id.exampleTextView);

    ButtonPressHandler h = new ButtonPressHandler();

    clickMeButton.setOnClickListener(
        v -> exampleTextView.setText(h.onPressed(getApplication())));

    Intent notificationIntent = new Intent();
    PendingIntent pendingIntent = PendingIntent.getActivity(this, 0, notificationIntent, 0);
    String channelId = "io.grpc.binder.cpp.benchmarks.server";
    createNotificationChannel(channelId, "debug endpoint service");

    Notification noti =
        new Notification.Builder(this, channelId)
            .setContentTitle(getText(android.R.string.untitled))
            .setContentText("gRPC service")
            .setSmallIcon(android.R.drawable.ic_menu_search)
            .setContentIntent(pendingIntent)
            .setTicker(getText(android.R.string.untitled))
            .build();
    startForeground(1, noti);
    startGrpcServer();
  }
  private String createNotificationChannel(String channelId, String channelName) {
    NotificationChannel chan =
        new NotificationChannel(channelId, channelName, NotificationManager.IMPORTANCE_NONE);
    chan.setLightColor(Color.BLUE);
    chan.setLockscreenVisibility(Notification.VISIBILITY_PRIVATE);
    NotificationManager service =
        (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
    service.createNotificationChannel(chan);
    return channelId;
  }

  private static native void startGrpcServer();
}
