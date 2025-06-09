## ✅ RUDP 開發進度 Checklist

### 🔧 基礎功能
- [x] 建立 `UdpSocketBase` 封裝 socket 操作（跨平台）
- [x] 實作 `UdpServer` 類別與接收 thread、plugin 架構
- [x] 實作 `UdpClient` 類別並支援 `connect_to()` 發送封包
- [x] 定義封包格式：`header` 與 `packet` 結構
- [x] 封包序列化與反序列化（`PacketHelper`）

### 🔌 插件系統
- [x] 定義 `IRudpPlugin` Plugin Interface
- [x] 實作 `ConsoleLogPlugin`（顯示收發封包）

### 📦 封包管理
- [ ] 支援 ACK 封包處理與應答機制
- [ ] 封包重送機制與 timeout 計時器
- [ ] 封包順序控制（seqId 對應與處理）
- [ ] 封包 retry 策略（次數與間隔）
- [ ] 封包管理器（`PacketManager` / `SessionHandler`）

---