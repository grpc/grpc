using Android.App;
using Android.Widget;
using Android.OS;
using Grpc.Core;
using Helloworld;

namespace HelloworldXamarin.Droid
{
    [Activity(Label = "HelloworldXamarin", MainLauncher = true, Icon = "@mipmap/icon")]
    public class MainActivity : Activity
    {
        //int count = 1;

        protected override void OnCreate(Bundle savedInstanceState)
        {
            base.OnCreate(savedInstanceState);

            // Set our view from the "main" layout resource
            SetContentView(Resource.Layout.Main);

            // Get our button from the layout resource,
            // and attach an event to it
            Button button = FindViewById<Button>(Resource.Id.myButton);

            button.Click += delegate { SayHello(button); };
        }

            private void SayHello(Button button)
            {

              // use loopback on host machine: https://developer.android.com/studio/run/emulator-networking
              Channel channel = new Channel("10.0.2.2:50051", ChannelCredentials.Insecure);

              var client = new Greeter.GreeterClient(channel);
              string user = "Xamarin";

              var reply = client.SayHello(new HelloRequest { Name = user });

              button.Text = "Greeting: " + reply.Message;

              channel.ShutdownAsync().Wait();
            }
    }

    
}

