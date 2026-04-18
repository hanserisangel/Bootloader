from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.backends import default_backend

'''
print_ecdsa_pubkey.py: 从 DER 编码的 ECDSA 公钥文件中提取公钥，并以十六进制格式打印
'''

with open("ec_pub.der","rb") as f:
    file_content = f.read()
print(file_content.hex())


