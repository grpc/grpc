// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

using Helloworld;
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
