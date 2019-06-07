using Greet;
using Grpc.Core;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;

namespace GreeterClient
{
  public sealed partial class MainPage : Page
  {
    public MainPage()
    {
      this.InitializeComponent();
      Loaded += MainPage_Loaded;
    }

    private async void MainPage_Loaded(object sender, RoutedEventArgs e)
    {
      // Target address has to correspond to the GreeterService.
      var channel = new Channel("localhost:50051", ChannelCredentials.Insecure);
      var client = new Greeter.GreeterClient(channel);
      var reply = await client.SayHelloAsync(new HelloRequest { Name = "UWP GreeterClient" });
      HelloText.Text = ("Greeting: " + reply.Message);
      await channel.ShutdownAsync();
    }
  }
}
