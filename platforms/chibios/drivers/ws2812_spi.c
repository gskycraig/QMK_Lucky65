#include "quantum.h"
#include "ws2812.h"

/* Adapted from https://github.com/gamazeps/ws2812b-chibios-SPIDMA/ */

// Define the spi your LEDs are plugged to here
#ifndef WS2812_SPI
#    define WS2812_SPI SPID1
#endif

#ifndef WS2812_SPI_MOSI_PAL_MODE
#    define WS2812_SPI_MOSI_PAL_MODE 5
#endif

#ifndef WS2812_SPI_SCK_PAL_MODE
#    define WS2812_SPI_SCK_PAL_MODE 5
#endif

#ifndef WS2812_SPI_DIVISOR
#    define WS2812_SPI_DIVISOR 16
#endif

// Push Pull or Open Drain Configuration
// Default Push Pull
#ifndef WS2812_EXTERNAL_PULLUP
#    if defined(USE_GPIOV1)
#        define WS2812_MOSI_OUTPUT_MODE PAL_MODE_ALTERNATE_PUSHPULL
#    else
#        define WS2812_MOSI_OUTPUT_MODE PAL_MODE_ALTERNATE(WS2812_SPI_MOSI_PAL_MODE) | PAL_OUTPUT_TYPE_PUSHPULL
#    endif
#else
#    if defined(USE_GPIOV1)
#        define WS2812_MOSI_OUTPUT_MODE PAL_MODE_ALTERNATE_OPENDRAIN
#    else
#        define WS2812_MOSI_OUTPUT_MODE PAL_MODE_ALTERNATE(WS2812_SPI_MOSI_PAL_MODE) | PAL_OUTPUT_TYPE_OPENDRAIN
#    endif
#endif

// Define SPI config speed
// baudrate should target 3.2MHz
// F072 fpclk = 48MHz
// 48/16 = 3Mhz
#if WS2812_SPI_DIVISOR == 2
#    define WS2812_SPI_DIVISOR_CR1_BR_X (0)
#elif WS2812_SPI_DIVISOR == 4
#    define WS2812_SPI_DIVISOR_CR1_BR_X (SPI_CR1_BR_0)
#elif WS2812_SPI_DIVISOR == 8
#    define WS2812_SPI_DIVISOR_CR1_BR_X (SPI_CR1_BR_1)
#elif WS2812_SPI_DIVISOR == 16 // default
#    define WS2812_SPI_DIVISOR_CR1_BR_X (SPI_CR1_BR_1 | SPI_CR1_BR_0)
#elif WS2812_SPI_DIVISOR == 24
#elif WS2812_SPI_DIVISOR == 32
#    define WS2812_SPI_DIVISOR_CR1_BR_X (SPI_CR1_BR_2)
#elif WS2812_SPI_DIVISOR == 64
#    define WS2812_SPI_DIVISOR_CR1_BR_X (SPI_CR1_BR_2 | SPI_CR1_BR_0)
#elif WS2812_SPI_DIVISOR == 128
#    define WS2812_SPI_DIVISOR_CR1_BR_X (SPI_CR1_BR_2 | SPI_CR1_BR_1)
#elif WS2812_SPI_DIVISOR == 256
#    define WS2812_SPI_DIVISOR_CR1_BR_X (SPI_CR1_BR_2 | SPI_CR1_BR_1 | SPI_CR1_BR_0)
#else
#    error "Configured WS2812_SPI_DIVISOR value is not supported at this time."
#endif

#ifdef CCC_LEVEL
#   if CCC_LEVEL < 0 || CCC_LEVEL > 15
#       error "The defined CCC_LEVEL range is not in 0 to 15."
#   endif
#endif

// Use SPI circular buffer
#ifdef WS2812_SPI_USE_CIRCULAR_BUFFER
#    define WS2812_SPI_BUFFER_MODE 1 // circular buffer
#else
#    define WS2812_SPI_BUFFER_MODE 0 // normal buffer
#endif

