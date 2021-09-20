package io.grpc.binder.cpp.exampleclient;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.widget.Button;
import android.widget.TextView;

/** Main class for the example app. */
public class MainActivity extends Activity {
  @Override
  public void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    Log.v("Example", "hello, world");

    setContentView(R.layout.activity_main);

    Button clickMeButton = findViewById(R.id.clickMeButton);
    TextView exampleTextView = findViewById(R.id.exampleTextView);

    ButtonPressHandler h = new ButtonPressHandler();

    clickMeButton.setOnClickListener(
        v -> exampleTextView.setText(h.onPressed(getApplication())));
  }
}
