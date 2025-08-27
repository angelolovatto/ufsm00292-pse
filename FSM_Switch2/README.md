# Entrega — Protocolo com FSM (switch-case)

Implementa **TX e RX** de um protocolo simples usando **FSM em C com `switch-case`** e **testes leves (TDD)**.
Inclui **diagramas Mermaid** para RX e TX.

## Quadro assumido
```
[STX=0x02][LEN][DATA0..DATA(LEN-1)][CHK][ETX=0x03]
```
- `CHK` = soma 8-bit de `LEN` + `DATA` (mod 256). Ajuste em `calc_checksum()` se necessário.

## Compilar/rodar
```bash
gcc -Wall -Wextra -O2 -std=c99 -o fsm main.c
./fsm
```

Saída esperada (resumo):
```
[PASS] Roundtrip nominal ("UFSM")
[PASS] Roundtrip nominal (10 bytes)
[PASS] Detecção de checksum inválido
[PASS] Detecção de ETX inválido

Resumo: 4 PASS / 0 FAIL
```
