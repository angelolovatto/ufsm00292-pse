#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>

/* ===================== Protocolo ===================== */
#define PROTO_STX        0x02
#define PROTO_ETX        0x03
#define PROTO_TAM_MAX    200  /* limite para testar LEN inválido */

/* Calcula CHK = STX ^ QTD ^ XOR(dados[0..tam-1]) */
static uint8_t proto_chk_xor(uint8_t stx, uint8_t tam, const uint8_t *dados) {
    uint8_t c = (uint8_t)(stx ^ tam);
    for (uint32_t i = 0; i < tam; ++i) c ^= dados[i];
    return c;
}

/* ===================== Transmissor (TX) ===================== */
typedef enum {
    TX_AGUARDA = 0,
    TX_ENVIA_STX,
    TX_ENVIA_TAM,
    TX_ENVIA_DADOS,
    TX_ENVIA_CHK,
    TX_ENVIA_ETX,
    TX_FIM
} estado_tx_t;

typedef struct {
    estado_tx_t st;
    const uint8_t *dados;
    uint8_t tam;
    uint8_t idx;
    uint8_t chk;  /* XOR acumulado: começa em STX ^ QTD e acumula DADOS */
} tx_t;

static void tx_iniciar(tx_t *tx) {
    tx->st = TX_AGUARDA;
    tx->dados = NULL;
    tx->tam = 0;
    tx->idx = 0;
    tx->chk = 0;
}

static void tx_solicitar(tx_t *tx, const uint8_t *dados, uint8_t tam) {
    tx->dados = dados;
    tx->tam = tam;
    tx->idx = 0;
    tx->chk = (uint8_t)(PROTO_STX ^ tam); /* inclui STX e QTD no XOR */
    tx->st = TX_ENVIA_STX;
}

/* Emite um passo; se hw_pronto!=0 e saiu byte, retorna 1 e escreve em *out. */
static int tx_passo(tx_t *tx, uint8_t *out, int hw_pronto) {
    if (!hw_pronto) return 0;
    switch (tx->st) {
        case TX_AGUARDA:
            return 0;

        case TX_ENVIA_STX:
            *out = PROTO_STX;
            tx->st = TX_ENVIA_TAM;
            return 1;

        case TX_ENVIA_TAM:
            *out = tx->tam;
            tx->st = (tx->tam == 0) ? TX_ENVIA_CHK : TX_ENVIA_DADOS;
            return 1;

        case TX_ENVIA_DADOS:
            if (tx->idx < tx->tam) {
                uint8_t b = tx->dados[tx->idx++];
                *out = b;
                tx->chk ^= b;
                if (tx->idx >= tx->tam) tx->st = TX_ENVIA_CHK;
                return 1;
            } else {
                tx->st = TX_ENVIA_CHK;
                return 0;
            }

        case TX_ENVIA_CHK:
            *out = tx->chk;
            tx->st = TX_ENVIA_ETX;
            return 1;

        case TX_ENVIA_ETX:
            *out = PROTO_ETX;
            tx->st = TX_FIM;
            return 1;

        case TX_FIM:
            tx->st = TX_AGUARDA;
            return 0;
    }
    return 0;
}

/* Helper p/ testes: emite o quadro completo para um buffer. */
static size_t tx_emitir_tudo(tx_t *tx, uint8_t *out_buf, size_t cap) {
    size_t n = 0;
    uint8_t b;
    for (int guard = 0; guard < 4096; ++guard) {
        if (tx_passo(tx, &b, 1)) {
            if (n < cap) out_buf[n++] = b;
        } else if (tx->st == TX_AGUARDA) {
            break;
        }
    }
    return n;
}

/* ===================== Receptor (RX) ===================== */
typedef enum {
    RX_AGUARDA_STX = 0,  /* S0 */
    RX_LE_TAM,          /* S1 */
    RX_LE_DADOS,        /* S2 */
    RX_LE_CHK,          /* S3 */
    RX_LE_ETX,          /* S4 */
    RX_OK,              /* S5 */
    RX_ERRO
} estado_rx_t;

typedef struct {
    estado_rx_t st;
    uint8_t dados[PROTO_TAM_MAX];
    uint8_t tam;
    uint8_t idx;
    uint8_t chk_xor;     /* começa em STX ^ QTD e acumula DADOS */
    uint8_t quadro_pronto;
} rx_t;

static void rx_iniciar(rx_t *rx) {
    rx->st = RX_AGUARDA_STX;
    rx->tam = 0;
    rx->idx = 0;
    rx->chk_xor = 0;
    rx->quadro_pronto = 0;
}

