using Android.App;
using Android.Widget;
using Android.OS;
using System.Threading.Tasks;
using Grpc.Core;
using Helloworld;

namespace HelloworldXamarin.Droid
{
    [Activity(Label = "HelloworldXamarin", MainLauncher = true, Icon = "@mipmap/icon")]
    public class MainActivity : Activity
    {
        const int Port = 50051;
        int count = 1;

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
            Server server = new Server
            {
              Services = { Greeter.BindService(new GreeterImpl()) },
              Ports = { new ServerPort("localhost", Port, ServerCredentials.Insecure) }
            };
            server.Start();

            // use loopback on host machine: https://developer.android.com/studio/run/emulator-networking
            //10.0.2.2:50051
            Channel channel = new Channel("localhost:50051", ChannelCredentials.Insecure);

            var client = new Greeter.GreeterClient(channel);
            string user = "Xamarin " + count;

            var reply = client.SayHello(new HelloRequest { Name = user });

            button.Text = "Greeting: " + reply.Message;

            channel.ShutdownAsync().Wait();
            server.ShutdownAsync().Wait();

            count++;
        }

        class GreeterImpl : Greeter.GreeterBase
        {
            // Server side handler of the SayHello RPC
            public override Task<HelloReply> SayHello(HelloRequest request, ServerCallContext context)
            {
              return Task.FromResult(new HelloReply { Message = "Hello " + request.Name });
            }
        }
    }
}

