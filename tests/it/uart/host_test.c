#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <at.h>
#include <circularbuffer.h>
#include <circularuart.h>
#include <data_interface.h>
#include <commands/at_message.h>

USART_TypeDef test_usart;
uint32_t test_primask;
unsigned test_remap_disabled, test_barriers, test_irq_restores;
static bool irq_enabled, auto_drain;
static uint32_t ticks, tick_step;
static unsigned drain_period, tick_calls;
static uint8_t wire[16384];
static size_t wire_size;
static const char *inject_on_tick;
static GPIO_InitTypeDef rx_gpio, tx_gpio;
static UART_InitTypeDef uart_config;

void USART1_IRQHandler(void);

void test_memory_barrier(void) { ++test_barriers; }
void test_restore_irq(uint32_t value)
{
    assert(test_primask == 1);
    ++test_irq_restores;
    test_primask = value;
}
void HAL_GPIO_Init(void *port, GPIO_InitTypeDef *gpio)
{
    assert(port == GPIOA);
    if (gpio->Pin == GPIO_PIN_10)
        rx_gpio = *gpio;
    else
    {
        assert(gpio->Pin == GPIO_PIN_9);
        tx_gpio = *gpio;
    }
}
int HAL_UART_Init(UART_HandleTypeDef *uart)
{
    uart_config = uart->Init;
    return HAL_OK;
}
void HAL_NVIC_SetPriority(int irq, unsigned priority, unsigned subpriority)
{
    assert(irq == USART1_IRQn);
}
void HAL_NVIC_EnableIRQ(int irq) { assert(irq == USART1_IRQn); irq_enabled = true; }
void HAL_NVIC_DisableIRQ(int irq) { assert(irq == USART1_IRQn); irq_enabled = false; }

// Invoke the real ISR for each byte; model RX and TX separately since a real
// USART DR has distinct receive/transmit storage despite sharing one address.
static void feed(const void *data, size_t size)
{
    const uint8_t *bytes = data;
    assert(irq_enabled && !test_primask);
    for (size_t i = 0; i < size; ++i)
    {
        assert(CircularUART_GetUnreadCount() < RX_BUFFERSIZE - 1);
        test_usart.SR = UART_FLAG_RXNE;
        test_usart.DR = bytes[i];
        USART1_IRQHandler();
        test_usart.SR = 0;
    }
}
static void feedText(const char *text) { feed(text, strlen(text)); }

static void drainOne(void)
{
    assert(irq_enabled && !test_primask);
    const uint16_t before = CircularUART_GetUnsentCount();
    test_usart.SR = UART_FLAG_TXE;
    USART1_IRQHandler();
    test_usart.SR = 0;
    if (CircularUART_GetUnsentCount() != before)
    {
        assert(wire_size < sizeof(wire));
        wire[wire_size++] = test_usart.DR;
    }
}
static void drainAll(void)
{
    unsigned limit = TX_BUFFERSIZE + 1;
    while (test_usart.CR1 & UART_IT_TXE)
    {
        assert(limit-- != 0);
        drainOne();
    }
    assert(CircularUART_GetUnsentCount() == 0);
}
uint32_t HAL_GetTick(void)
{
    ticks += tick_step;
    if (inject_on_tick != NULL && CircularUART_GetUnsentCount() == TX_BUFFERSIZE - 1)
    {
        const char *text = inject_on_tick;
        inject_on_tick = NULL;
        feedText(text);
    }
    if (auto_drain && ++tick_calls % drain_period == 0)
        drainOne();
    return ticks;
}

// Non-AT handlers are outside this test. Fail if one is unexpectedly dispatched.
#define UNUSED_HANDLER(name) \
    bool name(const AtMessage *message, uint8_t *out, uint16_t *size) \
    { assert(!"unexpected command handler"); return false; }
UNUSED_HANDLER(cmdDfuOpen)
UNUSED_HANDLER(cmdDfuWrite)
UNUSED_HANDLER(cmdDfuClose)
UNUSED_HANDLER(cmdReset)
UNUSED_HANDLER(cmdResetActiveApp)
UNUSED_HANDLER(cmdFOpen)
UNUSED_HANDLER(cmdFClose)
UNUSED_HANDLER(cmdFWrite)

static void init(void)
{
    test_primask = 0;
    test_remap_disabled = 0;
    atInitUartBuffers();
    assert(test_remap_disabled);
    assert(uart_config.BaudRate == 115200);
    assert(uart_config.WordLength == UART_WORDLENGTH_8B);
    assert(uart_config.StopBits == UART_STOPBITS_1);
    assert(uart_config.Parity == UART_PARITY_NONE);
    assert(uart_config.HwFlowCtl == UART_HWCONTROL_NONE);
    assert(uart_config.Mode == UART_MODE_TX_RX);
    assert(rx_gpio.Mode == GPIO_MODE_INPUT);
    assert(tx_gpio.Mode == GPIO_MODE_AF_PP);
    assert(irq_enabled && !test_primask);
    assert(test_usart.CR1 & UART_IT_RXNE);
    assert(!(test_usart.CR1 & UART_IT_TXE));
    wire_size = 0;
    ticks = 0;
    tick_step = 1;
    tick_calls = 0;
    drain_period = 1;
    auto_drain = false;
    inject_on_tick = NULL;
}

