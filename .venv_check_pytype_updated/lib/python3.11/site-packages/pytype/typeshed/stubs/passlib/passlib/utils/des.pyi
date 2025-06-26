from passlib.crypto.des import (
    des_encrypt_block as des_encrypt_block,
    des_encrypt_int_block as des_encrypt_int_block,
    expand_des_key as expand_des_key,
)
from passlib.utils.decor import deprecated_function as deprecated_function

def mdes_encrypt_int_block(key, input, salt: int = 0, rounds: int = 1): ...
