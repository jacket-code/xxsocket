# xxsocket
A cross platform socket wrapper APIs, support win32  &amp; linux  &amp; ios &amp; android &amp; wp8 &amp; wp8.1-universal &amp; win10-universal

current version: 0.0.330.3

## texas_holdem_server (原创示例)

我在这个仓库基础上新增了一个**可编译运行**的德州扑克（Texas Hold'em）服务端示例，使用 `xxsocket` 做 TCP 监听，协议为 **JSONL（每行一个 JSON 对象）**。

### 构建与运行

```bash
cmake -S . -B build
cmake --build build -j
./build/poker_server 7777 data
```

服务端会在 `data/` 目录下生成/读取：

- `data/accounts.jsonl`: 玩家账户余额（JSONL）
- `data/config.json`: 服务端配置（可选，支持热加载；单行 JSON 对象）

示例 `data/config.json`：

```json
{"defaultBalance":100000,"actionTimeoutMs":15000,"storeFlushMs":5000,"adminKey":"","rlCapacity":60,"rlRefillPerSecond":40}
```

### 快速试玩（用 netcat）

开两个终端分别连接：

```bash
nc 127.0.0.1 7777
```

然后依次发送（每条一行）：

```json
{"type":"hello","name":"Alice"}
{"type":"sit","tableId":1,"seat":0,"buyin":10000}
{"type":"ready","tableId":1,"ready":true}
```

另一个终端：

```json
{"type":"hello","name":"Bob"}
{"type":"sit","tableId":1,"seat":1,"buyin":10000}
{"type":"ready","tableId":1,"ready":true}
```

轮到你行动时发送：

```json
{"type":"action","tableId":1,"action":"check"}
```

或：

```json
{"type":"action","tableId":1,"action":"call"}
```

下注/加注使用 `amount` 表示 **本轮下注总额（raise/bet to）**：

```json
{"type":"action","tableId":1,"action":"bet","amount":200}
{"type":"action","tableId":1,"action":"raise","amount":500}
```

### 常用消息（节选）

```json
{"type":"list_rooms"}
{"type":"create_room","roomType":"private","name":"myroom","password":"1234","maxSeats":6,"sb":50,"bb":100,"buyinMin":1000,"buyinMax":20000}
{"type":"join_room","roomId":2,"password":"1234"}
{"type":"list_tables","roomId":2}
{"type":"observe","tableId":1,"on":true}
{"type":"auto","tableId":1,"on":true}
{"type":"topup","tableId":1,"amount":5000}
{"type":"hand_history","tableId":1,"handId":1}
{"type":"tournament_register","roomId":3}
```

### 代码位置

- `src/poker/`: 发牌、轮转、边池、摊牌、牌型评估
- `src/server/`: TCP 服务器与 JSONL 协议处理
- `src/tests/`: 引擎基础测试（牌型与筹码守恒）
