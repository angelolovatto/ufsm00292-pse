#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define STX 0x02u
#define ETX 0x03u
#define MAX_DATA 255

// ---------------------- Checksum ----------------------
static uint8_t calc_checksum(uint8_t len, const uint8_t *data) {
    uint16_t sum = len;
    for (uint8_t i = 0; i < len; ++i) sum += data[i];
    return (uint8_t)(sum & 0xFF);
}

// ---------------------- RX FSM ------------------------
typedef enum {
    RX_WAIT_STX = 0,
    RX_READ_LEN,
    RX_READ_DATA,
    RX_READ_CHK,
    RX_WAIT_ETX
} rx_state_t;

typedef enum {
    RX_IN_PROGRESS = 0,
    RX_OK,
    RX_ERROR
} rx_result_t;

typedef struct {
    rx_state_t state;
    uint8_t len;
    uint8_t idx;
    uint8_t data[MAX_DATA];
    uint8_t chk;
} rx_fsm_t;

static void rx_init(rx_fsm_t *rx) {
    rx->state = RX_WAIT_STX;
    rx->len = 0;
    rx->idx = 0;
    rx->chk = 0;
}

static rx_result_t rx_feed(rx_fsm_t *rx, uint8_t byte, uint8_t *out_len, uint8_t *out_buf) {
    switch (rx->state) {
        case RX_WAIT_STX:
            if (byte == STX) {
                rx->state = RX_READ_LEN;
                rx->len = 0;
                rx->idx = 0;
                rx->chk = 0;
            }
            return RX_IN_PROGRESS;

        case RX_READ_LEN:
            rx->len = byte;
            if (rx->len == 0) { // rejeita payload vazio (ajuste se desejar permitir)
                rx_init(rx);
                return RX_ERROR;
            }
            rx->state = RX_READ_DATA;
            return RX_IN_PROGRESS;

        case RX_READ_DATA:
            rx->data[rx->idx++] = byte;
            if (rx->idx == rx->len) {
                rx->state = RX_READ_CHK;
            }
            return RX_IN_PROGRESS;

        case RX_READ_CHK:
            rx->chk = byte;
            rx->state = RX_WAIT_ETX;
            return RX_IN_PROGRESS;

        case RX_WAIT_ETX:
            if (byte != ETX) {
                rx_init(rx);
                return RX_ERROR;
            } else {
                uint8_t calc = calc_checksum(rx->len, rx->data);
                if (calc == rx->chk) {
                    if (out_len) *out_len = rx->len;
                    if (out_buf) memcpy(out_buf, rx->data, rx->len);
                    rx_init(rx);
                    return RX_OK;
                } else {
                    rx_init(rx);
                    return RX_ERROR;
                }
            }
    }
    rx_init(rx);
    return RX_ERROR;
}

// ---------------------- TX FSM ------------------------
typedef enum {
    TX_IDLE = 0,
    TX_SEND_STX,
    TX_SEND_LEN,
    TX_SEND_DATA,
    TX_SEND_CHK,
    TX_SEND_ETX,
    TX_DONE
} tx_state_t;

typedef struct {
    tx_state_t state;
    const uint8_t *data;
    uint8_t len;
    uint8_t idx;
    uint8_t chk;
} tx_fsm_t;

static void tx_start(tx_fsm_t *tx, const uint8_t *data, uint8_t len) {
    tx->data = data;
    tx->len = len;
    tx->idx = 0;
    tx->chk = calc_checksum(len, data);
    tx->state = TX_SEND_STX;
}

static int tx_next(tx_fsm_t *tx, uint8_t *out_byte) {
    switch (tx->state) {
        case TX_IDLE:
            return 0;
        case TX_SEND_STX:
            *out_byte = STX;
            tx->state = TX_SEND_LEN;
            return 1;
        case TX_SEND_LEN:
            *out_byte = tx->len;
            tx->state = (tx->len > 0) ? TX_SEND_DATA : TX_SEND_CHK;
            return 1;
        case TX_SEND_DATA:
            *out_byte = tx->data[tx->idx++];
            if (tx->idx == tx->len) tx->state = TX_SEND_CHK;
            return 1;
        case TX_SEND_CHK:
            *out_byte = tx->chk;
            tx->state = TX_SEND_ETX;
            return 1;
        case TX_SEND_ETX:
            *out_byte = ETX;
            tx->state = TX_DONE;
            return 1;
        case TX_DONE:
            return 0;
    }
    return 0;
}

// ---------------------- Testes (TDD leve) ------------------------
static int tests_passed = 0;
static int tests_failed = 0;

static void assert_true(int cond, const char *msg) {
    if (cond) { printf("[PASS] %s\n", msg); ++tests_passed; }
    else      { printf("[FAIL] %s\n", msg); ++tests_failed; }
}

static void roundtrip_test(const char *label, const uint8_t *payload, uint8_t len, int corrupt_mode) {
    tx_fsm_t tx = {.state = TX_IDLE};
    rx_fsm_t rx; rx_init(&rx);

    uint8_t delivered[256] = {0};
    uint8_t delivered_len = 0;
    int got_ok = 0, got_err = 0;

    tx_start(&tx, payload, len);

    uint8_t byte;
    while (tx_next(&tx, &byte)) {
        // Corrupção controlada
        if (corrupt_mode == 1 && tx.state == TX_SEND_CHK) {
            byte ^= 0xFF; // corrompe checksum
        } else if (corrupt_mode == 2 && tx.state == TX_SEND_ETX) {
            byte = 0x00; // ETX inválido
        }
        rx_result_t r = rx_feed(&rx, byte, &delivered_len, delivered);
        if (r == RX_OK) got_ok = 1;
        if (r == RX_ERROR) got_err = 1;
    }

    if (corrupt_mode == 0) {
        int ok = got_ok && !got_err && (delivered_len == len) && (memcmp(delivered, payload, len) == 0);
        assert_true(ok, label);
    } else {
        assert_true(got_err && !got_ok, label);
    }
}

int main(void) {
    const uint8_t p1[] = {'U','F','S','M'};
    roundtrip_test("Roundtrip nominal (\"UFSM\")", p1, sizeof(p1), 0);

    const uint8_t p2[] = {1,2,3,4,5,6,7,8,9,10};
    roundtrip_test("Roundtrip nominal (10 bytes)", p2, sizeof(p2), 0);

    const uint8_t p3[] = {'C','H','E','C','K'};
    roundtrip_test("Deteccao de checksum invalido", p3, sizeof(p3), 1);

    const uint8_t p4[] = {'E','T','X'};
    roundtrip_test("Deteccao de ETX invalido", p4, sizeof(p4), 2);

    printf("\nResumo: %d passaram / %d falharam\n", tests_passed, tests_failed);
    return (tests_failed == 0) ? 0 : 1;
}
