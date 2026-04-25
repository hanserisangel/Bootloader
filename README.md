# [STM32 OTA Update: BootLoader part](https://github.com/hanserisangel/Bootloader)

----

## 1. 项目简介
本项目是一个基于 **STM32F407ZGT6** 的安全启动加载器（Bootloader），实现了可裁剪的固件升级方案，支持本地升级、远程OTA升级、全量升级与增量升级。所有升级流程均内置安全校验与加密保护，确保固件传输与存储的完整性、真实性与机密性。

核心功能如下：
- ✅ 基础启动功能：程序跳转与中断向量表重映射
- ✅ 本地固件升级：基于 **Ymodem 协议**，采用 UART 空闲中断 + DMA + RingBuffer 实现高效接收
- ✅ 远程OTA升级：可配合 APP 用户程序，通过 **ESP32-C3** 模块以 HTTP 协议连接 OneNET 服务器接收固件
- ✅ A/B双分区架构：支持升级失败自动回滚，保证设备永不“变砖”
- ✅ 固件来源可信：基于 **ECDSA** 数字签名算法校验固件
- ✅ 完整性校验：使用 **SHA-256** 哈希算法验证固件完整性
- ✅ 传输加密：通过 **AES-128** 加密固件，并配合 **ECDH 密钥协商** 防止密钥泄露
- ✅ 差分升级：基于 **HPatchLite + tinyuz** 实现增量升级，大幅减少升级包体积与传输时间

> <td style="text-align:center;">.</td>
> 
> **说明：**
> 1. 本地升级与 OTA 升级的核心流程一致：固件均先下载至外部 Flash（W25Q64），再写入 MCU 内部Flash；区别仅在于固件接收方式（本地升级用 Ymodem/OTA 升级用网络）。
> 2. 全量升级与差分升级的处理流程基本一致：全量升级传输完整固件包，差分升级传输压缩后的差分包。
> 3. 项目所有功能模块均支持按需裁剪，第6节提供了详细的裁剪方法。
> <td style="text-align:center;">.</td>

### 1.1 具体工作流程
本项目的工作流程图如下所示：
<div style="text-align: center;">
    <img src="https://github.com/hanserisangel/Bootloader/blob/master/image/boot.drawio.svg" width="100%" height="100%" alt="工作流程">
</div>

本项目的升级流程分为**固件接收**、**安全校验与解密**、**差分还原与写入**三个阶段，全程实现流式处理，明文差分包不落地存储，兼顾安全与性能。

**流程说明：**
1.  **固件接收**：支持两种途径
    - 本地升级：通过电脑端 Ymodem 协议传输固件
    - 远程OTA升级：通过服务器下发固件包
        两种方式的固件均先下载至外部 Flash（W25Q64）缓存区。
2.  **安全校验与解密**：从 W25Q64 读取固件包，边读边执行以下操作：
       - **完整性校验**：计算固件包的SHA-256哈希值
       - **来源校验**：使用预存的 ECDSA 公钥验证数字签名，确保固件未被篡改、来源可信
       - **密钥协商**：通过 OTP 分区中存储的 ECDH 私钥，与固件包中的盐值、IV 派生 AES 密钥
       - **流式解密**：使用派生的 AES 密钥对固件包进行流式解密
3. **差分还原与写入**（差分升级特有流程）：
       - 对解密后的差分包进行 tinyuz 流式解压
       - 基于 HPatchLite 算法，从当前活跃区（APP_A区）读取旧固件，与解压后的差分包进行差分还原
       - 将还原后的新固件流式写入非活跃区（APP_B区），全程无明文差分包落地 Flash
4. **升级完成与自动回滚**：
       - 写入完成后，更新升级状态标记，重启设备
       - Bootloader 检测到状态标记，启动新固件分区
       - 若新固件启动失败（无确认标记），下次启动时自动回滚至旧版本（APP_A区）

### 1.2 固件包的格式

<div style="text-align: center;">
    <img src="https://github.com/hanserisangel/Bootloader/blob/master/image/OTAremote.drawio.svg" width="100%" height="100%" alt="工作流程">
