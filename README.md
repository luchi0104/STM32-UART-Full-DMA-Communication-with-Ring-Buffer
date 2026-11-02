# STM32 UART Full-DMA Communication with Ring Buffer

基於 STM32L476RG 的 UART 全 DMA 通訊實作，整合 **Circular RX DMA、HT / TC / IDLE 事件處理、Ring Buffer、非阻塞 TX DMA、Pending Batch 重試機制及 UART 錯誤診斷**。

本專案以 STM32 HAL Library 為基礎，逐步實作 UART 資料接收、緩衝管理與非阻塞傳送，並透過實際硬體測試與 Debugger 分析 DMA 事件、資料搬運及錯誤處理流程。

> **專案狀態：功能性實作原型（Functional Prototype）**
>
> 已完成基本 UART Full-DMA Echo 測試與模組化驗證，但尚未針對所有中斷延遲、高負載及硬體錯誤情況完成可靠性驗證，因此不宣稱為 Production-Ready Driver。

---

## 1. 開發環境與硬體配置

| 項目 | 配置 |
|---|---|
| 開發板 | NUCLEO-L476RG |
| MCU | STM32L476RG |
| 開發環境 | STM32CubeIDE 2.2.0 |
| HAL Library | STM32L4 HAL |
| UART | USART2 |
| UART 腳位 | PA2（TX）、PA3（RX） |
| Baud Rate | 115200 |
| UART 格式 | 8N1 |
| RX DMA | DMA1 Channel 6，Circular Mode |
| TX DMA | DMA1 Channel 7，Normal Mode |
| RX DMA Buffer | 64 Bytes |
| TX DMA Buffer | 64 Bytes |
| Ring Buffer | 256 Slots（實際可使用 255 Bytes） |
| 測試工具 | Tera Term、STM32CubeIDE Debugger |

### DMA 設定

| 參數 | RX DMA | TX DMA |
|---|---|---|
| Direction | Peripheral to Memory | Memory to Peripheral |
| Mode | Circular | Normal |
| Peripheral Increment | Disable | Disable |
| Memory Increment | Enable | Enable |
| Data Width | Byte | Byte |
| Interrupt | Enabled | Enabled |

RX DMA 使用 Circular Mode 持續接收 UART 資料；TX DMA 使用 Normal Mode，由程式控制每次傳送的資料長度。

---

## 2. 系統架構

本專案採用 **RX DMA + Ring Buffer + TX DMA** 的雙 DMA 架構。

```text
                  PC / Tera Term
                        |
                     UART RX
                        |
                        v
                 USART2 Receiver
                        |
                        v
              RX Circular DMA (64 B)
                        |
                 HT / TC / IDLE
                        |
                        v
              UART RX Event Callback
                        |
                 RingBuffer_Push()
                        |
                        v
               Ring Buffer (256)
                        |
                 RingBuffer_Pop()
                        |
                        v
                 Main Loop
                        |
                        v
              TX Pending Batch (64 B)
                        |
              HAL_UART_Transmit_DMA()
                        |
                        v
                TX Normal DMA
                        |
                        v
                 USART2 TX
                        |
                        v
                  PC / Tera Term

             HAL_UART_TxCpltCallback()
                        |
                  tx_busy = 0
                        |
                允許下一批傳送
```

### 各模組職責

- **RX Circular DMA**：將 UART 接收資料自動搬運至記憶體。
- **HT / TC / IDLE Callback**：根據 DMA 接收進度處理新資料。
- **Ring Buffer**：暫存接收到的資料，分離接收與傳送流程。
- **TX Pending Batch**：保存尚未成功啟動傳送的資料。
- **TX Normal DMA**：非阻塞傳送資料至 USART2。
- **TX Complete Callback**：通知傳送完成並釋放 TX Buffer。

---

## 3. 專案檔案結構

```text
Core/
├── Inc/
│   ├── main.h
│   ├── uart_dma.h
│   └── ring_buffer.h
│
└── Src/
    ├── main.c
    ├── uart_dma.c
    ├── ring_buffer.c
    ├── stm32l4xx_it.c
    └── stm32l4xx_hal_msp.c

docs/
├── debugging_notes.md
└── test_matrix.md
```

### 模組說明

| 檔案 | 功能 |
|---|---|
| `main.c` | 系統初始化與主迴圈 |
| `uart_dma.h` | UART DMA 模組公開介面 |
| `uart_dma.c` | RX/TX DMA、Callback、Pending Batch 與統計 |
| `ring_buffer.h` | Ring Buffer 函式宣告 |
| `ring_buffer.c` | Ring Buffer 資料結構與 Push / Pop |
| `stm32l4xx_it.c` | UART / DMA 中斷處理 |
| `stm32l4xx_hal_msp.c` | GPIO、DMA Channel 與 NVIC 初始化 |

