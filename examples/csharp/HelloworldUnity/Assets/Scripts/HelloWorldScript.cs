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
