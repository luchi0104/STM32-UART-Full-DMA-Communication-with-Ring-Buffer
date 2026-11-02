# Test Matrix

| Scenario | Observed result | Scope |
| --- | --- | --- |
| 20 B RX/TX DMA Echo | Pass | Basic short message |
| 64 B RX/TX DMA Echo | Pass | HT / TC boundary |
| 80 B RX/TX DMA Echo | Pass | First wrap |
| 128 B RX/TX DMA Echo | Pass | Two circular RX cycles |
| Pending-batch simulated HAL_BUSY | Pass | One injected start failure, then retry |
| UART Error Callback with normal 20 B traffic | User reports zero error counters | Tested locally; latest callback not in archived module |
| Long-duration throughput / stress | Not tested | Open |
| DMA overwrite / delayed IRQ | Not validated | Open |
| Mid-transfer TX fault recovery | Not implemented/validated | Open |

The tests above are reported hardware observations, not a formal verification suite. Capture terminal logs and debugger screenshots before final GitHub publication.
