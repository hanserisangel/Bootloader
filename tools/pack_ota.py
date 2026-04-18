#!/usr/bin/env python3
import argparse
import hashlib
import os
import struct
from pathlib import Path

'''
pack_ota.py: Pack OTA with ECDH+HKDF+AES and ECDSA signature
- 生成一个格式为 [OTA 包头][元数据][密文][签名] 的完整 OTA 包
- 同时还可以验证 ECDH 公钥和设备私钥是否匹配，确保生成的包能被设备正确解密

- OTA 包头: 16 字节, 包含魔数(4 字节)、包头长度(4 字节)、固件长度(4 字节)、签名长度(4 字节)
- 元数据: 97 字节，包含 ECDH 公钥(65 字节)、HKDF 盐值(16 字节)、AES IV(16 字节)
- 密文: 同明文固件字节，对固件进行 AES-CTR 加密后的数据
- 签名: 70-72 字节，对包头+元数据+密文进行 ECDSA-SHA256 签名后的 DER 编码

- 注：由于签名长度可能会因为 DER 编码的变化而变化，因此需要循环计算直到签名长度稳定
- 注：依赖 cryptography 库进行 ECC、AES 和 HKDF 操作
'''

try:
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    from cryptography.hazmat.primitives.kdf.hkdf import HKDF
except Exception as exc:
    raise SystemExit("Missing dependency: cryptography (pip install cryptography)") from exc

OTA_MAGIC = 0x4F544148
OTA_HDR_SIZE = 16
OTA_ECDH_PUB_LEN = 65
OTA_SALT_LEN = 16
OTA_IV_LEN = 16


def load_ec_public_key(path: Path):
    data = path.read_bytes()
    return serialization.load_pem_public_key(data)


def load_ec_private_key(path: Path):
    data = path.read_bytes()
    return serialization.load_pem_private_key(data, password=None)


def pack_ota(
    fw_path: Path,
    sign_priv_path: Path,
    dev_pub_path: Path,
    out_path: Path,
    aes_len: int,
    dev_priv_check_path: Path | None = None,
) -> None:
    fw_bytes = fw_path.read_bytes()

    sign_priv = load_ec_private_key(sign_priv_path)
    dev_pub = load_ec_public_key(dev_pub_path)

    if dev_priv_check_path is not None:
        dev_priv = load_ec_private_key(dev_priv_check_path)
        pub_from_priv = dev_priv.public_key().public_bytes(
            serialization.Encoding.X962,
            serialization.PublicFormat.UncompressedPoint,
        )
        pub_from_file = dev_pub.public_bytes(
            serialization.Encoding.X962,
            serialization.PublicFormat.UncompressedPoint,
        )
        if pub_from_priv != pub_from_file:
            raise SystemExit("dev-pub does not match dev-priv-check")

    eph_priv = ec.generate_private_key(ec.SECP256R1())
    eph_pub = eph_priv.public_key().public_bytes(
        serialization.Encoding.X962,
        serialization.PublicFormat.UncompressedPoint,
    )

    if len(eph_pub) != OTA_ECDH_PUB_LEN:
        raise SystemExit("Unexpected ECDH public key length")

    shared = eph_priv.exchange(ec.ECDH(), dev_pub)
    shared32 = shared.rjust(32, b"\x00")
    salt = os.urandom(OTA_SALT_LEN)
    iv = os.urandom(OTA_IV_LEN)

    hkdf = HKDF(
        algorithm=hashes.SHA256(),
        length=aes_len,
        salt=salt,
        info=b"OTA-AES",
    )
    key = hkdf.derive(shared32)

    encryptor = Cipher(algorithms.AES(key), modes.CTR(iv)).encryptor()
    ciphertext = encryptor.update(fw_bytes) + encryptor.finalize()

    meta = eph_pub + salt + iv 
    fw_size = len(fw_bytes)

    sig_len = 0
    signature = b""
    # 签名长度可能会因为 DER 编码的变化而变化，因此需要循环直到长度稳定
    for _ in range(8):
        header = struct.pack("<IIII", OTA_MAGIC, OTA_HDR_SIZE, fw_size, sig_len)
        to_sign = header + meta + ciphertext
        signature = sign_priv.sign(to_sign, ec.ECDSA(hashes.SHA256()))
        new_sig_len = len(signature)
        if new_sig_len == sig_len:
            break
        sig_len = new_sig_len
    else:
        raise SystemExit("Signature length did not stabilize")

    # 用于调试：输出包头+元数据+密文的 SHA-256 摘要
    # to_sign = header + meta + ciphertext
    # digest = hashlib.sha256(to_sign).hexdigest()
    # print(f"sha256(header+meta+ciphertext)={digest}")

    # out_path.write_bytes(to_sign + signature)


def main() -> None:
    parser = argparse.ArgumentParser(description="Pack OTA with ECDH+HKDF+AES and ECDSA signature")
    parser.add_argument("--fw", required=True, help="Firmware bin path (plaintext)")
    parser.add_argument("--sign-priv", required=True, help="ECDSA signing private key (PEM)")
    parser.add_argument("--dev-pub", required=True, help="Device ECDH public key (PEM)")
    parser.add_argument("--dev-priv-check", help="Optional: device ECDH private key PEM for keypair consistency check")
    parser.add_argument("--out", required=True, help="Output OTA package path")
    parser.add_argument("--aes-len", type=int, default=16, choices=[16, 32], help="AES key length (16 or 32)")
    args = parser.parse_args()

    dev_priv_check = Path(args.dev_priv_check) if args.dev_priv_check else None
    pack_ota(
        Path(args.fw),
        Path(args.sign_priv),
        Path(args.dev_pub),
        Path(args.out),
        args.aes_len,
        dev_priv_check,
    )


if __name__ == "__main__":
    main()
