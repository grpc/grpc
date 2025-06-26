"""The abstract values used by vm.py.

This file contains BaseValue and its subclasses. Mixins such as Class
are in mixin.py, and other abstract logic is in abstract_utils.py.
"""

import logging

from pytype.abstract import _base
from pytype.abstract import _classes
from pytype.abstract import _function_base
from pytype.abstract import _instance_base
from pytype.abstract import _instances
from pytype.abstract import _interpreter_function
from pytype.abstract import _pytd_function
from pytype.abstract import _singletons
from pytype.abstract import _typing
from pytype.abstract import class_mixin
from pytype.abstract import mixin

log = logging.getLogger(__name__)

# For simplicity, we pretend all abstract values are defined in abstract.py.
BaseValue = _base.BaseValue

# These are technically mixins, but we use them a lot in isinstance() checks.
Class = class_mixin.Class
PythonConstant = mixin.PythonConstant

BuildClass = _classes.BuildClass
InterpreterClass = _classes.InterpreterClass
PyTDClass = _classes.PyTDClass
FunctionPyTDClass = _classes.FunctionPyTDClass
ParameterizedClass = _classes.ParameterizedClass
CallableClass = _classes.CallableClass
LiteralClass = _classes.LiteralClass
TupleClass = _classes.TupleClass

Function = _function_base.Function
NativeFunction = _function_base.NativeFunction
BoundFunction = _function_base.BoundFunction
BoundInterpreterFunction = _function_base.BoundInterpreterFunction
BoundPyTDFunction = _function_base.BoundPyTDFunction
ClassMethod = _function_base.ClassMethod
StaticMethod = _function_base.StaticMethod
Property = _function_base.Property
SignedFunction = _function_base.SignedFunction
SimpleFunction = _function_base.SimpleFunction

SimpleValue = _instance_base.SimpleValue
Instance = _instance_base.Instance

LazyConcreteDict = _instances.LazyConcreteDict
ConcreteValue = _instances.ConcreteValue
Module = _instances.Module
Coroutine = _instances.Coroutine
Iterator = _instances.Iterator
BaseGenerator = _instances.BaseGenerator
AsyncGenerator = _instances.AsyncGenerator
Generator = _instances.Generator
Tuple = _instances.Tuple
List = _instances.List
Dict = _instances.Dict
AnnotationsDict = _instances.AnnotationsDict
Splat = _instances.Splat
SequenceLength = _instances.SequenceLength

InterpreterFunction = _interpreter_function.InterpreterFunction

PyTDFunction = _pytd_function.PyTDFunction
PyTDSignature = _pytd_function.PyTDSignature
SignatureMutationError = _pytd_function.SignatureMutationError

Unknown = _singletons.Unknown
Singleton = _singletons.Singleton
Empty = _singletons.Empty
Deleted = _singletons.Deleted
Unsolvable = _singletons.Unsolvable
Null = _singletons.Null

AnnotationClass = _typing.AnnotationClass
AnnotationContainer = _typing.AnnotationContainer
ParamSpec = _typing.ParamSpec
ParamSpecArgs = _typing.ParamSpecArgs
ParamSpecKwargs = _typing.ParamSpecKwargs
ParamSpecInstance = _typing.ParamSpecInstance
Concatenate = _typing.Concatenate
TypeParameter = _typing.TypeParameter
TypeParameterInstance = _typing.TypeParameterInstance
Union = _typing.Union
LateAnnotation = _typing.LateAnnotation
FinalAnnotation = _typing.FinalAnnotation

AMBIGUOUS = (Unknown, Unsolvable)
AMBIGUOUS_OR_EMPTY = AMBIGUOUS + (Empty,)
FUNCTION_TYPES = (BoundFunction, Function)
INTERPRETER_FUNCTION_TYPES = (BoundInterpreterFunction, InterpreterFunction)
PYTD_FUNCTION_TYPES = (BoundPyTDFunction, PyTDFunction)
TYPE_VARIABLE_TYPES = (TypeParameter, ParamSpec)
TYPE_VARIABLE_INSTANCES = (TypeParameterInstance, ParamSpecInstance)

AmbiguousOrEmptyType = Unknown | Unsolvable | Empty
