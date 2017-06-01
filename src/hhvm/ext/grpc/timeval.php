<?hh

<<__NativeData("Timeval")>>
class Timeval {

  /**
   * Constructs a new instance of the Timeval class
   * @param long $microseconds The number of microseconds in the interval
   */
  <<__Native>>
  public function __construct(int $micrseconds): void;

  /**
   * Adds another Timeval to this one and returns the sum. Calculations saturate
   * at infinities.
   * @param Timeval $other_obj The other Timeval object to add
   * @return Timeval A new Timeval object containing the sum
   */
  <<__Native>>
  public function add(Timeval $otherTimeval): Timeval;

  /**
   * Subtracts another Timeval from this one and returns the difference.
   * Calculations saturate at infinities.
   * @param Timeval $other_obj The other Timeval object to subtract
   * @return Timeval A new Timeval object containing the diff 
   */
  <<__Native>>
  public function subtract(Timeval $otherTimeval): Timeval;

  /**
   * Return negative, 0, or positive according to whether a < b, a == b,
   * or a > b respectively.
   * @param Timeval $a_obj The first time to compare
   * @param Timeval $b_obj The second time to compare
   * @return long
   */
  <<__Native>>
  public static function compare(Timeval $timevalA, Timeval $timevalB): int;

  /**
   * Checks whether the two times are within $threshold of each other
   * @param Timeval $a_obj The first time to compare
   * @param Timeval $b_obj The second time to compare
   * @param Timeval $thresh_obj The threshold to check against
   * @return bool True if $a and $b are within $threshold, False otherwise
   */
  <<__Native>>
  public static function similar(Timeval $timevalA, Timeval $timevalB, Timeval $thresholdTimeval): bool;

  /**
   * Returns the current time as a timeval object
   * @return Timeval The current time
   */
  <<__Native>>
  public static function now(): Timeval;

  /**
   * Returns the zero time interval as a timeval object
   * @return Timeval Zero length time interval
   */
  <<__Native>>
  public static function zero(): Timeval;

  /**
   * Returns the infinite future time value as a timeval object
   * @return Timeval Infinite future time value
   */
  <<__Native>>
  public static function infFuture(): Timeval;

  /**
   * Returns the infinite past time value as a timeval object
   * @return Timeval Infinite past time value
   */
  <<__Native>>
  public static function infPast(): Timeval;

  /**
   * Sleep until this time, interpreted as an absolute timeout
   * @return void
   */
  <<__Native>>
  public function sleepUntil(): void;
}