#if defined(USE_GPIOV1)
#    define WS2812_SCK_OUTPUT_MODE PAL_MODE_ALTERNATE_PUSHPULL
#else
#    define WS2812_SCK_OUTPUT_MODE PAL_MODE_ALTERNATE(WS2812_SPI_SCK_PAL_MODE) | PAL_OUTPUT_TYPE_PUSHPULL
#endif

#define BYTES_FOR_LED_BYTE 4
#ifdef RGBW
#    define WS2812_CHANNELS 4
#else
#    define WS2812_CHANNELS 3
#endif
#define BYTES_FOR_LED (BYTES_FOR_LED_BYTE * WS2812_CHANNELS)
#define DATA_SIZE (BYTES_FOR_LED * WS2812_LED_COUNT)
#define RESET_SIZE (1000 * WS2812_TRST_US / (2 * WS2812_TIMING))
#define PREAMBLE_SIZE (4)
#ifdef CCC_LEVEL
#   define CCC_SIZE (BYTES_FOR_LED_BYTE * 2)
#else
#   define CCC_SIZE (0)
#endif

static uint8_t txbuf[PREAMBLE_SIZE + DATA_SIZE + RESET_SIZE + CCC_SIZE] = {0};

/*
 * As the trick here is to use the SPI to send a huge pattern of 0 and 1 to
 * the ws2812b protocol, we use this helper function to translate bytes into
 * 0s and 1s for the LED (with the appropriate timing).
 */
static uint8_t get_protocol_eq(uint8_t data, int pos) {
    uint8_t eq = 0;
    if (data & (1 << (2 * (3 - pos))))
        eq = 0b1110;
    else
        eq = 0b1000;
    if (data & (2 << (2 * (3 - pos))))
        eq += 0b11100000;
    else
        eq += 0b10000000;
    return eq;
}

static void set_led_color_rgb(LED_TYPE color, int pos) {
    uint8_t* tx_start = &txbuf[PREAMBLE_SIZE];

#if (WS2812_BYTE_ORDER == WS2812_BYTE_ORDER_GRB)
    for (int j = 0; j < 4; j++)
        tx_start[BYTES_FOR_LED * pos + j] = get_protocol_eq(color.g, j);
    for (int j = 0; j < 4; j++)
        tx_start[BYTES_FOR_LED * pos + BYTES_FOR_LED_BYTE + j] = get_protocol_eq(color.r, j);
    for (int j = 0; j < 4; j++)
        tx_start[BYTES_FOR_LED * pos + BYTES_FOR_LED_BYTE * 2 + j] = get_protocol_eq(color.b, j);
#elif (WS2812_BYTE_ORDER == WS2812_BYTE_ORDER_RGB)
    for (int j = 0; j < 4; j++)
        tx_start[BYTES_FOR_LED * pos + j] = get_protocol_eq(color.r, j);
    for (int j = 0; j < 4; j++)
        tx_start[BYTES_FOR_LED * pos + BYTES_FOR_LED_BYTE + j] = get_protocol_eq(color.g, j);
    for (int j = 0; j < 4; j++)
        tx_start[BYTES_FOR_LED * pos + BYTES_FOR_LED_BYTE * 2 + j] = get_protocol_eq(color.b, j);
#elif (WS2812_BYTE_ORDER == WS2812_BYTE_ORDER_BGR)
    for (int j = 0; j < 4; j++)
        tx_start[BYTES_FOR_LED * pos + j] = get_protocol_eq(color.b, j);
    for (int j = 0; j < 4; j++)
        tx_start[BYTES_FOR_LED * pos + BYTES_FOR_LED_BYTE + j] = get_protocol_eq(color.g, j);
    for (int j = 0; j < 4; j++)
        tx_start[BYTES_FOR_LED * pos + BYTES_FOR_LED_BYTE * 2 + j] = get_protocol_eq(color.r, j);
#endif
#ifdef RGBW
    for (int j = 0; j < 4; j++)
        tx_start[BYTES_FOR_LED * pos + BYTES_FOR_LED_BYTE * 4 + j] = get_protocol_eq(color.w, j);
#endif
}

