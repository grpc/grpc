package io.grpc.helloworldexample;

import android.content.Context;
import android.support.v7.app.ActionBarActivity;
import android.os.Bundle;
import android.os.AsyncTask;
import android.text.TextUtils;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.TextView;

import io.grpc.ChannelImpl;
import ex.grpc.GreeterGrpc;
import ex.grpc.Helloworld.HelloRequest;
import ex.grpc.Helloworld.HelloReply;
import io.grpc.transport.okhttp.OkHttpChannelBuilder;

import java.util.concurrent.TimeUnit;

public class Helloworld extends ActionBarActivity {
    private Button mSendButton;
    private EditText mHostEdit;
    private EditText mPortEdit;
    private EditText mMessageEdit;
    private TextView mResultText;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_helloworld);
        mSendButton = (Button) findViewById(R.id.send_button);
        mHostEdit = (EditText) findViewById(R.id.host_edit_text);
        mPortEdit = (EditText) findViewById(R.id.port_edit_text);
        mMessageEdit = (EditText) findViewById(R.id.message_edit_text);
        mResultText = (TextView) findViewById(R.id.grpc_response_text);
    }

    public void sendMessage(View view) {
        ((InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE))
                .hideSoftInputFromWindow(mHostEdit.getWindowToken(), 0);
        mSendButton.setEnabled(false);
        new GrpcTask().execute();
    }

    private class GrpcTask extends AsyncTask<Void, Void, String> {
        private String mHost;
        private String mMessage;
        private int mPort;
        private ChannelImpl mChannel;

        @Override
        protected void onPreExecute() {
            mHost = mHostEdit.getText().toString();
            mMessage = mMessageEdit.getText().toString();
            String portStr = mPortEdit.getText().toString();
            mPort = TextUtils.isEmpty(portStr) ? 0 : Integer.valueOf(portStr);
            mResultText.setText("");
        }

        private String sayHello(ChannelImpl channel) {
            GreeterGrpc.GreeterBlockingStub stub = GreeterGrpc.newBlockingStub(channel);
            HelloRequest message = new HelloRequest();
            message.name = mMessage;
            HelloReply reply = stub.sayHello(message);
            return reply.message;
        }

        @Override
        protected String doInBackground(Void... nothing) {
            try {
                mChannel = OkHttpChannelBuilder.forAddress(mHost, mPort).build();
                return sayHello(mChannel);
            } catch (Exception e) {
                return "Failed... : " + e.getMessage();
            }
        }

        @Override
        protected void onPostExecute(String result) {
            try {
                mChannel.shutdown().awaitTerminated(1, TimeUnit.SECONDS);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
            mResultText.setText(result);
            mSendButton.setEnabled(true);
        }
    }
}
