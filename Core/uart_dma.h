#ifndef UART_DMA_H
#define UART_DMA_H
#include "main.h"

HAL_StatusTypeDef UART_DMA_AppInit(UART_HandleTypeDef *huart);
void UART_DMA_AppProcess(void);

/* Optional debugger counters (definitions in uart_dma.c) */
extern volatile uint32_t dma_idle_count, dma_ht_count, dma_tc_count;
extern volatile uint32_t dma_invalid_size_count;
extern volatile uint32_t dma_push_attempt_count, dma_push_success_count;
extern volatile uint32_t tx_dma_start_count, tx_dma_complete_count;
extern volatile uint32_t tx_dma_busy_count, tx_dma_error_count;
extern volatile uint32_t dma_log_count;
#endif
