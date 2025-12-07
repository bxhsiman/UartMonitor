# UART Monitor (ESP32-C3/S3)

ESP32-C3/S3 通过 USB Serial/JTAG 做 UART1 透传的小工具，方便把 USB 口当作一个“软串口”。在 `idf.py monitor` 中输入内容并按 `ESC` 一次性发送到 UART1，同时将 UART1 收到的数据带时间戳打印回 PC，适合做嵌入式调试桥接。

## 特性
- USB Serial/JTAG ↔ UART1 双向透传
- 输入缓冲，按 `ESC` 发送并自动追加 `\r\n`
- UART1 收到的数据带时间戳回显
- 支持退格删除、不改动流控
- 引脚和波特率可在代码宏里修改

## 默认引脚
- UART1 TX: `GPIO2`
- UART1 RX: `GPIO6`
- 波特率: `115200`

如需修改，编辑 `main/main.c` 顶部的 `UART1_TXD_PIN`、`UART1_RXD_PIN`、`UART1_BAUD_RATE` 等宏。

## 编译与烧录
需要已安装的 ESP-IDF（建议 5.x 及以上）。步骤：

```bash
idf.py set-target esp32c3
idf.py build
idf.py flash
idf.py monitor
```

`monitor` 进入后即可与 UART1 通信，退出使用 `Ctrl+]`。

## 使用方法
1) 在 `idf.py monitor` 终端输入内容（支持长串），回显输入字符。
2) 按 `ESC`：将当前缓冲追加 `\r\n` 后发送到 UART1。
3) UART1 收到的数据会以 `[HH:MM:SS.mmm] UART1->USB [...]` 形式带时间戳打印。
4) 退格键可删除已输入字符；删除键同样有效。

## 硬件连接示例
- 开发板 USB-C 口直接连接电脑（走 USB Serial/JTAG）。
- 将 `GPIO2` 接下位机的 RX，`GPIO6` 接下位机的 TX，共地。

## 目录结构
```
├── CMakeLists.txt          # 项目入口
├── main
│   ├── CMakeLists.txt
│   └── main.c              # 透传逻辑
└── README.md
```

## 常见问题
- **没收到串口数据**：确认下位机波特率匹配且 RX/TX 交叉接线，确保共地。
- **ESC 无反应**：检查终端是否截获 ESC（如某些多路复用器），可换用 `idf.py monitor` 或普通串口终端。
- **缓冲溢出**：输入过长会提示缓冲已满，先按 `ESC` 发送再继续输入。***
