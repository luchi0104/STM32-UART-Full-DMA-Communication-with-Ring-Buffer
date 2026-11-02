#include "uart_dma.h"
#include "ring_buffer.h"

static UART_HandleTypeDef *uart_handle;
static DMA_HandleTypeDef *rx_dma_handle;


/* ================= Configuration ================= */

#define DMA_RX_SIZE   64U
#define TX_DMA_SIZE   64U
#define DMA_LOG_SIZE  16U

/* ================= RX DMA ================= */

static uint8_t dma_rx_buffer[DMA_RX_SIZE];
static RingBuffer_t uart_rx_rb;

static uint16_t dma_old_pos = 0;

/* ================= TX DMA ================= */

static uint8_t tx_dma_buffer[TX_DMA_SIZE];

static volatile uint8_t tx_busy = 0;

static uint8_t tx_pending = 0;
static uint16_t tx_pending_len = 0;

/* ================= RX Debug Statistics ================= */

volatile uint32_t dma_idle_count = 0;
volatile uint32_t dma_ht_count = 0;
volatile uint32_t dma_tc_count = 0;
volatile uint32_t dma_invalid_size_count = 0;

volatile uint32_t dma_push_attempt_count = 0;
volatile uint32_t dma_push_success_count = 0;

/* ================= TX Debug Statistics ================= */

volatile uint32_t tx_dma_start_count = 0;
volatile uint32_t tx_dma_complete_count = 0;
volatile uint32_t tx_dma_busy_count = 0;
volatile uint32_t tx_dma_error_count = 0;

/* ================= DMA Event Log ================= */

typedef struct
{
    uint32_t sequence;
    uint32_t event;

    uint16_t size;
    uint16_t old_pos;
    uint16_t copied;
    uint16_t reserved;
    uint16_t cndtr;
    uint16_t write_pos;

} DMA_EventLog_t;

volatile DMA_EventLog_t dma_log[DMA_LOG_SIZE];
volatile uint32_t dma_log_count = 0;


static void UART_TX_DMA_Process(void);

HAL_StatusTypeDef UART_DMA_AppInit(UART_HandleTypeDef *huart)
{
    if (huart == NULL || huart->hdmarx == NULL) return HAL_ERROR;
    uart_handle = huart;
    rx_dma_handle = huart->hdmarx;
    RingBuffer_Init(&uart_rx_rb);
    return HAL_UARTEx_ReceiveToIdle_DMA(huart, dma_rx_buffer, DMA_RX_SIZE);
}

void UART_DMA_AppProcess(void)
{
    UART_TX_DMA_Process();
}




void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart != uart_handle)
    {
        return;
    }

    // Record successful TX completion
    tx_dma_complete_count++;

    // Release TX DMA buffer
    tx_busy = 0;
}




void HAL_UARTEx_RxEventCallback(
    UART_HandleTypeDef *huart,
    uint16_t Size)
{
    if (huart != uart_handle)
        return;

    HAL_UART_RxEventTypeTypeDef event =
        HAL_UARTEx_GetRxEventType(huart);

    if (event == HAL_UART_RXEVENT_IDLE)
        dma_idle_count++;
    else if (event == HAL_UART_RXEVENT_HT)
        dma_ht_count++;
    else if (event == HAL_UART_RXEVENT_TC)
        dma_tc_count++;

    uint16_t old_before = dma_old_pos;
    uint32_t attempts_before = dma_push_attempt_count;

    uint16_t cndtr =
        __HAL_DMA_GET_COUNTER(rx_dma_handle);

    uint16_t write_pos =
        (DMA_RX_SIZE - cndtr) % DMA_RX_SIZE;

    if (Size == 0U || Size > DMA_RX_SIZE)
    {
        dma_invalid_size_count++;
        goto log_event;
    }

    /*
     * Special case:
     * Empty IDLE at the initial DMA position.
     *
     * This guard is only used before the first
     * confirmed DMA progress.
     */
    if (event == HAL_UART_RXEVENT_IDLE &&
        Size == DMA_RX_SIZE &&
        cndtr == DMA_RX_SIZE &&
        dma_old_pos == 0U &&
        dma_ht_count == 0U &&
        dma_tc_count == 0U)
    {
        goto log_event;
    }

    /*
     * TC confirms the DMA buffer end boundary.
     */
    uint16_t pos = Size;

    if (event == HAL_UART_RXEVENT_TC)
    {
        pos = DMA_RX_SIZE;
    }
    else if (event == HAL_UART_RXEVENT_HT)
    {
        pos = DMA_RX_SIZE / 2U;
    }

    /*
     * When a completed DMA cycle is followed
     * by a new position, restart from index 0.
     */
    if (dma_old_pos == DMA_RX_SIZE &&
        pos < DMA_RX_SIZE)
    {
        dma_old_pos = 0U;
    }

    /*
     * Do not interpret arbitrary backward
     * positions as new data.
     */
    if (pos < dma_old_pos)
    {
        goto log_event;
    }

    for (uint16_t i = dma_old_pos; i < pos; i++)
    {
        dma_push_attempt_count++;

        if (RingBuffer_Push(
                &uart_rx_rb,
                dma_rx_buffer[i]))
        {
            dma_push_success_count++;
        }
    }

    dma_old_pos = pos;

log_event:
    uint32_t index = dma_log_count % DMA_LOG_SIZE;

    dma_log[index].sequence = dma_log_count + 1U;
    dma_log[index].event = (uint32_t)event;
    dma_log[index].size = Size;
    dma_log[index].old_pos = old_before;
    dma_log[index].copied =
        (uint16_t)(dma_push_attempt_count - attempts_before);
    dma_log[index].reserved = 0U;
    dma_log[index].cndtr = cndtr;
    dma_log[index].write_pos = write_pos;

    dma_log_count++;
}



static void UART_TX_DMA_Process(void)
{
    /* 1. DMA is still transmitting */
    if (tx_busy)
    {
        return;
    }

    /*
     * 2. Prepare a new batch only when
     *    there is no pending batch.
     */
    if (!tx_pending)
    {
        uint16_t length = 0;
        uint8_t data;

        while (length < TX_DMA_SIZE)
        {
            if (!RingBuffer_Pop(&uart_rx_rb, &data))
            {
                break;
            }

            tx_dma_buffer[length] = data;
            length++;
        }

        if (length == 0U)
        {
            return;
        }

        tx_pending_len = length;
        tx_pending = 1;
    }

    /*
     * 3. Try to start DMA.
     *
     * The pending buffer must not be modified
     * until DMA starts and completes.
     */
    tx_busy = 1;

    HAL_StatusTypeDef status =
        HAL_UART_Transmit_DMA(
            uart_handle,
            tx_dma_buffer,
            tx_pending_len
        );

    if (status == HAL_OK)
    {
        tx_dma_start_count++;

        /*
         * DMA now owns the TX buffer.
         * The completion callback releases it.
         */
        tx_pending = 0;
    }
    else
    {
        /*
         * DMA did not accept the batch.
         * Keep pending data for retry.
         */
        tx_busy = 0;

        if (status == HAL_BUSY)
        {
            tx_dma_busy_count++;
        }
        else
        {
            tx_dma_error_count++;
        }
    }
}




