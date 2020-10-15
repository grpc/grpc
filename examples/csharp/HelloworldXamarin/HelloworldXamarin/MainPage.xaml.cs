using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Xamarin.Forms;
using Grpc.Core;
using Helloworld;

namespace HelloworldXamarin
{
    public partial class MainPage : ContentPage
    {
        const int Port = 30051;
        int count = 1;

        public MainPage()
        {
            InitializeComponent();
        }

        private void Button_Clicked(object sender, EventArgs e)
        {
            SayHello(sender as Button);
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
            // 10.0.2.2:30051
            Channel channel = new Channel("localhost:30051", ChannelCredentials.Insecure);

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
