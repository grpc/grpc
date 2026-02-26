<?php

/**
 * @generate-class-entries
 * @generate-function-entries
 * @generate-legacy-arginfo
 */

namespace Grpc;

use Closure;

class CallCredentials
{

  /**
   * Create composite credentials from two existing credentials.
   *
   * @param CallCredentials $creds1 The first credential
   * @param CallCredentials $creds2 The second credential
   *
   * @return CallCredentials The new composite credentials object
   */
  public static function createComposite(
    CallCredentials $creds1,
    CallCredentials $creds2
  ): CallCredentials {
  }

  /**
   * Create a call credentials object from the plugin API
   *
   * @param Closure $callback The callback function
   *
   * @return CallCredentials The new call credentials object
   */
  public static function createFromPlugin(Closure $callback): CallCredentials
  {
  }
}