/* Alimenta UM byte na FSM do RX */
static void rx_entrada(rx_t *rx, uint8_t byte) {
    switch (rx->st) {
        case RX_AGUARDA_STX:
            if (byte == PROTO_STX) {
                rx->st = RX_LE_TAM;
            }
            break;

        case RX_LE_TAM:
            rx->tam = byte;
            rx->idx = 0;
            if (rx->tam > PROTO_TAM_MAX) { rx->st = RX_ERRO; break; }
            rx->chk_xor = (uint8_t)(PROTO_STX ^ rx->tam);
            rx->st = (rx->tam == 0) ? RX_LE_CHK : RX_LE_DADOS;
            break;

        case RX_LE_DADOS:
            if (rx->idx < rx->tam) {
                rx->dados[rx->idx++] = byte;
                rx->chk_xor ^= byte;
                if (rx->idx >= rx->tam) rx->st = RX_LE_CHK;
            } else {
                rx->st = RX_ERRO; /* overflow de segurança */
            }
            break;

        case RX_LE_CHK:
            if (byte == rx->chk_xor) {
                rx->st = RX_LE_ETX;
            } else {
                rx->st = RX_ERRO;
            }
            break;

        case RX_LE_ETX:
            if (byte == PROTO_ETX) {
                rx->st = RX_OK;
                rx->quadro_pronto = 1;
            } else {
                rx->st = RX_ERRO;
            }
            break;

        case RX_OK:
            /* aguarda coleta */
            break;

        case RX_ERRO:
            if (byte == PROTO_STX) rx->st = RX_LE_TAM;
            break;
    }
}

/* Se houver quadro pronto, copia payload em 'out' e retorna tam; senão 0 */
static size_t rx_pegar_dados(rx_t *rx, uint8_t *out, size_t cap) {
    if (!rx->quadro_pronto) return 0;
    if (cap < rx->tam) return 0;
    for (uint8_t i = 0; i < rx->tam; ++i) out[i] = rx->dados[i];
    size_t L = rx->tam;
    rx_iniciar(rx);
    return L;
}

/* ===================== TDD (testes) ===================== */
static int falhas = 0;
#define ASSERT_EQ_U8(g,e,m) do{ if(((uint8_t)(g))!=((uint8_t)(e))){ \
    printf("FALHA: %s (got=0x%02X exp=0x%02X)\n", m,(unsigned)(uint8_t)(g),(unsigned)(uint8_t)(e)); falhas++; }}while(0)
#define ASSERT_EQ_SZ(g,e,m) do{ if(((size_t)(g))!=((size_t)(e))){ \
    printf("FALHA: %s (got=%zu exp=%zu)\n", m,(size_t)(g),(size_t)(e)); falhas++; }}while(0)
#define ASSERT_TRUE(c,m) do{ if(!(c)){ printf("FALHA: %s\n", m); falhas++; }}while(0)

static void dump(const char *tit, const uint8_t *p, size_t n){
    printf("%s:", tit);
    for(size_t i=0;i<n;i++) printf(" %02X", p[i]);
    printf("\n");
}

static void teste_tx_quadro_completo(void){
    printf("== teste_tx_quadro_completo ==\n");
    const uint8_t dados[] = {0xA1, 0xB2};
    uint8_t quadro[64];
    tx_t tx; tx_iniciar(&tx);
    tx_solicitar(&tx, dados, (uint8_t)sizeof(dados));
    size_t n = tx_emitir_tudo(&tx, quadro, sizeof(quadro));
    dump("quadro", quadro, n);

    uint8_t chk = proto_chk_xor(PROTO_STX, (uint8_t)sizeof(dados), dados);
    ASSERT_EQ_SZ(n, (size_t)(1 + 1 + sizeof(dados) + 1 + 1), "tamanho do quadro");
    ASSERT_EQ_U8(quadro[0], PROTO_STX, "STX");
    ASSERT_EQ_U8(quadro[1], (uint8_t)sizeof(dados), "QTD");
    ASSERT_EQ_U8(quadro[2], dados[0], "DADOS[0]");
    ASSERT_EQ_U8(quadro[3], dados[1], "DADOS[1]");
    ASSERT_EQ_U8(quadro[4], chk, "CHK");
    ASSERT_EQ_U8(quadro[5], PROTO_ETX, "ETX");
    printf("OK\n");
}

