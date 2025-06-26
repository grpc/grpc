from _typeshed import Incomplete

from tensorflow.keras import (
    activations as activations,
    constraints as constraints,
    initializers as initializers,
    layers as layers,
    losses as losses,
    metrics as metrics,
    models as models,
    optimizers as optimizers,
    regularizers as regularizers,
)
from tensorflow.keras.models import Model as Model

def __getattr__(name: str) -> Incomplete: ...
