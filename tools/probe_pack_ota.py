import argparse
import hashlib
import struct
from pathlib import Path

'''
probe_pack_ota.py: Probe first 16 bytes for OTA ECDH/HKDF/AES-CTR alignment
- 解析 OTA 包头和元数据，使用设备私钥派生 AES 密钥和 IV, 解密密文的前 16 字节(主要用于调试)
- 验证解密后的前 16 字节和原始固件的前 16 字节是否匹配
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
OTA_META_LEN = OTA_ECDH_PUB_LEN + OTA_SALT_LEN + OTA_IV_LEN


def hx16(tag: str, data: bytes) -> None:
    cut = data[:16]
    print(f"{tag}: {' '.join(f'{b:02X}' for b in cut)}")


def parse_pkg(pkg: bytes):
    if len(pkg) < OTA_HDR_SIZE + OTA_META_LEN:
        raise SystemExit("Package too small")

    magic, header_size, fw_size, sig_len = struct.unpack("<IIII", pkg[:OTA_HDR_SIZE])
    if magic != OTA_MAGIC:
        raise SystemExit(f"Bad magic: 0x{magic:08X}")
    if header_size != OTA_HDR_SIZE:
        raise SystemExit(f"Bad header size: {header_size}")

    meta = pkg[OTA_HDR_SIZE: OTA_HDR_SIZE + OTA_META_LEN]
    cipher_start = OTA_HDR_SIZE + OTA_META_LEN
    cipher_end = cipher_start + fw_size
    sig_end = cipher_end + sig_len

    if sig_end > len(pkg):
        raise SystemExit("Package truncated")

    ciphertext = pkg[cipher_start:cipher_end]
    signature = pkg[cipher_end:sig_end]
    return meta, ciphertext, signature


def derive_key(meta: bytes, dev_priv_path: Path, aes_len: int) -> tuple[bytes, bytes]:
    eph_pub = meta[:OTA_ECDH_PUB_LEN]
    salt = meta[OTA_ECDH_PUB_LEN: OTA_ECDH_PUB_LEN + OTA_SALT_LEN]
    iv = meta[OTA_ECDH_PUB_LEN + OTA_SALT_LEN: OTA_META_LEN]

    if len(eph_pub) != OTA_ECDH_PUB_LEN or eph_pub[0] != 0x04:
        raise SystemExit("Invalid eph_pub")

    dev_priv = serialization.load_pem_private_key(dev_priv_path.read_bytes(), password=None)
    eph_pub_key = ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), eph_pub)

    shared = dev_priv.exchange(ec.ECDH(), eph_pub_key)
    shared32 = shared.rjust(32, b"\x00")

    hkdf = HKDF(
        algorithm=hashes.SHA256(),
        length=aes_len,
        salt=salt,
        info=b"OTA-AES",
    )
    key = hkdf.derive(shared32)
    return key, iv


def main() -> None:
    parser = argparse.ArgumentParser(description="Probe first 16 bytes for OTA ECDH/HKDF/AES-CTR alignment")
    parser.add_argument("--pkg", required=True, help="OTA package path (header+meta+ciphertext+signature)")
    parser.add_argument("--dev-priv", required=True, help="Device ECDH private key PEM path")
    parser.add_argument("--fw", help="Original plaintext firmware bin path (optional, for full compare)")
    parser.add_argument("--aes-len", type=int, default=16, choices=[16, 32], help="AES key length")
    args = parser.parse_args()

    pkg = Path(args.pkg).read_bytes()
    meta, ciphertext, _ = parse_pkg(pkg)
    key, iv = derive_key(meta, Path(args.dev_priv), args.aes_len)

    decryptor = Cipher(algorithms.AES(key), modes.CTR(iv)).decryptor()
    plaintext_all = decryptor.update(ciphertext) + decryptor.finalize()
    plaintext = plaintext_all[:16]

    # 用于调试：输出密文、密钥、IV 和解密后的明文的前 16 字节
    # hx16("DBG cipher[0:16]", ciphertext)
    # hx16("DBG key[0:16]", key)
    # hx16("DBG iv[0:16]", iv)
    # hx16("DBG plain[0:16]", plaintext)

    # 用于调试：输出密文和解密后的明文的 SHA-256 摘要
    # print(f"sha256(pkg-ciphertext)={hashlib.sha256(ciphertext).hexdigest()}")
    # print(f"sha256(decrypted)={hashlib.sha256(plaintext_all).hexdigest()}")

    # 验证解密后的密文和原始固件是否匹配（如果提供了原始固件）
    if args.fw:
        fw = Path(args.fw).read_bytes()
        print(f"sha256(fw)={hashlib.sha256(fw).hexdigest()}")
        print(f"len(decrypted)={len(plaintext_all)}, len(fw)={len(fw)}")
        hx16("FW  [0:16]", fw)
        if len(fw) == len(plaintext_all) and fw == plaintext_all:
            print("COMPARE: MATCH")
        else:
            print("COMPARE: MISMATCH")


if __name__ == "__main__":
    main()