static void expectRead(Result_t expected, const void *bytes, uint16_t length, uint16_t capacity)
{
    uint8_t out[RESPONSE_BUFFER_SIZE + 1];
    memset(out, 0xA5, sizeof(out));
    uint16_t size = 0xFFFF;
    assert(dataInterfaceRead(out, capacity, &size) == expected);
    assert(size == length);
    if (length != 0)
        assert(memcmp(out, bytes, length) == 0);
    for (size_t i = length; i < sizeof(out); ++i)
        assert(out[i] == 0xA5);
}
static void expectAT(void) { expectRead(SUCCESS_T, "AT", 2, RESPONSE_BUFFER_SIZE); }
static void expectWaiting(void) { expectRead(INPROGRESS_T, NULL, 0, RESPONSE_BUFFER_SIZE); }
static void expectError(void) { expectRead(ERROR_T, NULL, 0, RESPONSE_BUFFER_SIZE); }

static void testFraming(void)
{
    init();
    const char request[] = "AT\r\n";
    // Every possible split, including a terminator split across calls.
    for (size_t split = 0; split < sizeof(request) - 1; ++split)
    {
        feed(request, split);
        expectWaiting();
        feed(request + split, sizeof(request) - 1 - split);
        expectAT();
        expectWaiting();
    }
    for (size_t i = 0; i < sizeof(request) - 1; ++i)
    {
        feed(request + i, 1);
        if (i == sizeof(request) - 2)
            expectAT();
        else
            expectWaiting();
    }
    feedText("noise\r\nat\r\nAAAA");
    expectWaiting();
    feedText("T\r\nAT\r\nAT+UNKNOWN\r\nAT");
    expectAT();
    expectAT();
    expectRead(SUCCESS_T, "AT+UNKNOWN", 10, RESPONSE_BUFFER_SIZE);
    expectWaiting();
    feedText("\r\n");
    expectAT();

    // Continuous noise must not fill the frame buffer or prevent recovery.
    uint8_t noise[1500];
    memset(noise, 'x', sizeof(noise));
    for (unsigned i = 0; i < 4; ++i)
    {
        feed(noise, sizeof(noise));
        expectWaiting();
    }
    feedText("AT\r\n");
    expectAT();
}

static void testBounds(void)
{
    uint8_t request[RESPONSE_BUFFER_SIZE + 100];
    memset(request, 'x', sizeof(request));
    request[0] = 'A';
    request[1] = 'T';
    for (size_t length = RESPONSE_BUFFER_SIZE - 1; length <= RESPONSE_BUFFER_SIZE + 1; ++length)
    {
        init();
        feed(request, length);
        expectWaiting();
        feedText("\r");
        expectWaiting();
        feedText("\nAT\r\n");
        if (length <= RESPONSE_BUFFER_SIZE)
            expectRead(SUCCESS_T, request, length, RESPONSE_BUFFER_SIZE);
        else
            expectError();
        expectAT();
    }

    init();
    feed(request, sizeof(request));
    expectWaiting();
    // Embedded commands in an oversized frame must not be dispatched.
    feedText("AT+RESET\r");
    expectWaiting();
    feedText("\nAT\r\n");
    expectError();
    expectAT();

    feedText("AT\r\nAT\r\nAT\r\n");
    expectRead(ERROR_T, NULL, 0, 1);
    expectRead(SUCCESS_T, "AT", 2, 2);
    expectRead(ERROR_T, NULL, 0, 0);
    expectWaiting();
}

static void testReset(void)
{
    init();
    feedText("AT+partial");
    expectWaiting();
    dataInterfaceClearBuffers(true, false);
    feedText("AT\r\n");
    expectAT();
    feedText("AT+partial");
    expectWaiting();
    dataInterfaceInit(BAUDRATE);
    feedText("AT\r\n");
    expectAT();

    uint8_t oversized[1500];
    memset(oversized, 'x', sizeof(oversized));
    oversized[0] = 'A';
    oversized[1] = 'T';
    for (unsigned reinit = 0; reinit < 2; ++reinit)
    {
        feed(oversized, sizeof(oversized));
        expectWaiting();
        if (reinit)
            dataInterfaceInit(BAUDRATE);
        else
            dataInterfaceClearBuffers(true, false);
        feedText("AT\r\n");
        expectAT();
    }

    feedText("AT\r\n");
    assert(circularUartSend((const uint8_t*)"old", 3) == 3);
    const unsigned restores = test_irq_restores;
    __disable_irq();
    dataInterfaceClearBuffers(true, true);
    assert(test_primask == 1); // Preserve an already masked caller.
    assert(test_irq_restores == restores + 2);
    test_primask = 0;
    assert(CircularUART_GetUnreadCount() == 0);
    assert(CircularUART_GetUnsentCount() == 0);
    assert(!(test_usart.CR1 & UART_IT_TXE));
    assert(test_usart.CR1 & UART_IT_RXNE);
}