static void teste_rx_valido(void){
    printf("== teste_rx_valido ==\n");
    const uint8_t dados[] = {0x11, 0x22, 0x33};
    uint8_t chk = proto_chk_xor(PROTO_STX, (uint8_t)sizeof(dados), dados);
    uint8_t quadro[] = { PROTO_STX, (uint8_t)sizeof(dados), 0x11, 0x22, 0x33, chk, PROTO_ETX };

    rx_t rx; rx_iniciar(&rx);
    for (size_t i=0;i<sizeof(quadro);++i) rx_entrada(&rx, quadro[i]);
    ASSERT_TRUE(rx.quadro_pronto == 1, "quadro_pronto deve ser 1");
    uint8_t out[8]; size_t L = rx_pegar_dados(&rx, out, sizeof(out));
    ASSERT_EQ_SZ(L, sizeof(dados), "tamanho payload");
    ASSERT_TRUE(memcmp(out, dados, L) == 0, "conteúdo payload");
    printf("OK\n");
}

static void teste_rx_chk_incorreto(void){
    printf("== teste_rx_chk_incorreto ==\n");
    const uint8_t dados[] = {0x01, 0x02};
    uint8_t chk = proto_chk_xor(PROTO_STX, (uint8_t)sizeof(dados), dados);
    uint8_t quadro[] = { PROTO_STX, 2, 0x01, 0x02, (uint8_t)(chk ^ 0xFF), PROTO_ETX }; /* CHK ruim */

    rx_t rx; rx_iniciar(&rx);
    for (size_t i=0;i<sizeof(quadro);++i) rx_entrada(&rx, quadro[i]);
    ASSERT_TRUE(rx.quadro_pronto == 0, "não deve sinalizar pronto");
    ASSERT_TRUE(rx.st == RX_ERRO, "estado final deve ser ERRO");
    printf("OK\n");
}

static void teste_rx_etx_incorreto(void){
    printf("== teste_rx_etx_incorreto ==\n");
    const uint8_t dados[] = {0x77};
    uint8_t chk = proto_chk_xor(PROTO_STX, 1, dados);
    uint8_t quadro[] = { PROTO_STX, 1, 0x77, chk, 0x99 }; /* ETX errado */

    rx_t rx; rx_iniciar(&rx);
    for (size_t i=0;i<sizeof(quadro);++i) rx_entrada(&rx, quadro[i]);
    ASSERT_TRUE(rx.quadro_pronto == 0, "não deve sinalizar pronto");
    ASSERT_TRUE(rx.st == RX_ERRO, "estado final deve ser ERRO (ETX errado)");
    printf("OK\n");
}

static void teste_rx_tam_invalido(void){
    printf("== teste_rx_tam_invalido ==\n");
    uint8_t quadro[] = { PROTO_STX, (uint8_t)(PROTO_TAM_MAX+1), 0x00 }; /* len > max */
    rx_t rx; rx_iniciar(&rx);
    for (size_t i=0;i<sizeof(quadro);++i) rx_entrada(&rx, quadro[i]);
    ASSERT_TRUE(rx.st == RX_ERRO, "len maior que o máximo deve cair em ERRO");
    printf("OK\n");
}

static void teste_loopback_tx_rx(void){
    printf("== teste_loopback_tx_rx ==\n");
    const uint8_t dados[] = {0x41, 0x42, 0x43}; /* 'A','B','C' */
    tx_t tx; tx_iniciar(&tx);
    tx_solicitar(&tx, dados, (uint8_t)sizeof(dados));
    uint8_t buf[64]; size_t n = tx_emitir_tudo(&tx, buf, sizeof(buf));
    dump("quadro gerado", buf, n);

    rx_t rx; rx_iniciar(&rx);
    for (size_t i=0;i<n; ++i) rx_entrada(&rx, buf[i]);
    ASSERT_TRUE(rx.quadro_pronto == 1, "loopback deve resultar em quadro pronto");
    uint8_t out[8]; size_t L = rx_pegar_dados(&rx, out, sizeof(out));
    ASSERT_EQ_SZ(L, sizeof(dados), "tamanho no loopback");
    ASSERT_TRUE(memcmp(out, dados, L) == 0, "conteúdo no loopback");
    printf("OK\n");
}

int main(void){
    teste_tx_quadro_completo();
    teste_rx_valido();
    teste_rx_chk_incorreto();
    teste_rx_etx_incorreto();
    teste_rx_tam_invalido();
    teste_loopback_tx_rx();

    if (falhas == 0) {
        printf("\nTODOS OS TESTES PASSARAM\n");
        return 0;
    } else {
        printf("\nTESTES FALHARAM: %d\n", falhas);
        return 1;
    }
}
