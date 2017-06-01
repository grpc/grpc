<?hh

<<__NativeData("CallCredentials")>>
class CallCredentials {

  /**
   * Create composite credentials from two existing credentials.
   * @param CallCredentials $cred1_obj The first credential
   * @param CallCredentials $cred2_obj The second credential
   * @return CallCredentials The new composite credentials object
   */
  <<__Native>>
  public function createComposite(CallCredentials $cred1, CallCredentials $cred2): CallCredentials;

  /**
   * Create a call credentials object from the plugin API
   * @param function $fci The callback function
   * @return CallCredentials The new call credentials object
   */
  <<__Native>>
  public function createFromPlugin((function(string, string): array) $callback): CallCredentials;

}
