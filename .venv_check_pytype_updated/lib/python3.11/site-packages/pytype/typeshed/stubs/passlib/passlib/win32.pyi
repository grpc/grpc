from typing import Any

from passlib.hash import nthash as nthash

raw_nthash: Any

def raw_lmhash(secret, encoding: str = "ascii", hex: bool = False): ...
