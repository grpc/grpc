from _typeshed import Incomplete
from collections.abc import Callable, Iterable, Sequence
from typing import Any, Generic, TypeVar, overload
from typing_extensions import Self, TypeAlias

import tensorflow as tf
from tensorflow import Tensor, Variable, VariableAggregation, VariableSynchronization
from tensorflow._aliases import AnyArray, DTypeLike, TensorCompatible
from tensorflow.keras.activations import _Activation
from tensorflow.keras.constraints import Constraint
from tensorflow.keras.initializers import _Initializer
from tensorflow.keras.regularizers import _Regularizer

_InputT = TypeVar("_InputT", contravariant=True)
_OutputT = TypeVar("_OutputT", covariant=True)

class InputSpec:
    dtype: str | None
    shape: tuple[int | None, ...]
    ndim: int | None
    max_ndim: int | None
    min_ndim: int | None
    axes: dict[int, int | None] | None
    def __init__(
        self,
        dtype: DTypeLike | None = None,
        shape: Iterable[int | None] | None = None,
        ndim: int | None = None,
        max_ndim: int | None = None,
        min_ndim: int | None = None,
        axes: dict[int, int | None] | None = None,
        allow_last_axis_squeeze: bool = False,
        name: str | None = None,
    ) -> None: ...
    def get_config(self) -> dict[str, Any]: ...
    @classmethod
    def from_config(cls, config: dict[str, Any]) -> type[Self]: ...

# Most layers have input and output type of just Tensor and when we support default type variables,
# maybe worth trying.
class Layer(tf.Module, Generic[_InputT, _OutputT]):
    # The most general type is ContainerGeneric[InputSpec] as it really
    # depends on _InputT. For most Layers it is just InputSpec
    # though. Maybe describable with HKT?
    input_spec: InputSpec | Any

    @property
    def trainable(self) -> bool: ...
    @trainable.setter
    def trainable(self, value: bool) -> None: ...
    def __init__(
        self, trainable: bool = True, name: str | None = None, dtype: DTypeLike | None = None, dynamic: bool = False
    ) -> None: ...

    # *args/**kwargs are allowed, but have obscure footguns and tensorflow documentation discourages their usage.
    # First argument will automatically be cast to layer's compute dtype, but any other tensor arguments will not be.
    # Also various tensorflow tools/apis can misbehave if they encounter a layer with *args/**kwargs.
    def __call__(self, inputs: _InputT, *, training: bool = False, mask: TensorCompatible | None = None) -> _OutputT: ...
    def call(self, inputs: _InputT, /) -> _OutputT: ...

    # input_shape's real type depends on _InputT, but we can't express that without HKT.
    # For example _InputT tf.Tensor -> tf.TensorShape, _InputT dict[str, tf.Tensor] -> dict[str, tf.TensorShape].
    def build(self, input_shape: Any, /) -> None: ...
    @overload
    def compute_output_shape(self: Layer[tf.Tensor, tf.Tensor], input_shape: tf.TensorShape, /) -> tf.TensorShape: ...
    @overload
    def compute_output_shape(self, input_shape: Any, /) -> Any: ...
    def add_weight(
        self,
        name: str | None = None,
        shape: Iterable[int | None] | None = None,
        dtype: DTypeLike | None = None,
        initializer: _Initializer | None = None,
        regularizer: _Regularizer = None,
        trainable: bool | None = None,
        constraint: _Constraint = None,
        use_resource: bool | None = None,
        synchronization: VariableSynchronization = ...,
        aggregation: VariableAggregation = ...,
    ) -> tf.Variable: ...
    def add_loss(self, losses: tf.Tensor | Sequence[tf.Tensor] | Callable[[], tf.Tensor]) -> None: ...
    def count_params(self) -> int: ...
    @property
    def trainable_variables(self) -> list[Variable]: ...
    @property
    def non_trainable_variables(self) -> list[Variable]: ...
    @property
    def trainable_weights(self) -> list[Variable]: ...
    @property
    def non_trainable_weights(self) -> list[Variable]: ...
    @property
    def losses(self) -> list[Tensor]: ...
    def get_weights(self) -> list[AnyArray]: ...
    def set_weights(self, weights: Sequence[AnyArray]) -> None: ...
    def get_config(self) -> dict[str, Any]: ...
    @classmethod
    def from_config(cls, config: dict[str, Any]) -> Self: ...
    def __getattr__(self, name: str) -> Incomplete: ...

