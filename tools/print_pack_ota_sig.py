import struct

'''
print_pack_ota_sig.py: Extract and verify ECDSA signature from OTA package
- 从 OTA 包中提取包头、元数据、密文和签名
- 输出签名的前后 8 字节和签名长度，供调试使用

- 注：依赖 struct 模块进行二进制数据解析
'''
data = open("OTA_A_merge2.bin","rb").read()
magic,hdr,fw,siglen = struct.unpack("<IIII", data[:16])
sig = data[-siglen:]
print(sig[:8].hex(), sig[-8:].hex(), siglen)