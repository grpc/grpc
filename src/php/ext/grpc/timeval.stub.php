<?php

/**
 * @generate-class-entries
 * @generate-function-entries
 * @generate-legacy-arginfo
 */

namespace Grpc;

class Timeval
{

  /**
   * Constructs a new instance of the Timeval class
   *
   * @param int $microseconds The number of microseconds in the interval
   */
  public function __construct(int $microseconds)
  {

  }

  /**
   * Adds another Timeval to this one and returns the sum. Calculations saturate
   * at infinities.
   *
   * @param Timeval $timeval The other Timeval object to add
   *
   * @return Timeval A new Timeval object containing the sum
   */
  public function add(Timeval $timeval): Timeval
  {

  }

  /**
   * Subtracts another Timeval from this one and returns the difference.
   * Calculations saturate at infinities.
   *
   * @param Timeval $timeval The other Timeval object to subtract
   *
   * @return Timeval A new Timeval object containing the diff
   */
  public function subtract(Timeval $timeval): Timeval
  {

  }

  /**
   * Return negative, 0, or positive according to whether a < b, a == b,
   * or a > b respectively.
   *
   * @param Timeval $a_timeval The first time to compare
   * @param Timeval $b_timeval The second time to compare
   *
   * @return int
   */
  public static function compare(Timeval $a_timeval, Timeval $b_timeval): int
  {

  }

  /**
   * Checks whether the two times are within $threshold_timeval of each other
   *
   * @param Timeval $a_timeval      The first time to compare
   * @param Timeval $b_timeval      The second time to compare
   * @param Timeval $threshold_timeval The threshold to check against
   *
   * @return bool True if $a and $b are within $threshold_timeval, False otherwise
   */
  public static function similar(Timeval $a_timeval, Timeval $b_timeval, Timeval $threshold_timeval): bool
  {

  }

  /**
   * Returns the current time as a timeval object
   *
   * @return Timeval The current time
   */
  public static function now(): Timeval
  {

  }

  /**
   * Returns the zero time interval as a timeval object
   *
   * @return Timeval Zero length time interval
   */
  public static function zero(): Timeval
  {

  }

  /**
   * Returns the infinite future time value as a timeval object
   *
   * @return Timeval Infinite future time value
   */
  public static function infFuture(): Timeval
  {

  }

  /**
   * Returns the infinite past time value as a timeval object
   *
   * @return Timeval Infinite past time value
   */
  public static function infPast(): Timeval
  {

  }

  /**
   * Sleep until this time, interpreted as an absolute timeout
   *
   * @return void
   */
  public function sleepUntil(): void
  {

  }
}
