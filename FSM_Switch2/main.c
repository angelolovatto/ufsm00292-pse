#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>

#define STX 0x02u
#define ETX 0x03u
#define MAX_DATA 255

// ---------------- Utilidades ----------------
static uint8_t calc_checksum(uint8_t qtd, const uint8_t *data) {
    uint16_t sum = qtd;
    for (uint16_t i = 0; i < qtd; ++i) sum += data[i];
    return (uint8_t)(sum & 0xFF);
}

typedef enum {
    RX_IN_PROGRESS = 0,
    RX_OK,
    RX_ERROR
} rx_result_t;

// ------------- FSM via ponteiros de função -------------
struct rx_fsm;
typedef void (*state_fn)(struct rx_fsm *rx, uint8_t byte);

typedef enum {
    EVT_NONE = 0,
    EVT_OK   = 1,
    EVT_ERR  = 2
} rx_event_t;

typedef struct rx_fsm {
    // Estado atual (ponteiro de função)
    state_fn step;

    // Em montagem
    uint8_t qtd;
    uint8_t idx;
    uint8_t data[MAX_DATA];
    uint8_t chk;

    // Buffer de entrega (shadow) — preserva dados do frame válido
    uint8_t last_qtd;
    uint8_t last_data[MAX_DATA];

    // Evento pendente a ser consumido por rx_feed()
    rx_event_t evt;
} rx_fsm_t;

// Estados (protótipos)
static void st_wait_stx(rx_fsm_t *rx, uint8_t b);
static void st_read_qtd(rx_fsm_t *rx, uint8_t b);
static void st_read_data(rx_fsm_t *rx, uint8_t b);
static void st_read_chk (rx_fsm_t *rx, uint8_t b);
static void st_wait_etx(rx_fsm_t *rx, uint8_t b);

// Reset do "miolo" (não apaga evt nem last_*)
static void rx_reset_core(rx_fsm_t *rx) {
    rx->step = st_wait_stx;
    rx->qtd  = 0;
    rx->idx  = 0;
    rx->chk  = 0;
}

// Reset total
static void rx_init(rx_fsm_t *rx) {
    rx->last_qtd = 0;
    rx->evt = EVT_NONE;
    rx_reset_core(rx);
}

// ---------- Implementação dos estados ----------
static void st_wait_stx(rx_fsm_t *rx, uint8_t b) {
    if (b == STX) {
        rx->qtd = 0;
        rx->idx = 0;
        rx->chk = 0;
        rx->step = st_read_qtd;
    }
    // Senão, ignora ruído
}

static void st_read_qtd(rx_fsm_t *rx, uint8_t b) {
    rx->qtd = b;

    // Se precisar forçar QTD >= 1, descomente:
    // if (rx->qtd == 0) { rx->evt = EVT_ERR; rx_reset_core(rx); return; }

    rx->idx = 0;
    rx->step = (rx->qtd > 0) ? st_read_data : st_read_chk;
}

static void st_read_data(rx_fsm_t *rx, uint8_t b) {
    if (rx->idx < rx->qtd) {
        rx->data[rx->idx++] = b;
        if (rx->idx == rx->qtd) {
            rx->step = st_read_chk;
        }
    } else {
        // Recebeu mais dados do que QTD: erro
        rx->evt = EVT_ERR;
        rx_reset_core(rx);
    }
}

static void st_read_chk(rx_fsm_t *rx, uint8_t b) {
    rx->chk = b;
    rx->step = st_wait_etx;
}

static void st_wait_etx(rx_fsm_t *rx, uint8_t b) {
    if (b != ETX) {
        rx->evt = EVT_ERR;
        rx_reset_core(rx);
        return;
    }
    // ETX ok → valida checksum
    uint8_t calc = calc_checksum(rx->qtd, rx->data);
    if (calc == rx->chk) {
        // Prepara entrega antes de resetar o core
        rx->last_qtd = rx->qtd;
        if (rx->last_qtd > 0) memcpy(rx->last_data, rx->data, rx->last_qtd);
        rx->evt = EVT_OK;
        rx_reset_core(rx); // volta a esperar próximo frame
    } else {
        rx->evt = EVT_ERR;
        rx_reset_core(rx);
    }
}

// --------------- Driver por byte ---------------
static rx_result_t rx_feed(rx_fsm_t *rx, uint8_t byte, uint8_t *out_qtd, uint8_t *out_buf) {
    // Processa 1 byte no estado corrente
    rx->step(rx, byte);

    // Consome evento (se houver)
    if (rx->evt == EVT_OK) {
        if (out_qtd) *out_qtd = rx->last_qtd;
        if (out_buf && rx->last_qtd > 0) memcpy(out_buf, rx->last_data, rx->last_qtd);
        rx->evt = EVT_NONE;
        return RX_OK;
    }
    if (rx->evt == EVT_ERR) {
        rx->evt = EVT_NONE;
        return RX_ERROR;
    }
    return RX_IN_PROGRESS;
}