模組化後，`main.c` 主要負責呼叫：

```c
/* 初始化 UART DMA 模組 */
if (UART_DMA_AppInit(&huart2) != HAL_OK)
{
    Error_Handler();
}

/* 主迴圈 */
while (1)
{
    UART_DMA_AppProcess();
}
```

DMA 接收、傳送及 Callback 的細節集中於 `uart_dma.c`，降低 `main.c` 的複雜度。

---

## 4. 已實作功能

### 4.1 UART Circular RX DMA

使用：

```c
HAL_UARTEx_ReceiveToIdle_DMA()
```

啟動 UART DMA 接收。

透過以下事件處理接收資料：

- **HT（Half Transfer）**：DMA Buffer 接收至半滿位置。
- **TC（Transfer Complete）**：DMA 完成一圈資料搬運。
- **IDLE（Idle Line Detection）**：UART 接收線進入閒置狀態。

Callback 使用已處理位置追蹤資料區段，避免在基本測試中重複搬運相同資料。

### 4.2 Ring Buffer

實作獨立的 Ring Buffer 模組，提供：

- `RingBuffer_Init()`
- `RingBuffer_Push()`
- `RingBuffer_Pop()`
- `RingBuffer_Available()`
- `RingBuffer_Clear()`

Ring Buffer 使用 Head / Tail 索引管理資料，並記錄 Overflow 次數。

本專案採用保留一格的設計：

```text
Ring Buffer Size = 256
Usable Capacity  = 255 Bytes
```

### 4.3 非阻塞 UART TX DMA

使用：

```c
HAL_UART_Transmit_DMA()
```

將 Ring Buffer 中的資料批次傳送至 UART。

與逐 Byte Blocking TX 不同，DMA 啟動後 CPU 不需要等待整批資料傳送完成。

### 4.4 TX Pending Batch 與 HAL_BUSY Retry

為避免 TX DMA 啟動失敗時遺失資料，新增 Pending Batch 機制。

```text
Ring Buffer
     |
     v
TX Pending Buffer
     |
     v
HAL_UART_Transmit_DMA()
     |
     +---- HAL_OK ----> DMA 傳送
     |                     |
     |                     v
     |               TX Complete
     |                     |
     |                tx_busy = 0
     |
     +---- HAL_BUSY --> 保留 Pending
                              |
                              v
                         下次重試
```

主要狀態變數：

| 變數 | 用途 |
|---|---|
| `tx_busy` | TX DMA 是否正在傳送 |
| `tx_pending` | 是否存在待傳送批次 |
| `tx_pending_len` | 待傳送資料長度 |

當 `HAL_UART_Transmit_DMA()` 回傳 `HAL_BUSY` 時，程式保留 TX Buffer 內容，等待下一次主迴圈重新嘗試。

### 4.5 UART 錯誤診斷

透過 `HAL_UART_ErrorCallback()` 監測 UART 錯誤。

診斷項目包含：

| 錯誤 | 說明 |
|---|---|
| ORE | Overrun Error，接收資料未及時處理 |
| FE | Framing Error，資料框格式錯誤 |
| NE | Noise Error，接收訊號受到雜訊影響 |
| PE | Parity Error，奇偶校驗錯誤 |

目前已完成正常通訊下的 Error Counter 測試。

**注意：** 若使用較早的模組化封存版本，`uart_dma.c` 可能尚未包含 UART Error Callback。發布前應確認已合併最新的本機測試版本。

---

## 5. 功能測試與驗證

使用 NUCLEO-L476RG 搭配 Tera Term 進行 UART Echo 測試。

### 5.1 Full DMA Echo 測試

| 傳送長度 | 驗證項目 | 測試結果 |
|---|---|---|
| 20 Bytes | IDLE 短資料接收 | Pass |
| 64 Bytes | HT / TC 邊界 | Pass |
| 80 Bytes | Circular DMA Wrap-around | Pass |
| 128 Bytes | 兩次完整 DMA 循環 | Pass |

測試確認接收資料可透過 RX DMA、Ring Buffer 與 TX DMA 回傳至電腦。

### 5.2 TX DMA 傳送統計

| 測試長度 | TX DMA Start | TX DMA Complete | Ring Buffer Overflow |
|---|---:|---:|---:|
| 20 Bytes | 1 | 1 | 0 |
| 64 Bytes | 2 | 2 | 0 |
| 80 Bytes | 3 | 3 | 0 |
| 128 Bytes | 4 | 4 | 0 |

TX DMA 批次數可能因接收時序而不同，不一定等於資料長度除以 TX Buffer 大小。

### 5.3 HAL_BUSY 故障注入

