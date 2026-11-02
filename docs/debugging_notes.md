# Debugging Notes — STM32 UART DMA

## 1. Circular RX DMA: unexpected extra 64 bytes

**Symptom:** 20-byte input produced 84 RX push attempts; 64-byte input produced 128. Enlarging the ring buffer removed its overflow but did not fix the excess copies.

**Evidence:** Event log recorded `IDLE, Size=64, CNDTR=64, old_pos=0` before useful receive progress. A zero-input experiment reproduced this event. Inspecting `HAL_UART_IRQHandler()` in the STM32L4 HAL showed a circular-DMA IDLE branch that reports `RxXferSize` when the DMA remaining count equals the configured buffer length.

**Root cause in application logic:** Treating `Size=64` as 64 *new* bytes and copying `[0,64)` unconditionally. `Size` denotes a buffer position/boundary, not the number of bytes newly received by this callback. `CNDTR=64` may mean no transfer yet or a completed wrap.

**Teaching mitigation:** An initial empty-IDLE guard combined with HT/TC position tracking eliminated extra pushes in the tested 20/32/64/80/128-byte cases. **Not a general proof of correctness**: delayed events and DMA overwrites remain possible.

## 2. Buffer capacity confusion

A 64-slot ring buffer using the one-slot-empty rule stores only 63 bytes. Expanding it to 256 slots increased usable capacity to 255 and eliminated observed software overflow, but did not address duplicate RX processing.

## 3. HAL_BUSY during TX DMA

**Experiment:** Two immediate calls to `HAL_UART_Transmit_DMA()` returned `HAL_OK` then `HAL_BUSY`. A TX completion callback later returned the UART state to READY.

**Mitigation:** Keep a pending TX batch in a persistent 64-byte buffer and retry on start failure. A one-shot simulated `HAL_BUSY` test completed successfully without losing the 20-byte Echo.

**Limit:** A DMA transfer that starts but fails mid-stream may have transmitted a partial prefix; blindly resending the full batch can duplicate bytes.

## 4. Callback and IRQ separation

`DMA1_Channel6_IRQHandler`, `DMA1_Channel7_IRQHandler`, and `USART2_IRQHandler` call HAL IRQ handlers. The HAL then invokes `HAL_UARTEx_RxEventCallback()` or `HAL_UART_TxCpltCallback()` as appropriate. Callbacks run in interrupt context and should remain short and non-blocking.

## 5. Follow-up investigations

- Reproduce and measure RX DMA overwrite under delayed processing using a controlled test harness.
- Record UART ORE/FE/NE/PE errors and test recovery, not just zero-error operation.
- Automate binary byte-for-byte integrity comparisons across long streams.
- Verify ring-buffer and pending-batch behavior under sustained input rates.