</div>

说明：
1. **头部**：20 byte, 包含`魔数(4 字节)、包头长度(4 字节)、包类型(4 字节)、固件长度(4 字节)、签名长度(4 字节)`
   - 包头长度：20
   - 包类型：0是全量包，1是增量包
   - 固件长度：固件大小
   - 签名长度：固件包最后一个字段`签名`的长度
2. **ECDH公钥+盐值+iv**：65+16+16=97byte，用于派生AES密钥
3. **固件密文**：AES加密后的固件
4. **签名**：ECDSA 签名值


### 1.3. 固件包的打包方法
本部分全部使用 tools 文件夹下的文件，tools 文件夹的结构如下所示：
```
项目根目录/
└── tools/
    ├── probe_pack_ota.py(测试密文解密后和原固件是否匹配)
    ├── print_pack_ota_sig.py(输出签名，用于调试)
    ├── print_ecdsa_pubkey.py(输出 ecdsa 的公钥，得到十六进制公钥)
    ├── print_ecdh_prikey.py(输出 ecdh 的私钥，得到十六进制私钥)
    ├── pack_ota.py(将固件打包)
    ├── hpatchi.exe(差分还原)
    ├── hdiffi.exe(差分压缩)
    ├── ecdsa_key
    │   ├── ec_priv.pem(ecdsa 私钥，pem 格式)
    │   ├── ec_pub.der(ecdsa 公钥，der 格式)
    │   └── ec_pub.pem(ecdsa 公钥，pem 格式)
    ├── ecdh_key
    │   ├── dev_ecdh_priv.der(ecdh 私钥，der 格式)
    │   ├── dev_ecdh_priv.pem(ecdh 私钥，pem 格式)
    │   └── dev_ecdh_pub.pem(ecdh 公钥，pem 格式)
    └── app_firmware
        ├── delta.bin(差分固件)
        ├── delta_merge.bin(差分固件包)
        ├── OTA_A.bin(全量固件)
        ├── OTA_A_merge.bin(全量固件包)
        └── OTA_B.bin(与OTA_A.bin差分的固件)
```

---

固件包是在 pc 上利用 pack_ota.py 脚本打包的，你的 windows 电脑上需要安装过 python 才能运行，命令格式如下：

```shell
python pack_ota.py --fw [固件] --sign-priv [ecdsa 私钥(pem 格式)] --dev-pub [ecdh 公钥(pem 格式)] --out [输出固件包] --aes-len 16 --pkg-type [固件类型]
```
说明：符号 `[]` 表示需要根据你的实际目录路径进行替换
- 密钥生成方式：我是在 ubantu 上用 openssl 命令生成的，如果没有 openssl 包，请安装一个
    - ECDSA
        ```shell
        $ openssl ecparam -name prime256v1 -genkey -noout -out ec_priv.pem
        $ openssl ec -in ec_priv.pem -pubout -out ec_pub.pem
        $ openssl ec -in ec_priv.pem -pubout -outform DER -out ec_pub.der
        ```
    - ECDH
        ```shell
        $ openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out dev_ecdh_priv.pem
        $ openssl pkey -in dev_ecdh_priv.pem -pubout -out dev_ecdh_pub.pem
        $ openssl pkey -in dev_ecdh_priv.pem -outform DER -out dev_ecdh_priv.der
        ```
- 说明：密钥的格式有两种，一个是 pem 格式，一个是 der 格式，两种格式都一样，只是 pem 格式是文本形式，der 格式是二进制形式。上面的 python 命令用 pem 格式的密钥，程序里面用的是 der 格式的密钥，可以先用 print_ecdsa_pubkey.py 和 print_ecdh_prikey.py 脚本打印 der 格式的密钥，然后复制到程序中的密钥数组里。

