<?php
/*
 *
 * Copyright 2017 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

// Histogram class for use in performance testing and measurement
class Histogram {
  private $resolution;
  private $max_possible;
  private $sum;
  private $sum_of_squares;
  private $multiplier;
  private $count;
  private $min_seen;
  private $max_seen;
  private $buckets;

  private function bucket_for($value) {
    return (int)(log($value) / log($this->multiplier));
  }

  public function __construct($resolution, $max_possible) {
    $this->resolution = $resolution;
    $this->max_possible = $max_possible;
    $this->sum = 0;
    $this->sum_of_squares = 0;
    $this->multiplier = 1+$resolution;
    $this->count = 0;
    $this->min_seen = $max_possible;
    $this->max_seen = 0;
    $this->buckets = array_fill(0, $this->bucket_for($max_possible)+1, 0);
  }

  public function add($value) {
    $this->sum += $value;
    $this->sum_of_squares += $value * $value;
    $this->count += 1;
    if ($value < $this->min_seen) {
      $this->min_seen = $value;
    }
    if ($value > $this->max_seen) {
      $this->max_seen = $value;
    }
    $this->buckets[$this->bucket_for($value)] += 1;
  }

  public function minimum() {
    return $this->min_seen;
  }

  public function maximum() {
    return $this->max_seen;
  }

  public function sum() {
    return $this->sum;
  }

  public function sum_of_squares() {
    return $this->sum_of_squares;
  }

  public function count() {
    return $this->count;
  }

  public function contents() {
    return $this->buckets;
  }

  public function clean() {
    $this->sum = 0;
    $this->sum_of_squares = 0;
    $this->count = 0;
    $this->min_seen = $this->max_possible;
    $this->max_seen = 0;
    $this->buckets = array_fill(0, $this->bucket_for($this->max_possible)+1, 0);
  }
}
