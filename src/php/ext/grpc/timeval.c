/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "timeval.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <php.h>
#include <php_ini.h>
#include <ext/standard/info.h>
#include <ext/spl/spl_exceptions.h>
#include "php_grpc.h"

#include <zend_exceptions.h>

#include <stdbool.h>

#include <grpc/grpc.h>
#include <grpc/support/time.h>

zend_class_entry *grpc_ce_timeval;
#if PHP_MAJOR_VERSION >= 7
static zend_object_handlers timeval_ce_handlers;
#endif

/* Frees and destroys an instance of wrapped_grpc_call */
PHP_GRPC_FREE_WRAPPED_FUNC_START(wrapped_grpc_timeval)
PHP_GRPC_FREE_WRAPPED_FUNC_END()

/* Initializes an instance of wrapped_grpc_timeval to be associated with an
 * object of a class specified by class_type */
php_grpc_zend_object create_wrapped_grpc_timeval(zend_class_entry *class_type
                                                 TSRMLS_DC) {
  PHP_GRPC_ALLOC_CLASS_OBJECT(wrapped_grpc_timeval);
  zend_object_std_init(&intern->std, class_type TSRMLS_CC);
  object_properties_init(&intern->std, class_type);
  PHP_GRPC_FREE_CLASS_OBJECT(wrapped_grpc_timeval, timeval_ce_handlers);
}

zval *grpc_php_wrap_timeval(gpr_timespec wrapped TSRMLS_DC) {
  zval *timeval_object;
  PHP_GRPC_MAKE_STD_ZVAL(timeval_object);
  object_init_ex(timeval_object, grpc_ce_timeval);
  wrapped_grpc_timeval *timeval = Z_WRAPPED_GRPC_TIMEVAL_P(timeval_object);
  memcpy(&timeval->wrapped, &wrapped, sizeof(gpr_timespec));
  return timeval_object;
}

/**
 * Constructs a new instance of the Timeval class
 * @param long $microseconds The number of microseconds in the interval
 */
PHP_METHOD(Timeval, __construct) {
  wrapped_grpc_timeval *timeval = Z_WRAPPED_GRPC_TIMEVAL_P(getThis());
  php_grpc_long microseconds;

  /* "l" == 1 long */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "l", &microseconds) ==
      FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "Timeval expects a long", 1 TSRMLS_CC);
    return;
  }
  gpr_timespec time = gpr_time_from_micros(microseconds, GPR_TIMESPAN);
  memcpy(&timeval->wrapped, &time, sizeof(gpr_timespec));
}

/**
 * Adds another Timeval to this one and returns the sum. Calculations saturate
 * at infinities.
 * @param Timeval $other_obj The other Timeval object to add
 * @return Timeval A new Timeval object containing the sum
 */
PHP_METHOD(Timeval, add) {
  zval *other_obj;

  /* "O" == 1 Object */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "O", &other_obj,
                            grpc_ce_timeval) == FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "add expects a Timeval", 1 TSRMLS_CC);
    return;
  }
  wrapped_grpc_timeval *self = Z_WRAPPED_GRPC_TIMEVAL_P(getThis());
  wrapped_grpc_timeval *other = Z_WRAPPED_GRPC_TIMEVAL_P(other_obj);
  zval *sum =
    grpc_php_wrap_timeval(gpr_time_add(self->wrapped, other->wrapped)
                          TSRMLS_CC);
  RETURN_DESTROY_ZVAL(sum);
}

/**
 * Subtracts another Timeval from this one and returns the difference.
 * Calculations saturate at infinities.
 * @param Timeval $other_obj The other Timeval object to subtract
 * @return Timeval A new Timeval object containing the diff 
 */
PHP_METHOD(Timeval, subtract) {
  zval *other_obj;

  /* "O" == 1 Object */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "O", &other_obj,
                            grpc_ce_timeval) == FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "subtract expects a Timeval", 1 TSRMLS_CC);
    return;
  }
  wrapped_grpc_timeval *self = Z_WRAPPED_GRPC_TIMEVAL_P(getThis());
  wrapped_grpc_timeval *other = Z_WRAPPED_GRPC_TIMEVAL_P(other_obj);
  zval *diff =
    grpc_php_wrap_timeval(gpr_time_sub(self->wrapped, other->wrapped)
                          TSRMLS_CC);
  RETURN_DESTROY_ZVAL(diff);
}

/**
 * Return negative, 0, or positive according to whether a < b, a == b,
 * or a > b respectively.
 * @param Timeval $a_obj The first time to compare
 * @param Timeval $b_obj The second time to compare
 * @return long
 */
PHP_METHOD(Timeval, compare) {
  zval *a_obj;
  zval *b_obj;

  /* "OO" == 2 Objects */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "OO", &a_obj,
                            grpc_ce_timeval, &b_obj,
                            grpc_ce_timeval) == FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "compare expects two Timevals", 1 TSRMLS_CC);
    return;
  }
  wrapped_grpc_timeval *a = Z_WRAPPED_GRPC_TIMEVAL_P(a_obj);
  wrapped_grpc_timeval *b = Z_WRAPPED_GRPC_TIMEVAL_P(b_obj);
  long result = gpr_time_cmp(a->wrapped, b->wrapped);
  RETURN_LONG(result);
}