為驗證 Pending Batch 的重試機制，曾在測試版本中模擬一次 `HAL_BUSY`。

測試結果：

```text
tx_dma_busy_count     = 1
tx_dma_start_count    = 1
tx_dma_complete_count = 1
tx_dma_error_count    = 0

Ring Buffer Overflow  = 0
Echo Result           = Pass
```

結果顯示，在模擬 DMA 啟動失敗的情況下，待傳送資料成功保留，並於下一次嘗試完成傳送。

此測試不代表已驗證真實 DMA 傳輸中途故障的恢復能力。

---

## 6. Debugging 經驗

### 6.1 Circular DMA 的 Size=64 邊界問題

開發過程中曾發現：

```text
實際傳送：20 Bytes
Ring Buffer Push：84 Bytes
```

進一步使用 Event Log 發現：

```text
Event     = IDLE
Size      = 64
CNDTR     = 64
Old Pos   = 0
```

透過零輸入測試與 STM32 HAL 原始碼分析，確認 Circular DMA 在特定條件下可能回報 `IDLE, Size=64`。

問題並非 `Size=64` 本身錯誤，而是原本的 Callback 將 Size 直接解讀為新增資料量，導致多餘的資料搬運。

後續加入接收位置追蹤與初始空 IDLE 防護，並通過基本邊界測試。

### 6.2 HAL_BUSY 與 TX Buffer 管理

在 TX DMA 實驗中，連續啟動兩次 DMA 傳送：

```text
First TX Request  = HAL_OK
Second TX Request = HAL_BUSY
```

因此加入 `tx_busy` 與 Pending Batch，避免 DMA 正在傳送時覆寫 TX Buffer，並保留尚未成功啟動的資料。

### 6.3 使用 Debug Counter 與 Event Log

本專案透過 STM32CubeIDE Expressions 觀察：

- DMA HT / TC / IDLE 次數。
- DMA CNDTR 與接收位置。
- Ring Buffer Head / Tail。
- Ring Buffer Overflow。
- TX DMA Start / Complete。
- HAL_BUSY 與 Error Counter。

這些資訊有助於分析資料遺失、重複搬運與 DMA 狀態問題。

詳細紀錄請參考 [Debugging Notes](docs/debugging_notes.md)。

---

## 7. 編譯與執行

1. 使用 STM32CubeIDE 匯入專案：**File → Import → Existing Projects into Workspace**。
2. 確認 `.ioc` 中的 USART2、RX/TX DMA 與 NVIC 設定。
3. Build 專案並燒錄至 NUCLEO-L476RG。
4. 開啟 Tera Term，連接對應的 ST-LINK Virtual COM Port。
5. 設定 UART 為 115200 baud、8N1，並關閉 Local Echo。
6. 傳送測試字串或 Binary File。
7. 檢查 Echo 內容，並使用 Debugger 觀察 DMA 與 Ring Buffer 統計。

---

## 8. 已知限制

本專案目前屬於功能性學習原型，仍有以下限制：

- **RX DMA 中斷延遲**：目前的 HT / TC / IDLE 位置追蹤尚未保證能處理所有中斷延遲與事件交錯情況。
- **DMA Buffer 覆寫**：Ring Buffer Overflow Counter 無法偵測資料在 RX DMA Buffer 中被覆寫。
- **TX 傳輸中途錯誤**：Pending Batch 可處理 DMA 啟動失敗，但尚未解決部分資料已送出後的恢復問題。
- **高負載測試**：尚未完成長時間連續資料傳輸與高負載壓力測試。
- **自動化資料驗證**：仍可進一步加入完整的 Binary Byte-by-Byte 比對與測試報告。

因此，本專案不宣稱具備完整的無資料遺失保證。

---

## 9. 後續改善方向

- 改善 RX Circular DMA 的事件追蹤與覆寫偵測。
- 建立 UART DMA Timeout 與錯誤恢復狀態機。
- 增加長時間資料完整性與壓力測試。
- 提供模組化 Debug Statistics API。
- 加入 Command Parser，支援 UART 指令解析。
- 探索 FreeRTOS 環境下的 UART DMA 通訊設計。

---

## 10. 專案總結

本專案從 UART 基礎通訊出發，逐步整合：

**UART Interrupt → Ring Buffer → Circular RX DMA → HT / TC / IDLE → TX DMA → Pending Batch → Error Diagnostics → 模組化設計**

透過實際硬體測試、Debugger 觀察與 HAL 原始碼分析，理解 DMA 非阻塞傳輸、緩衝區管理、Callback、中斷事件及錯誤處理的設計原理。

本專案主要目的為建立 STM32 韌體通訊開發能力，並作為後續學習 SPI、I2C、FreeRTOS 與更完整通訊協定設計的基礎。

---
