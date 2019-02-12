#region Copyright notice and license

// Copyright 2019 The gRPC Authors
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

#endregion

using UnityEngine;
using UnityEngine.UI;

public class HelloWorldScript : MonoBehaviour {
  int counter = 1;

  // Use this for initialization
  void Start () {}

  // Update is called once per frame
  void Update() {}

  // Ran when button is clicked
  public void RunHelloWorld(Text text)
  {
    var reply = HelloWorldTest.Greet("Unity " + counter);
    text.text = "Greeting: " + reply.Message;
    counter++;
  }
}
