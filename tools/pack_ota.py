import argparse
import struct
from pathlib import Path

OTA_MAGIC = 0x4F544148  # 'OTAH' little-endian 魔数
OTA_HDR_SIZE = 16       # 固定的 OTA 包头大小 (16 字节)

def pack_ota(fw_path: Path, sig_path: Path, out_path: Path) -> None:
    fw_bytes = fw_path.read_bytes()
    sig_bytes = sig_path.read_bytes()

    header = struct.pack(
        "<IIII",        # 小端字节序，4 个无符号整数
        OTA_MAGIC,      # 魔数
        OTA_HDR_SIZE,   # 包头大小
        len(fw_bytes),  # 固件数据长度
        len(sig_bytes), # 签名数据长度
    )

    out_path.write_bytes(header + fw_bytes + sig_bytes)

def main() -> None:
    parser = argparse.ArgumentParser(description="Pack OTA: header + firmware + signature")
    parser.add_argument("--fw", required=True, help="Firmware bin path")
    parser.add_argument("--sig", required=True, help="Signature bin path")
    parser.add_argument("--out", required=True, help="Output OTA package path")
    args = parser.parse_args()

    fw_path = Path(args.fw)
    sig_path = Path(args.sig)
    out_path = Path(args.out)

    if not fw_path.is_file():
        raise SystemExit(f"Firmware not found: {fw_path}")
    if not sig_path.is_file():
        raise SystemExit(f"Signature not found: {sig_path}")

    pack_ota(fw_path, sig_path, out_path)


if __name__ == "__main__":
    main()