// -----------------------------------------------------
// TDD leve (harness de teste)
// -----------------------------------------------------
static int tests_passed = 0, tests_failed = 0;
static void assert_true(int cond, const char *msg) {
    if (cond) { printf("[PASSARAM] %s\n", msg); ++tests_passed; }
    else      { printf("[FALHARAM] %s\n", msg); ++tests_failed; }
}

// Monta um frame válido em out_frame
static size_t build_frame(const uint8_t *payload, uint8_t n, uint8_t *out) {
    size_t k = 0;
    out[k++] = STX;
    out[k++] = n;
    for (uint8_t i = 0; i < n; ++i) out[k++] = payload[i];
    out[k++] = calc_checksum(n, payload);
    out[k++] = ETX;
    return k;
}

static void feed_bytes(rx_fsm_t *rx, const uint8_t *bytes, size_t n,
                       int *got_ok, int *got_err,
                       uint8_t *dl_qtd, uint8_t *dl_buf) {
    for (size_t i = 0; i < n; ++i) {
        rx_result_t r = rx_feed(rx, bytes[i], dl_qtd, dl_buf);
        if (r == RX_OK)   *got_ok  = 1;
        else if (r == RX_ERROR) *got_err = 1;
    }
}

static void test_nominal(const char *label, const uint8_t *p, uint8_t n) {
    uint8_t frame[512];
    size_t L = build_frame(p, n, frame);

    rx_fsm_t rx; rx_init(&rx);
    int ok = 0, err = 0;
    uint8_t dl_qtd = 0, dl_buf[256] = {0};
    feed_bytes(&rx, frame, L, &ok, &err, &dl_qtd, dl_buf);

    int good = ok && !err && dl_qtd == n && (n == 0 || memcmp(dl_buf, p, n) == 0);
    assert_true(good, label);
}

static void test_bad_checksum(const char *label, const uint8_t *p, uint8_t n) {
    uint8_t frame[512];
    size_t L = build_frame(p, n, frame);
    // Corrompe CHK (penúltimo byte)
    frame[L - 2] ^= 0xFF;

    rx_fsm_t rx; rx_init(&rx);
    int ok = 0, err = 0;
    uint8_t dl_qtd = 0, dl_buf[256] = {0};
    feed_bytes(&rx, frame, L, &ok, &err, &dl_qtd, dl_buf);

    assert_true(err && !ok, label);
}

static void test_bad_etx(const char *label, const uint8_t *p, uint8_t n) {
    uint8_t frame[512];
    size_t L = build_frame(p, n, frame);
    // Corrompe ETX (último byte)
    frame[L - 1] = 0x00;

    rx_fsm_t rx; rx_init(&rx);
    int ok = 0, err = 0;
    uint8_t dl_qtd = 0, dl_buf[256] = {0};
    feed_bytes(&rx, frame, L, &ok, &err, &dl_qtd, dl_buf);

    assert_true(err && !ok, label);
}

static void test_noise_before_stx(void) {
    const uint8_t payload[] = {'U','F','S','M'};
    uint8_t frame[512];
    size_t L = build_frame(payload, (uint8_t)sizeof(payload), frame);

    uint8_t noise[] = {0x00, 0x11, 0x22, 0x33};
    uint8_t stream[600];
    size_t k = 0;
    for (size_t i = 0; i < sizeof(noise); ++i) stream[k++] = noise[i];
    for (size_t i = 0; i < L; ++i) stream[k++] = frame[i];

    rx_fsm_t rx; rx_init(&rx);
    int ok = 0, err = 0;
    uint8_t dl_qtd = 0, dl_buf[256] = {0};
    feed_bytes(&rx, stream, k, &ok, &err, &dl_qtd, dl_buf);

    int good = ok && !err && dl_qtd == sizeof(payload)
               && memcmp(dl_buf, payload, sizeof(payload)) == 0;
    assert_true(good, "Ruido antes do STX eh ignorado e frame eh decodificado");
}

int main(void) {
    // QTD = 0 (sem dados)
    // Evita array de tamanho zero: usa ponteiro nulo com n=0
    const uint8_t *p0 = NULL;
    test_nominal("Frame valido com QTD=0 (sem dados)", p0, 0);

    const uint8_t p1[] = {'U','F','S','M'};
    test_nominal("Frame nominal (\"UFSM\")", p1, (uint8_t)sizeof(p1));

    const uint8_t p2[] = {1,2,3,4,5,6,7,8,9,10};
    test_nominal("Frame nominal (10 bytes)", p2, (uint8_t)sizeof(p2));

    const uint8_t p3[] = {'C','H','E','C','K'};
    test_bad_checksum("Deteccao de checksum invalido", p3, (uint8_t)sizeof(p3));

    const uint8_t p4[] = {'E','T','X'};
    test_bad_etx("Deteccao de ETX invalido", p4, (uint8_t)sizeof(p4));

    test_noise_before_stx();

    printf("\nResumo: %d PASSARAM / %d FALHARAM\n", tests_passed, tests_failed);
    return (tests_failed == 0) ? 0 : 1;
}
