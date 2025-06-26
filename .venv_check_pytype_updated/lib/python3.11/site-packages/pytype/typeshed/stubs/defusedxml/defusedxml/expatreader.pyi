from _typeshed import Incomplete

# Cannot type most things here as DefusedExpatParser is based off of
# xml.sax.expatreader, which is an undocumented module and lacks types at the moment.

__origin__: str

DefusedExpatParser = Incomplete

def create_parser(*args, **kwargs): ...
