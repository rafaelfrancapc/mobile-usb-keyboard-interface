#ifndef CATRACA_UINPUT_BACKEND_H
#define CATRACA_UINPUT_BACKEND_H

/*
 * Abstrai "digitar a credencial + Enter" na Raspberry Pi, exatamente como o
 * módulo input.rs fazia na versão Rust: cria um teclado USB virtual de
 * verdade a nível de kernel (/dev/uinput), então o que estiver plugado na
 * porta USB da Pi (a máquina do controle de acesso) enxerga isso como um
 * teclado/leitor de código de barras físico de verdade.
 *
 * Modo escolhido pela variável de ambiente CATRACA_INPUT_MODE:
 *   - "uinput" (padrão): usa o /dev/uinput de verdade.
 *   - "log": não digita nada, só imprime o que digitaria (útil pra testar
 *     sem precisar de permissão em /dev/uinput).
 *
 * Se o modo "uinput" for pedido mas a criação do device falhar (permissão,
 * módulo do kernel não carregado, etc.), cai para o modo log automaticamente
 * — igual ao fallback que a versão Rust já fazia.
 */

typedef struct uinput_backend uinput_backend_t;

/* Lê CATRACA_INPUT_MODE do ambiente e cria o backend correspondente.
 * Nunca retorna NULL: na pior das hipóteses, cai para o modo log. */
uinput_backend_t *uinput_backend_create(void);

/* Digita cada dígito de `digits` em sequência e finaliza com Enter.
 * Retorna 0 em sucesso, -1 em erro (mensagem já impressa em stderr). */
int uinput_backend_type_and_enter(uinput_backend_t *backend, const char *digits);

void uinput_backend_destroy(uinput_backend_t *backend);

#endif