/**
 * Checks whether the two times are within $threshold of each other
 * @param Timeval $a_obj The first time to compare
 * @param Timeval $b_obj The second time to compare
 * @param Timeval $thresh_obj The threshold to check against
 * @return bool True if $a and $b are within $threshold, False otherwise
 */
PHP_METHOD(Timeval, similar) {
  zval *a_obj;
  zval *b_obj;
  zval *thresh_obj;

  /* "OOO" == 3 Objects */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "OOO", &a_obj,
                            grpc_ce_timeval, &b_obj, grpc_ce_timeval,
                            &thresh_obj, grpc_ce_timeval) == FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "compare expects three Timevals", 1 TSRMLS_CC);
    return;
  }
  wrapped_grpc_timeval *a = Z_WRAPPED_GRPC_TIMEVAL_P(a_obj);
  wrapped_grpc_timeval *b = Z_WRAPPED_GRPC_TIMEVAL_P(b_obj);
  wrapped_grpc_timeval *thresh = Z_WRAPPED_GRPC_TIMEVAL_P(thresh_obj);
  int result = gpr_time_similar(a->wrapped, b->wrapped, thresh->wrapped);
  RETURN_BOOL(result);
}

/**
 * Returns the current time as a timeval object
 * @return Timeval The current time
 */
PHP_METHOD(Timeval, now) {
  zval *now = grpc_php_wrap_timeval(gpr_now(GPR_CLOCK_REALTIME) TSRMLS_CC);
  RETURN_DESTROY_ZVAL(now);
}

/**
 * Returns the zero time interval as a timeval object
 * @return Timeval Zero length time interval
 */
PHP_METHOD(Timeval, zero) {
  zval *grpc_php_timeval_zero =
    grpc_php_wrap_timeval(gpr_time_0(GPR_CLOCK_REALTIME) TSRMLS_CC);
  RETURN_DESTROY_ZVAL(grpc_php_timeval_zero);
}

/**
 * Returns the infinite future time value as a timeval object
 * @return Timeval Infinite future time value
 */
PHP_METHOD(Timeval, infFuture) {
  zval *grpc_php_timeval_inf_future =
    grpc_php_wrap_timeval(gpr_inf_future(GPR_CLOCK_REALTIME) TSRMLS_CC);
  RETURN_DESTROY_ZVAL(grpc_php_timeval_inf_future);
}

/**
 * Returns the infinite past time value as a timeval object
 * @return Timeval Infinite past time value
 */
PHP_METHOD(Timeval, infPast) {
  zval *grpc_php_timeval_inf_past =
    grpc_php_wrap_timeval(gpr_inf_past(GPR_CLOCK_REALTIME) TSRMLS_CC);
  RETURN_DESTROY_ZVAL(grpc_php_timeval_inf_past);
}

/**
 * Sleep until this time, interpreted as an absolute timeout
 * @return void
 */
PHP_METHOD(Timeval, sleepUntil) {
  wrapped_grpc_timeval *this = Z_WRAPPED_GRPC_TIMEVAL_P(getThis());
  gpr_sleep_until(this->wrapped);
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_construct, 0, 0, 1)
  ZEND_ARG_INFO(0, microseconds)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_add, 0, 0, 1)
  ZEND_ARG_INFO(0, timeval)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_compare, 0, 0, 2)
  ZEND_ARG_INFO(0, a_timeval)
  ZEND_ARG_INFO(0, b_timeval)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_infFuture, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_infPast, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_now, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_similar, 0, 0, 3)
  ZEND_ARG_INFO(0, a_timeval)
  ZEND_ARG_INFO(0, b_timeval)
  ZEND_ARG_INFO(0, threshold_timeval)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sleepUntil, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_subtract, 0, 0, 1)
  ZEND_ARG_INFO(0, timeval)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_zero, 0, 0, 0)
ZEND_END_ARG_INFO()

static zend_function_entry timeval_methods[] = {
  PHP_ME(Timeval, __construct, arginfo_construct,
         ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
  PHP_ME(Timeval, add, arginfo_add,
         ZEND_ACC_PUBLIC)
  PHP_ME(Timeval, compare, arginfo_compare,
         ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
  PHP_ME(Timeval, infFuture, arginfo_infFuture,
         ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
  PHP_ME(Timeval, infPast, arginfo_infPast,
         ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
  PHP_ME(Timeval, now, arginfo_now,
         ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
  PHP_ME(Timeval, similar, arginfo_similar,
         ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
  PHP_ME(Timeval, sleepUntil, arginfo_sleepUntil,
         ZEND_ACC_PUBLIC)
  PHP_ME(Timeval, subtract, arginfo_subtract,
         ZEND_ACC_PUBLIC)
  PHP_ME(Timeval, zero, arginfo_zero,
         ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
  PHP_FE_END
};

void grpc_init_timeval(TSRMLS_D) {
  zend_class_entry ce;
  INIT_CLASS_ENTRY(ce, "Grpc\\Timeval", timeval_methods);
  ce.create_object = create_wrapped_grpc_timeval;
  grpc_ce_timeval = zend_register_internal_class(&ce TSRMLS_CC);
  PHP_GRPC_INIT_HANDLER(wrapped_grpc_timeval, timeval_ce_handlers);
}

void grpc_shutdown_timeval(TSRMLS_D) {}