ecdh 私钥：（本来应该写在OTP分区里的，但是这个分区只能写一次，为了避免造成不可逆的影响，这里把私钥写在程序里）
```c
static const uint8_t k_ota_ecdh_priv[] = {
    0x0e, 0xa7, 0xb4, 0xed, 0x04, 0x45, 0xe8, 0x1e, 0x3e, 0xf8, 0x79, 0x98, 0x1c, 0x3f, 0x18, 0xd3,
    0xe0, 0xf9, 0x7e, 0x39, 0x14, 0x15, 0x8a, 0xb1, 0xb7, 0xf5, 0xd2, 0xab, 0x55, 0x74, 0x28, 0xd8
};
```
ecdsa 公钥：这个公钥一定要写入 W25Q64
```c
static const uint8_t k_ota_ec_pub[] = {
    0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01, 0x06, 0x08,
    0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00, 0x04, 0x35, 0x69, 0xe7, 
    0xab, 0x4b, 0x12, 0x25, 0x12, 0x5d, 0xbe, 0x89, 0x12, 0x7f, 0xb6, 0x2c, 0xcd, 0x7d, 0x91, 
    0xb3, 0xf9, 0x1d, 0x4a, 0x52, 0x11, 0x42, 0x39, 0xad, 0xe8, 0xd9, 0xa6, 0x4a, 0xec, 0xa7, 
    0x45, 0x2a, 0xcf, 0x48, 0xc3, 0x09, 0x3b, 0x4e, 0x06, 0xa3, 0x7b, 0x16, 0x9d, 0xba, 0xe6, 
    0x04, 0x44, 0xf0, 0x93, 0xb6, 0xd3, 0x87, 0x7b, 0x73, 0xe0, 0xa6, 0xf5, 0x27, 0xc4, 0xc5, 0x62
};
```
### 1.4 生成差分压缩固件
使用tools里面的hdiffi.exe，命令的具体使用方法可以见 [here](https://github.com/sisong/HPatchLite)
```shell
.\hdiffi.exe -f -d -c-tinyuz [旧固件] [新固件] [输出的差分压缩固件]
```
可以现在 pc 上试一试差分还原能不能成功：
```shell
.\hpatchi.exe [旧固件] [差分压缩固件] [输出的新固件]
```

---

## 2. 环境准备

以下是本项目开发与验证所使用的环境，并非强制要求，你可以根据自身情况灵活选择。

#### 推荐开发环境
- **操作系统**：Windows 11
- **开发工具链**：
  - IDE：Keil MDK-ARM 5
  - 配置工具：STM32CubeMX
  - 辅助脚本：Python 3
  - 串口工具：SecureCRT
- **密钥生成工具**：Ubuntu 22.04 + OpenSSL
- **硬件平台**：STM32F407ZGT6 + W25Q64 SPI Flash
- **第三方依赖库**：
  - `mbedtls（可通过 STM32CubeMX 添加）`：用于实现 AES、ECDSA、ECDH、SHA256 安全算法
  - `HPatchLite + tinyuz`：用于实现差分升级与压缩算法（可从 [GitHub](https://github.com/sisong/HPatchLite) 获取）

#### 强制要求
以下两点是运行本项目的硬性前提：
1.  **MCU内部Flash空间**：必须大于 `Bootloader大小 + 2 × APP分区大小`（本项目当前Bootloader体积约66KB，裁剪功能可进一步减小体积；APP 分区大小可根据实际需求调整）。
2.  **外部Flash**：必须搭载SPI Flash（本项目使用W25Q64），用于缓存固件升级包和其他关键参数。

---

## 3. 理论介绍
### 3.1 程序跳转与中断向量表映射
Bootloader的核心功能之一，就是实现从 Bootloader 到 APP 程序的跳转，并正确处理中断向量表的映射，确保 APP 运行时中断功能正常。
**前置知识**
要理解跳转原理，需要先回顾STM32的上电启动流程：
1.  **上电启动流程**
    从上电到运行`main`函数，内核会依次执行以下步骤：
    - 设置`MSP`栈指针的初始值，并让`PC`程序计数器指向`Reset`复位中断函数。
    - 执行`Reset`复位中断函数，跳转到`SystemInit`初始化时钟树。
    - 返回后，跳转到编译器实现的`__main`函数，初始化内存段和C语言运行环境。
    - 最终跳转到用户的`main`函数执行。
2.  **地址重映射与中断向量表**
    - STM32上电时总是从`0x00`地址开始执行，因此Flash区域（从`0x0800_0000`开始）会被映射到`0x00`地址处。
    - `startup.s`启动文件中定义了**中断向量表**，它位于程序的最开始位置，其中第一个元素是`MSP`栈指针初始值，第二个元素是`Reset`复位函数地址。
    - 本项目的Bootloader本质上也是一个APP程序，因此它的bin文件也包含自己的中断向量表。同理，A/B分区中的固件也各自包含独立的中断向量表。

**程序跳转原理：模拟上电启动**
程序跳转的核心，就是在Bootloader中**模拟一次上电启动流程**：
1.  从目标APP的中断向量表中，读取第一个元素（`MSP`栈指针初始值），设置主栈指针。
2.  读取第二个元素（`Reset`复位函数地址），并跳转到该地址执行。
3.  跳转后，后续的时钟初始化、内存初始化、运行`main`函数等流程，都由APP的启动文件自动完成。

```c
/**
 * @brief  跳转到应用程序入口地址，开始执行新固件
 * @param  addr: 应用程序的入口地址，通常是分区起始地址
 * @retval None
 */
static void LOAD_A(uint32_t addr)
{
    // (uint32_t *)addr 表示 addr 是地址，*(uint32_t *)addr 表示读取该地址处的值
    // flash 最前面是中断向量表，第一条是初始栈指针，第二条是复位中断处理函数地址
    if((*(uint32_t *)addr >= SRAM1_BASE) && (*(uint32_t *)addr <= SRAM2_BASE))
    {
        MSP_setSP(*(uint32_t *)addr);       // 设置 MSP 为新固件的初始栈指针
        Load_A = (load_a)*(uint32_t *)(addr + 4);   // 获取新固件的复位处理函数地址，并强制转换为函数指针类型
        BootLoader_Clear();                 // 清空外设，避免新固件运行时受到干扰
        Load_A();                           // 跳转到新固件的复位处理函数，开始执行新固件
    } else LOG_E("Brance failed!");
}
```
**中断向量表映射**
由于A/B分区的APP固件不是从`0x0800_0000`地址开始存放的，其内部的中断向量表地址也发生了偏移。为了让内核在 APP 运行时能正确找到中断服务程序，必须修改中断向量表的映射地址：
1.  **核心机制**：Cortex-M 内核的`SCB`（系统控制块）中，有一个名为`VTOR`（向量表偏移寄存器）的寄存器，它决定了中断向量表的基地址。
2.  **实现方法**：在跳转前，将`VTOR`寄存器的值设置为目标 APP 的起始地址。这样，内核在响应中断时，就会从APP程序的起始位置去查找中断向量表，确保中断功能正常工作。

![alt text](局部截取_20260423_194800.png)
修改文件中的这个值，完成映射。这里我写的是 0x20000 是因为这是 A 分区的 APP 程序，根据第 5 节的分区表中 A 分区在 128KB 开始的区域；如果是 B 分区的 APP程序，这里要填 0x80000，因为 B 分区在 128+384KB 开始的区域

如果使用 keil5 直接下载 A/B 分区的应用程序，还需要修改下图的值
![alt text](UV4.exe_20260423_195152.png)
![alt text](UV4.exe_20260423_195234.png)
### 3.2 安全算法介绍与对比
本项目采用了一套工业级的安全算法组合，在保障固件传输与存储安全的同时，兼顾了嵌入式平台的性能与内存限制。以下是核心算法的功能介绍与选型对比：
<table style="width:100%">
    <tr>
        <td style="text-align:center;background:#F2F2F2;width:200px"><b>算法</b></td>
        <td style="text-align:center;background:#F2F2F2;width:400px"><b>核心功能</b></td>
        <td style="text-align:center;background:#F2F2F2;width:800px"><b>选型对比与优势</b></td>
    </tr>
    <tr>
        <td style="text-align:center"><b>SHA-256</b></td>
        <td style="text-align:center">固件完整性校验；为ECDSA签名提供摘要</td>
        <td>相比MD5：MD5已被证明存在碰撞漏洞，不具备安全校验能力。SHA-256是目前广泛使用的安全哈希算法，抗碰撞能力强，计算效率在STM32平台上表现良好。</td>
    </tr>
    <tr>
        <td style="text-align:center"><b>ECDSA(secp256r1)</b></td>
        <td style="text-align:center">固件数字签名, 防止篡改与伪造</td>
        <td>相比RSA：在同等安全强度下，ECDSA的密钥长度和签名长度远小于RSA，对MCU的计算和存储开销更低。</td>
    </tr>
    <tr>
        <td style="text-align:center"><b>AES-128</b></td>
        <td style="text-align:center">固件对称加密, 防止明文泄露</td>
        <td>作为NIST标准的对称加密算法，AES-128安全可靠，加密/解密速度快。</td>
    </tr>
    <tr>
        <td style="text-align:center"><b>ECDH(secp256r1)</b></td>
        <td style="text-align:center">安全密钥协商, 动态派生AES密钥</td>
        <td>相比固定密钥/UID派生密钥：通过ECDH可以在不传输密钥的情况下，安全协商出会话密钥，避免了密钥硬编码泄露的风险，每次升级密钥唯一，安全性更高。</td>
    </tr>
    <tr>
        <td style="text-align:center"><b>HPatchlite</b></td>
        <td style="text-align:center">差分算法, 识别新固件与旧固件的区别</td>
        <td>相比bsdiff、HDiffPatch：HPatchLite专为嵌入式设备优化，内存占用极低（仅需几KB RAM），支持流式处理。</td>
    </tr>
    <tr>
        <td style="text-align:center"><b>tinyuz</b></td>
        <td style="text-align:center">轻量压缩算法, 压缩固件包的体积</td>
        <td>相比lzma、zlib：tinyuz解压时内存占用极小（最小仅需1KB字典），解码速度快。</td>
    </tr>
</table>

### 3.3 Ymodem 协议
本项目的本地固件升级采用 **Ymodem 协议** 实现，相比传统的 Xmodem 协议，它在传输效率和可靠性上都有显著优势。
<table style="width:100%">
    <tr>
        <td style="text-align:center;background:#F2F2F2;width:200px"><b>协议</b></td>
        <td style="text-align:center;background:#F2F2F2;width:400px"><b>单次有效数据</b></td>
        <td style="text-align:center;background:#F2F2F2;width:800px"><b>特点与对比</b></td>
    </tr>
    <tr>
        <td style="text-align:center;width:200px"><b>Ymodem</b></td>
        <td style="text-align:center;width:400px">1024 字节</td>
        <td style="width:800px">每次传输1KB有效数据，减少了ACK/NAK确认次数，大幅提升传输效率；同时保留了CRC校验机制，保证数据可靠性。</td>
    </tr>
    <tr>
        <td style="text-align:center;width:200px"><b>Xmodem</b></td>
        <td style="text-align:center;width:400px">128/256 字节</td>
        <td style="width:800px">单次数据量小，确认次数多，传输效率低下，且不支持文件大小等附加信息的传输，已逐渐被Ymodem取代。</td>
    </tr>
</table>

- 经计算 Ymodem 的有效吞吐相比 Xmodem 提高 87.5%
- 下载总耗时减少 12%，而且固件越大，耗时减少越多

Ymodem 具体介绍可参考 https://www.cnblogs.com/zzssdd2/p/15418778.html
### 3.4 串口空闲中断 + DMA + ringbuffer
本项目的串口数据接收采用 **空闲中断 + DMA + RingBuffer** 的组合方案，兼顾了效率、CPU负载与灵活性

#### 方案选型说明
1.  **为什么不需要双缓冲接收？**
    Ymodem 协议是应答式传输，PC 端仅在收到 ACK/NAK 后才会发送下一个数据帧，因此不会出现发送方持续发送导致接收缓冲区溢出的情况，无需复杂的双缓冲机制。

2.  **为什么使用空闲中断？**
    串口除了接收 Ymodem 固定长度的数据帧外，还需要接收不定长的命令行数据。空闲中断可以在传输停止时自动触发，判断一帧数据接收完成，适配不定长数据的处理场景。

3.  **为什么使用DMA？**
    DMA 控制器可以在无需 CPU 干预的情况下，直接将串口数据搬运到内存中，大幅减轻CPU负担，实现高效、低延迟的数据接收。

4.  **为什么使用RingBuffer？**
    环形缓冲区作为数据接收的中间缓存，可以平滑处理“高速接收”与“低速处理”之间的速度差，提高内存利用率，避免数据丢失。

### 3.5 A/B 分区与自动回滚
除了bootloader部分，stm32的内部flash分成了A/B双分区，一旦新固件在设备上运行失败，则重启后回滚到另一个分区，防止设备变砖，自动回滚逻辑如下：
```c
uint8_t active_slot = Boot_GetActiveSlot();
uint8_t inactive_slot = Boot_GetInactiveSlot();

// A/B双分区自动回滚机制
if(OTA_Info.OTA_status == FAIL)
{
    LOG_W("Previous OTA update failed, rollback to previous version");
    active_slot = inactive_slot;
    OTA_Info.OTA_area = active_slot; // 更新 OTA_Info 中的 active slot 信息
    OTA_Info.OTA_status = SUCCESS; // 将状态重置为 SUCCESS，避免重复回滚
}
else if(OTA_Info.OTA_status == UPDATE)
{
    LOG_W("Previous OTA update not verified, skipping to new version");
    OTA_Info.OTA_status = FAIL;
    // 将状态设置为 FAIL，等待本次版本验证结果，如果验证成功会在后续 APP 程序里更新为 SUCCESS，如果验证失败则保持 FAIL，等待下次重启回滚
}

// 跳转到激活槽的应用程序
LOG_I("Boot active slot %c", (active_slot == MCU_FLASH_APP_A_SLOT) ? 'A' : 'B');
LOAD_A(Boot_GetSlotStartAddr(active_slot));
```
APP 固件程序需要在程序一开始向 W25Q64 写入`OTA_Info.OTA_area = NORMAL`，下次重启时才不会进行回滚

---

## 4. demo 演示
### 4.1 串口交互命令行
交互命令行的设计主要是为了方便调试，实际应用中可以删除这个功能，当然保留也不构成问题。上电启动后在 2s 内输入w，否则就会跳过命令行状态，跳转到应用程序。
<div style="text-align: center;">
    <img src="https://github.com/hanserisangel/Bootloader/blob/master/image/%E4%B8%B2%E5%8F%A3%E8%8F%9C%E5%8D%95.png" width="100%" height="100%" alt="工作流程">
</div>

可以看到串口显示菜单
### 4.2 擦除非活动分区
擦除mcu内部的目前非活动分区
<div style="text-align: center;">
    <img src="https://github.com/hanserisangel/Bootloader/blob/master/image/%5B1%5D.png" width="30%" height="30%" alt="工作流程">
</div>
显示擦除成功
### 4.3 把固件下载到 mcu 非活动分区
如果是全量升级，那么这个命令行的功能，等同于`4.6 + 4.7`这两个命令行合在一起 
如果是增量升级，那么这个命令行的功能，等同于`4.6 + 4.8`这两个命令行合在一起
<div style="text-align: center;">
    <img src="https://github.com/hanserisangel/Bootloader/blob/master/image/%5B2%5D.png" width="100%" height="100%" alt="工作流程">
</div>
下载成功，并自动切换了活动分区
### 4.4 设置版本号
在 SecureCRT 右键粘贴版本号，我设置的版本号格式是`version-1.0`，版本号格式可以自己修改，修改时需要改变版本号的最大长度，在`main.h`的`OTA_VERSION_MAX_LEN`宏定义
<div style="text-align: center;">
    <img src="https://github.com/hanserisangel/Bootloader/blob/master/image/%5B3%5D.png" width="50%" height="50%" alt="工作流程">
</div>
显示设置成功
### 4.5 查询版本号
查看版本号，版本号被改变有两种方式：
- 通过`4.4`命令改变
- 通过 APP 远程 OTA 升级的时候会改变，因为 ONENET 服务器在发送固件之前会发送固件的版本号，APP 下载完后会将版本号写入
<div style="text-align: center;">
    <img src="https://github.com/hanserisangel/Bootloader/blob/master/image/%5B4%5D.png" width="50%" height="50%" alt="工作流程">
</div>
目前的版本号是`version-1.0`
### 4.6 把固件下载到 W25Q64 中
下载的位置可查看第5.1节的分区表，也可以直接查看`main.h`头文件
<div style="text-align: center;">
    <img src="https://github.com/hanserisangel/Bootloader/blob/master/image/%5B5%5D.png" width="100%" height="100%" alt="工作流程">
</div>
显示下载成功
### 4.7 把全量固件从 W25Q64 下载到 mcu 非活动分区
如果 W25Q64 中是全量固件，就用这个命令将固件下载到 mcu 的非活动区
<div style="text-align: center;">
    <img src="https://github.com/hanserisangel/Bootloader/blob/master/image/%5B6%5D.png" width="60%" height="60%" alt="工作流程">
</div>
显示下载成功
### 4.8 把差分固件从 W25Q64 下载到 mcu 非活动分区
如果 W25Q64 中是增量固件，就用这个命令将固件下载到 mcu 的非活动区
<div style="text-align: center;">
    <img src="https://github.com/hanserisangel/Bootloader/blob/master/image/%5B8%5D.png" width="60%" height="60%" alt="工作流程">
</div>
显示下载成功

**需要注意的是，差分升级用到了旧固件和差分固件两个输入来源，旧固件直接读取mcu内部flash的活动分区，差分固件则是本地/远程下载来的，所以进行差分升级的时候，务必要保证mcu内部flash的活动分区运行的是旧固件，要不然差分还原的结果是错的，得到的新固件也是运行不起来的。**

---

## 5. FLASH 分区表
### 5.1 W25Q64

<table style="width:100%">
    <tr>
        <td style="text-align:center;background:#F2F2F2;width:200px"><b>起始位置(字节)</b></td>
        <td style="text-align:center;background:#F2F2F2;width:200px"><b>结束位置(字节)</b></td>
        <td style="text-align:center;background:#F2F2F2;width:200px"><b>分区大小</b></td>
        <td style="text-align:center;background:#F2F2F2;width:400px"><b>存放的数据</b></td>
        <td style="text-align:center;background:#F2F2F2;width:200px"><b>数据大小</b></td>
        <td style="text-align:center;background:#F2F2F2;width:100px"><b>是否为热数据</b></td>
    </tr>
    <tr>
        <td style="text-align:center">0</td>
        <td style="text-align:center">4KB</td>
        <td style="text-align:center">4KB</td>
        <td style="text-align:center">ECDSA 公钥</td>
        <td style="text-align:center">91 byte</td>
        <td style="text-align:center">&#10062</td>
    </tr>
    <tr>
        <td style="text-align:center">4KB</td>
        <td style="text-align:center">8KB</td>
        <td style="text-align:center">4KB</td>
        <td style="text-align:center">OTA_Info 信息</td>
        <td style="text-align:center">28 byte</td>
        <td style="text-align:center">&#10062</td>
    </tr>
    <tr>
        <td style="text-align:center">8KB</td>
        <td style="text-align:center">12KB</td>
        <td style="text-align:center">4KB</td>
        <td style="text-align:center">mata元数据（ecdh公钥+盐值+iv）</td>
        <td style="text-align:center">97 byte</td>
        <td style="text-align:center">&#10062</td>
    </tr>
    <tr>
        <td style="text-align:center">12KB</td>
        <td style="text-align:center">16KB</td>
        <td style="text-align:center">4KB</td>
        <td style="text-align:center">包头+签名数据</td>
        <td style="text-align:center">20+70~20+72 byte</td>
        <td style="text-align:center">&#10062</td>
    </tr>
    <tr>
        <td style="text-align:center">16KB</td>
        <td style="text-align:center">64KB</td>
        <td style="text-align:center">48KB</td>
        <td style="text-align:center">APP断点续传的进度</td>
        <td style="text-align:center">不定</td>
        <td style="text-align:center">&#9989</td>
    </tr>
    <tr>
        <td style="text-align:center">0x10000</td>
        <td style="text-align:center">0X70000</td>
        <td style="text-align:center">384KB</td>
        <td style="text-align:center">固件包</td>
        <td style="text-align:center">固件大小</td>
        <td style="text-align:center">&#10062</td>
    </tr>
</table>

### 5.2 mcu flash

<table style="width:100%">
    <tr>
        <td style="text-align:center;background:#F2F2F2;width:200px"><b>存放的数据</b></td>
        <td style="text-align:center;background:#F2F2F2;width:200px"><b>起始位置(字节)</b></td>
        <td style="text-align:center;background:#F2F2F2;width:200px"><b>结束位置(字节)</b></td>
        <td style="text-align:center;background:#F2F2F2;width:100px"><b>分区大小</b></td>
        <td style="text-align:center;background:#F2F2F2;width:200px"><b>扇区(编号)</b></td>
    </tr>
    <tr>
        <td style="text-align:center"><b>bootloader</b></td>
        <td style="text-align:center">0x0800_0000</td>
        <td style="text-align:center">0x0802_0000</td>
        <td style="text-align:center">128KB</td>
        <td style="text-align:center">0-4</td>
    </tr>
    <tr>
        <td style="text-align:center"><b>APP 分区 A</b></td>
        <td style="text-align:center">0x0802_0000</td>
        <td style="text-align:center">0x0808_0000</td>
        <td style="text-align:center">384KB</td>
        <td style="text-align:center">5-7</td>
    </tr>
    <tr>
        <td style="text-align:center"><b>APP 分区 B</b></td>
        <td style="text-align:center">0x0808_0000</td>
        <td style="text-align:center">0x080e_0000</td>
        <td style="text-align:center">384KB</td>
        <td style="text-align:center">8-10</td>
    </tr>
</table>

---

## 6. 裁剪
本项目将本地升级、远程OTA升级、全量升级、差分升级全都集成到了一起，这使得 bootloader 体积偏大，所以大家移植使用这个项目的时候，可以根据需求进行裁剪，去掉你的项目中用不到的功能

1. **删除串口交互菜单**
适用于仅需要删除串口交互菜单，删除菜单后其它功能不变，不会造成影响

2. **远程 OTA 升级 + 差分升级**
适用于仅需要 OTA 升级 + 差分升级，并且删除本地升级和全量升级。但是，需要注意的是，你的 APP 程序需要能够下载服务器的固件到外部 flash，要不然用不了这个部分的裁剪，关于这个 APP 程序的模板，请查看我的另一个存储库（还未更新）

3. **远程 OTA 升级 + 全量升级**
适用于仅需要 OTA 升级 + 全量升级，并且删除本地升级和差分升级。但是，需要注意的是，你的 APP 程序需要能够下载服务器的固件到外部 flash，要不然用不了这个部分的裁剪，关于这个 APP 程序的模板，请查看我的另一个存储库（还未更新）
4. **本地固件升级 + 差分升级**
适用于仅需要本地升级 + 差分升级，并且删除远程 OTA 升级和全量升级。因为不需要 APP 程序从服务器下载固件，所以可以直接使用这个部分的裁剪，但是 APP 程序在成功运行后要向外部 flash 写入`运行正常标志`，才不会自动回滚
5. **本地固件升级 + 全量升级**
适用于仅需要本地升级 + 全量升级，并且删除远程 OTA 升级和差分升级。因为不需要 APP 程序从服务器下载固件，所以可以直接使用这个部分的裁剪，但是 APP 程序在成功运行后要向外部 flash 写入`运行正常标志`，才不会自动回滚
