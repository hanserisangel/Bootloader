#!/usr/bin/env python3
import argparse
import os
import struct
from pathlib import Path

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


def pack_ota(fw_path: Path, sign_priv_path: Path, dev_pub_path: Path, out_path: Path, aes_len: int) -> None:
    fw_bytes = fw_path.read_bytes()

    sign_priv = load_ec_private_key(sign_priv_path)
    dev_pub = load_ec_public_key(dev_pub_path)

    eph_priv = ec.generate_private_key(ec.SECP256R1())
    eph_pub = eph_priv.public_key().public_bytes(
        serialization.Encoding.X962,
        serialization.PublicFormat.UncompressedPoint,
    )

    if len(eph_pub) != OTA_ECDH_PUB_LEN:
        raise SystemExit("Unexpected ECDH public key length")

    shared = eph_priv.exchange(ec.ECDH(), dev_pub)
    salt = os.urandom(OTA_SALT_LEN)
    iv = os.urandom(OTA_IV_LEN)

    hkdf = HKDF(
        algorithm=hashes.SHA256(),
        length=aes_len,
        salt=salt,
        info=b"OTA-AES",
    )
    key = hkdf.derive(shared)

    encryptor = Cipher(algorithms.AES(key), modes.CTR(iv)).encryptor()
    ciphertext = encryptor.update(fw_bytes) + encryptor.finalize()

    meta = eph_pub + salt + iv
    fw_size = len(fw_bytes)

    sig_len = 0
    signature = b""
    for _ in range(3):
        header = struct.pack("<IIII", OTA_MAGIC, OTA_HDR_SIZE, fw_size, sig_len)
        to_sign = header + meta + ciphertext
        signature = sign_priv.sign(to_sign, ec.ECDSA(hashes.SHA256()))
        new_sig_len = len(signature)
        if new_sig_len == sig_len:
            break
        sig_len = new_sig_len

    header = struct.pack("<IIII", OTA_MAGIC, OTA_HDR_SIZE, fw_size, len(signature))
    to_sign = header + meta + ciphertext
    signature = sign_priv.sign(to_sign, ec.ECDSA(hashes.SHA256()))

    out_path.write_bytes(header + meta + ciphertext + signature)


def main() -> None:
    parser = argparse.ArgumentParser(description="Pack OTA with ECDH+HKDF+AES and ECDSA signature")
    parser.add_argument("--fw", required=True, help="Firmware bin path (plaintext)")
    parser.add_argument("--sign-priv", required=True, help="ECDSA signing private key (PEM)")
    parser.add_argument("--dev-pub", required=True, help="Device ECDH public key (PEM)")
    parser.add_argument("--out", required=True, help="Output OTA package path")
    parser.add_argument("--aes-len", type=int, default=16, choices=[16, 32], help="AES key length (16 or 32)")
    args = parser.parse_args()

    pack_ota(Path(args.fw), Path(args.sign_priv), Path(args.dev_pub), Path(args.out), args.aes_len)


if __name__ == "__main__":
    main()
