package io.grpc.kotlin

import kotlin.reflect.KClass

@Target(AnnotationTarget.CLASS)
@Retention(AnnotationRetention.BINARY)
annotation class StubFor(val value: KClass<*>)
