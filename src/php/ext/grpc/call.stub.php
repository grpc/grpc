<?php

/**
 * @generate-class-entries
 * @generate-function-entries
 * @generate-legacy-arginfo
 */

namespace Grpc;

use stdClass;

class Call
{
  /**
   * Constructs a new instance of the Call class.
   *
   * @param Channel     $channel       The channel to associate the call with.
   *                                   Must not be closed.
   * @param string      $method        The method to call
   * @param Timeval     $deadline      The deadline for completing the call
   * @param string|null $host_override The host is set by user (optional)
   */
  public function __construct(
    Channel $channel,
    string  $method,
    Timeval $deadline,
    ?string $host_override = null)
  {
  }

  /**
   * Start a batch of RPC actions.
   *
   * @todo better description for $ops
   *
   * @param array<int, mixed> $ops Array of actions to take
   *
   * @return stdClass&object{message?: string, send_metadata?: bool, send_message?: bool, send_close?: bool, send_status?: bool, metadata?: array<string, string[]>, status?: object{code: int, metadata: array<string, string[]>, details: string}, cancelled?: bool} Object with results of all actions
   */
  public function startBatch(array $ops): stdClass
  {
  }

  /**
   * Get the endpoint this call/stream is connected to
   *
   * @return string The URI of the endpoint
   */
  public function getPeer(): string
  {
  }

  /**
   * Cancel the call. This will cause the call to end with STATUS_CANCELLED
   * if it has not already ended with another status.
   *
   * @return void
   */
  public function cancel(): void
  {
  }

  /**
   * Set the CallCredentials for this call.
   *
   * @param CallCredentials $credentials The CallCredentials object
   *
   * @return int The error code
   */
  public function setCredentials(CallCredentials $credentials): int
  {
  }
}