static void testReplies(void)
{
    init();
    feedText("AT\r\nAT\r\nAT+UNKNOWN\r\nAT\r\n");
    for (unsigned i = 0; i < 4; ++i)
        atPoolUartBuffer();
    atPoolUartBuffer();
    drainAll();
    const char expected[] = "OK\nOK\nERROR\nOK\n";
    assert(wire_size == sizeof(expected) - 1);
    assert(memcmp(wire, expected, wire_size) == 0);

    // Wrap both rings many times and verify exact response counts.
    wire_size = 0;
    for (unsigned i = 0; i < 1000; ++i)
    {
        feedText("AT\r\n");
        atPoolUartBuffer();
        drainAll();
    }
    assert(wire_size == 3000);
    for (size_t i = 0; i < wire_size; i += 3)
        assert(memcmp(wire + i, "OK\n", 3) == 0);
}

static void testSends(void)
{
    uint8_t payload[TX_BUFFERSIZE * 4 + 17];
    for (size_t i = 0; i < sizeof(payload); ++i)
        payload[i] = (uint8_t)i;

    for (unsigned simple = 0; simple < 2; ++simple)
    {
        init();
        assert(circularUartSend((const uint8_t*)"prefix", 6) == 6);
        feedText("AT\r\n");
        inject_on_tick = "AT\r\n";
        auto_drain = true;
        // Total duration exceeds one second, but individual progress does not.
        tick_step = 10;
        drain_period = 3;
        uint16_t sent = simple ? dataSimpleSend(payload, sizeof(payload)) :
                                 dataInterfaceSend(payload, sizeof(payload));
        assert(sent == sizeof(payload));
        auto_drain = false;
        drainAll();
        assert(wire_size == sizeof(payload) + 6);
        assert(memcmp(wire, "prefix", 6) == 0);
        assert(memcmp(wire + 6, payload, sizeof(payload)) == 0);
        expectAT();
        expectAT();
        expectWaiting();
        assert(dataInterfaceSend(payload, 0) == 0);
        assert(dataSimpleSend(payload, 0) == 0);
    }

    for (unsigned simple = 0; simple < 2; ++simple)
    {
        init();
        ticks = UINT32_MAX - 500; // Exercise timeout across tick rollover.
        uint32_t start = ticks;
        uint16_t sent = simple ? dataSimpleSend(payload, sizeof(payload)) :
                                 dataInterfaceSend(payload, sizeof(payload));
        assert(sent == TX_BUFFERSIZE - 1);
        assert((uint32_t)(ticks - start) >= 1000);
        assert((uint32_t)(ticks - start) < 1010);
        assert(CircularUART_GetUnsentCount() == sent);
        // A completely full queue accepts nothing and keeps the old data.
        assert(dataInterfaceSend(payload, 1) == 0);
        drainAll();
        assert(wire_size == sent);
        assert(memcmp(wire, payload, sent) == 0);
        auto_drain = true;
        assert(dataInterfaceSend(payload + sent, sizeof(payload) - sent) == sizeof(payload) - sent);
        auto_drain = false;
        drainAll();
        assert(wire_size == sizeof(payload));
        assert(memcmp(wire, payload, sizeof(payload)) == 0);
    }
}

static void testRingWrap(void)
{
    CircularBufferObject_t ring;
    uint8_t memory[8], output[8];
    CircularBuffer_init(&ring, memory, 3);
    assert(CircularBuffer_pushBack(&ring, (const uint8_t*)"abcdefg", 7) == 7);
    assert(!CircularBuffer_pushBackByte(&ring, 'x'));
    assert(CircularBuffer_checkAndClearFault(&ring, false));
    assert(CircularBuffer_popFront(&ring, output, 5) == 5);
    assert(memcmp(output, "abcde", 5) == 0);
    assert(CircularBuffer_pushBack(&ring, (const uint8_t*)"hijkl", 5) == 5);
    assert(CircularBuffer_popFront(&ring, output, 8) == 7);
    assert(memcmp(output, "fghijkl", 7) == 0);
    assert(!CircularBuffer_popFrontByte(&ring, output));
    assert(!CircularBuffer_checkAndClearFault(&ring, true));
}

int main(void)
{
    testFraming();
    testBounds();
    testReset();
    testReplies();
    testSends();
    testRingWrap();
    assert(test_barriers != 0);
    puts("UART/parser host regression tests passed");
    return 0;
}
