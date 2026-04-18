from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.backends import default_backend

'''
print_ecdh_prikey.py: 输出 ECDH 私钥的原始字节内容
'''

with open("dev_ecdh_priv.pem","rb") as f:
    key = serialization.load_pem_private_key(f.read(), password=None, backend=default_backend())
priv_int = key.private_numbers().private_value
priv_bytes = priv_int.to_bytes(32, "big")
print(priv_bytes.hex())