# Every layer has trainable, dtype, name, and dynamic. At runtime these
# are mainly handled with **kwargs, passed up and then validated.
# In actual implementation there's 12 allowed keyword arguments, but only
# 4 are documented and other 8 are mainly internal. The other 8 can be found
# https://github.com/keras-team/keras/blob/e6784e4302c7b8cd116b74a784f4b78d60e83c26/keras/engine/base_layer.py#L329
# PEP 692 support would be very helpful here and allow removing stubtest allowlist for
# all layer constructors.

# TODO: Replace last Any after adding tf.keras.mixed_precision.Policy.
_LayerDtype: TypeAlias = DTypeLike | dict[str, Any] | Any

_Constraint: TypeAlias = str | dict[str, Any] | Constraint | None

# Layer's compute_output_shape commonly have instance as first argument name instead of self.
# This is an artifact of actual implementation commonly uses a decorator to define it.
# Layer.build has same weirdness sometimes. For both marked as positional only.
class Dense(Layer[tf.Tensor, tf.Tensor]):
    def __init__(
        self,
        units: int,
        activation: _Activation = None,
        use_bias: bool = True,
        kernel_initializer: _Initializer = "glorot_uniform",
        bias_initializer: _Initializer = "zeros",
        kernel_regularizer: _Regularizer = None,
        bias_regularizer: _Regularizer = None,
        activity_regularizer: _Regularizer = None,
        kernel_constraint: _Constraint = None,
        bias_constraint: _Constraint = None,
        trainable: bool = True,
        dtype: _LayerDtype = None,
        dynamic: bool = False,
        name: str | None = None,
    ) -> None: ...

class BatchNormalization(Layer[tf.Tensor, tf.Tensor]):
    def __init__(
        self,
        axis: int = -1,
        momentum: float = 0.99,
        epsilon: float = 0.001,
        center: bool = True,
        scale: bool = True,
        beta_initializer: _Initializer = "zeros",
        gamma_initializer: _Initializer = "ones",
        moving_mean_initializer: _Initializer = "zeros",
        moving_variance_initializer: _Initializer = "ones",
        beta_regularizer: _Regularizer = None,
        gamma_regularizer: _Regularizer = None,
        beta_constraint: _Constraint = None,
        gamma_constraint: _Constraint = None,
        trainable: bool = True,
        dtype: _LayerDtype = None,
        dynamic: bool = False,
        name: str | None = None,
    ) -> None: ...

class ReLU(Layer[tf.Tensor, tf.Tensor]):
    def __init__(
        self,
        max_value: float | None = None,
        negative_slope: float | None = 0.0,
        threshold: float | None = 0.0,
        trainable: bool = True,
        dtype: _LayerDtype = None,
        dynamic: bool = False,
        name: str | None = None,
    ) -> None: ...

class Dropout(Layer[tf.Tensor, tf.Tensor]):
    def __init__(
        self,
        rate: float,
        noise_shape: TensorCompatible | Sequence[int | None] | None = None,
        seed: int | None = None,
        trainable: bool = True,
        dtype: _LayerDtype = None,
        dynamic: bool = False,
        name: str | None = None,
    ) -> None: ...

class Embedding(Layer[tf.Tensor, tf.Tensor]):
    def __init__(
        self,
        input_dim: int,
        output_dim: int,
        embeddings_initializer: _Initializer = "uniform",
        embeddings_regularizer: _Regularizer = None,
        embeddings_constraint: _Constraint = None,
        mask_zero: bool = False,
        input_length: int | None = None,
        trainable: bool = True,
        dtype: _LayerDtype = None,
        dynamic: bool = False,
        name: str | None = None,
    ) -> None: ...

def __getattr__(name: str) -> Incomplete: ...
