from passlib.handlers.argon2 import argon2 as argon2
from passlib.handlers.bcrypt import bcrypt as bcrypt, bcrypt_sha256 as bcrypt_sha256
from passlib.handlers.cisco import cisco_asa as cisco_asa, cisco_pix as cisco_pix, cisco_type7 as cisco_type7
from passlib.handlers.des_crypt import bigcrypt as bigcrypt, bsdi_crypt as bsdi_crypt, crypt16 as crypt16, des_crypt as des_crypt
from passlib.handlers.digests import (
    hex_md4 as hex_md4,
    hex_md5 as hex_md5,
    hex_sha1 as hex_sha1,
    hex_sha256 as hex_sha256,
    hex_sha512 as hex_sha512,
    htdigest as htdigest,
)
from passlib.handlers.django import (
    django_bcrypt as django_bcrypt,
    django_bcrypt_sha256 as django_bcrypt_sha256,
    django_des_crypt as django_des_crypt,
    django_disabled as django_disabled,
    django_pbkdf2_sha1 as django_pbkdf2_sha1,
    django_pbkdf2_sha256 as django_pbkdf2_sha256,
    django_salted_md5 as django_salted_md5,
    django_salted_sha1 as django_salted_sha1,
)
from passlib.handlers.fshp import fshp as fshp
from passlib.handlers.ldap_digests import (
    ldap_bcrypt as ldap_bcrypt,
    ldap_bsdi_crypt as ldap_bsdi_crypt,
    ldap_des_crypt as ldap_des_crypt,
    ldap_md5 as ldap_md5,
    ldap_md5_crypt as ldap_md5_crypt,
    ldap_plaintext as ldap_plaintext,
    ldap_salted_md5 as ldap_salted_md5,
    ldap_salted_sha1 as ldap_salted_sha1,
    ldap_salted_sha256 as ldap_salted_sha256,
    ldap_salted_sha512 as ldap_salted_sha512,
    ldap_sha1 as ldap_sha1,
    ldap_sha1_crypt as ldap_sha1_crypt,
    ldap_sha256_crypt as ldap_sha256_crypt,
    ldap_sha512_crypt as ldap_sha512_crypt,
)
from passlib.handlers.md5_crypt import apr_md5_crypt as apr_md5_crypt, md5_crypt as md5_crypt
from passlib.handlers.misc import plaintext as plaintext, unix_disabled as unix_disabled, unix_fallback as unix_fallback
from passlib.handlers.mssql import mssql2000 as mssql2000, mssql2005 as mssql2005
from passlib.handlers.mysql import mysql41 as mysql41, mysql323 as mysql323
from passlib.handlers.oracle import oracle10 as oracle10, oracle11 as oracle11
from passlib.handlers.pbkdf2 import (
    atlassian_pbkdf2_sha1 as atlassian_pbkdf2_sha1,
    cta_pbkdf2_sha1 as cta_pbkdf2_sha1,
    dlitz_pbkdf2_sha1 as dlitz_pbkdf2_sha1,
    grub_pbkdf2_sha512 as grub_pbkdf2_sha512,
    ldap_pbkdf2_sha1 as ldap_pbkdf2_sha1,
    ldap_pbkdf2_sha256 as ldap_pbkdf2_sha256,
    ldap_pbkdf2_sha512 as ldap_pbkdf2_sha512,
    pbkdf2_sha1 as pbkdf2_sha1,
    pbkdf2_sha256 as pbkdf2_sha256,
    pbkdf2_sha512 as pbkdf2_sha512,
)
from passlib.handlers.phpass import phpass as phpass
from passlib.handlers.postgres import postgres_md5 as postgres_md5
from passlib.handlers.roundup import (
    ldap_hex_md5 as ldap_hex_md5,
    ldap_hex_sha1 as ldap_hex_sha1,
    roundup_plaintext as roundup_plaintext,
)
from passlib.handlers.scram import scram as scram
from passlib.handlers.scrypt import scrypt as scrypt
from passlib.handlers.sha1_crypt import sha1_crypt as sha1_crypt
from passlib.handlers.sha2_crypt import sha256_crypt as sha256_crypt, sha512_crypt as sha512_crypt
from passlib.handlers.sun_md5_crypt import sun_md5_crypt as sun_md5_crypt
from passlib.handlers.windows import (
    bsd_nthash as bsd_nthash,
    lmhash as lmhash,
    msdcc as msdcc,
    msdcc2 as msdcc2,
    nthash as nthash,
)
