using System;
using System.Threading.Tasks;

using Grpc.Core;
using Helloworld;

using UIKit;

namespace HelloworldXamarin.iOS
{
    public partial class ViewController : UIViewController
    {
        const int Port = 50051;
        int count = 1;

        public ViewController(IntPtr handle) : base(handle)
        {
        }

        public override void ViewDidLoad()
        {
            base.ViewDidLoad();

            // Perform any additional setup after loading the view, typically from a nib.
            Button.AccessibilityIdentifier = "myButton";
            Button.TouchUpInside += delegate
            {
                var title = SayHello();
                Button.SetTitle(title, UIControlState.Normal);
            };
        }

        public override void DidReceiveMemoryWarning()
        {
            base.DidReceiveMemoryWarning();
            // Release any cached data, images, etc that aren't in use.		
        }

        private string SayHello()
        {
            Server server = new Server
            {
                Services = { Greeter.BindService(new GreeterImpl()) },
                Ports = { new ServerPort("localhost", Port, ServerCredentials.Insecure) }
            };
            server.Start();

            Channel channel = new Channel("localhost:50051", ChannelCredentials.Insecure);

            var client = new Greeter.GreeterClient(channel);
            string user = "Xamarin " + count;

            var reply = client.SayHello(new HelloRequest { Name = user });

            channel.ShutdownAsync().Wait();
            server.ShutdownAsync().Wait();

            count++;

            return "Greeting: " + reply.Message;
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
