#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adb_bridge.h"
#include "uinput_backend.h"

#define TAMANHO_MAX_CREDENCIAL 11

static volatile sig_atomic_t g_rodando = 1;

static void ao_receber_sinal(int sinal) {
    (void)sinal;
    g_rodando = 0;
}

static int credencial_valida(const char *linha) {
    size_t len = strlen(linha);
    if (len == 0 || len > TAMANHO_MAX_CREDENCIAL) {
        return 0;
    }
    for (size_t i = 0; i < len; i++) {
        if (!isdigit((unsigned char)linha[i])) {
            return 0;
        }
    }
    return 1;
}

typedef struct {
    uinput_backend_t *backend;
} contexto_t;

static void ao_receber_linha(const char *linha, void *dados_usuario) {
    contexto_t *ctx = (contexto_t *)dados_usuario;

    if (!credencial_valida(linha)) {
        fprintf(stderr, "[catraca] credencial inválida recebida do tablet: \"%s\"\n", linha);
        return;
    }

    fprintf(stderr, "[catraca] credencial recebida: %s\n", linha);
    if (uinput_backend_type_and_enter(ctx->backend, linha) != 0) {
        fprintf(stderr, "[catraca] falha ao reproduzir a credencial no teclado virtual\n");
    }
}

static int getenv_int(const char *nome, int padrao) {
    const char *valor = getenv(nome);
    if (valor == NULL || valor[0] == '\0') {
        return padrao;
    }
    char *fim = NULL;
    long n = strtol(valor, &fim, 10);
    if (fim == valor || n <= 0 || n > 65535) {
        fprintf(stderr, "[catraca] valor inválido em %s=\"%s\", usando %d\n", nome, valor, padrao);
        return padrao;
    }
    return (int)n;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    signal(SIGTERM, ao_receber_sinal);
    signal(SIGINT, ao_receber_sinal);
    signal(SIGPIPE, SIG_IGN); /* write() num socket que caiu não deve matar o processo */

    const char *tablet_ip = getenv("CATRACA_TABLET_IP");
    if (tablet_ip == NULL || tablet_ip[0] == '\0') {
        fprintf(stderr,
                "[catraca] CATRACA_TABLET_IP não definido — configure o IP do tablet na rede "
                "privada (ex.: em /etc/catraca-bridge.conf)\n");
        return 1;
    }

    adb_bridge_config_t cfg = {
        .tablet_ip = tablet_ip,
        .adb_port = getenv_int("CATRACA_ADB_PORT", 5555),
        .app_port = getenv_int("CATRACA_APP_PORT", 8765),
    };

    fprintf(stderr, "=== catraca-bridge ===\n");
    fprintf(stderr, "[catraca] tablet: %s (adb porta %d, app porta %d)\n",
            cfg.tablet_ip, cfg.adb_port, cfg.app_port);

    contexto_t ctx = {
        .backend = uinput_backend_create(),
    };

    adb_bridge_run(&cfg, &g_rodando, ao_receber_linha, &ctx);

    fprintf(stderr, "[catraca] encerrando (sinal recebido)\n");
    uinput_backend_destroy(ctx.backend);
    return 0;
}
