using System;
using Grpc.Core;
using Helloworld;

using UIKit;

namespace HelloworldXamarin.iOS
{
    public partial class ViewController : UIViewController
    {
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

              // use loopback on host machine: https://developer.android.com/studio/run/emulator-networking
              Channel channel = new Channel("10.0.2.2:50051", ChannelCredentials.Insecure);

              var client = new Greeter.GreeterClient(channel);
              string user = "Xamarin";

              var reply = client.SayHello(new HelloRequest { Name = user });

              channel.ShutdownAsync().Wait();

              return "Greeting: " + reply.Message;
            }
    }
}