#ifdef CCC_LEVEL
static void set_led_ccc_level(uint8_t ccc_level, uint16_t leds) {
    uint8_t* tx_start = &txbuf[PREAMBLE_SIZE];

    for (int j = 0; j < 4; j++)
        tx_start[BYTES_FOR_LED * leds + j] = get_protocol_eq(((ccc_level << 4) | ccc_level), j);
    for (int j = 0; j < 4; j++)
        tx_start[BYTES_FOR_LED * leds + BYTES_FOR_LED_BYTE + j] = get_protocol_eq((ccc_level << 4) | 2, j);
}
#endif

void ws2812_init(void) {
    palSetLineMode(RGB_DI_PIN, WS2812_MOSI_OUTPUT_MODE);

#ifdef WS2812_SPI_SCK_PIN
    palSetLineMode(WS2812_SPI_SCK_PIN, WS2812_SCK_OUTPUT_MODE);
#endif // WS2812_SPI_SCK_PIN

    // TODO: more dynamic baudrate
    static const SPIConfig spicfg = {
#ifndef HAL_LLD_SELECT_SPI_V2
// HAL_SPI_V1
#    if SPI_SUPPORTS_CIRCULAR == TRUE
        WS2812_SPI_BUFFER_MODE,
#    endif
        NULL, // end_cb
        PAL_PORT(RGB_DI_PIN),
        PAL_PAD(RGB_DI_PIN),
#    if defined(WB32F3G71xx) || defined(WB32FQ95xx)
        0,
        0,
        WS2812_SPI_DIVISOR
#    else
        WS2812_SPI_DIVISOR_CR1_BR_X,
        0
#    endif
#else
    // HAL_SPI_V2
#    if SPI_SUPPORTS_CIRCULAR == TRUE
        WS2812_SPI_BUFFER_MODE,
#    endif
#    if SPI_SUPPORTS_SLAVE_MODE == TRUE
        false,
#    endif
        NULL, // data_cb
        NULL, // error_cb
        PAL_PORT(RGB_DI_PIN),
        PAL_PAD(RGB_DI_PIN),
        WS2812_SPI_DIVISOR_CR1_BR_X,
        0
#endif
    };

    spiAcquireBus(&WS2812_SPI);     /* Acquire ownership of the bus.    */
    spiStart(&WS2812_SPI, &spicfg); /* Setup transfer parameters.       */
    spiSelect(&WS2812_SPI);         /* Slave Select assertion.          */
#ifdef WS2812_SPI_USE_CIRCULAR_BUFFER
    spiStartSend(&WS2812_SPI, ARRAY_SIZE(txbuf), txbuf);
#endif
}

void ws2812_setleds(LED_TYPE* ledarray, uint16_t leds) {
    static bool s_init = false;
    if (!s_init) {
        ws2812_init();
        s_init = true;
    }

    for (uint8_t i = 0; i < leds; i++) {
        set_led_color_rgb(ledarray[i], i);
    }
#ifdef CCC_LEVEL
    set_led_ccc_level(CCC_LEVEL, leds);
#endif
    // Send async - each led takes ~0.03ms, 50 leds ~1.5ms, animations flushing faster than send will cause issues.
    // Instead spiSend can be used to send synchronously (or the thread logic can be added back).
#ifndef WS2812_SPI_USE_CIRCULAR_BUFFER
#    ifdef WS2812_SPI_SYNC
    spiSend(&WS2812_SPI, ARRAY_SIZE(txbuf), txbuf);
#    else
    spiStartSend(&WS2812_SPI, ARRAY_SIZE(txbuf), txbuf);
#    endif
#endif
}
