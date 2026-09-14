#ifndef CATRACA_ADB_BRIDGE_H
#define CATRACA_ADB_BRIDGE_H

#include <signal.h>

/*
 * Substitui o rpi_adb_bridge.py original, mas via ADB DE REDE em vez de USB:
 * o app no tablet continua sendo o SERVIDOR (abre um ServerSocket local e
 * espera alguém conectar — isso não muda em nada). O que muda é como a gente
 * chega nesse socket: em vez do tablet estar plugado por cabo, a Pi faz
 *
 *   adb connect <tablet_ip>:<adb_port>
 *   adb forward tcp:<app_port> tcp:<app_port>
 *
 * pela rede privada entre a Pi e o tablet, e a partir daí conectar em
 * 127.0.0.1:<app_port> tem exatamente o mesmo efeito que tinha via USB: o
 * ADB encaminha essa conexão pro app que está escutando naquela porta
 * dentro do tablet.
 */

typedef struct {
    const char *tablet_ip;
    int adb_port;   /* porta do adbd no tablet, normalmente 5555 */
    int app_port;   /* porta que o app escuta no tablet E que a Pi conecta localmente */
} adb_bridge_config_t;

/* Chamado uma vez pra cada linha completa (sem o \n) recebida do tablet. */
typedef void (*adb_bridge_line_cb)(const char *linha, void *dados_usuario);

/* Roda o loop principal (bloqueante): conecta via adb, abre o túnel,
 * conecta no socket local e fica lendo linhas, reconectando sozinho sempre
 * que a conexão cai. Só retorna quando *rodando vira 0 (ex.: SIGTERM). */
void adb_bridge_run(const adb_bridge_config_t *cfg,
                     volatile sig_atomic_t *rodando,
                     adb_bridge_line_cb on_line,
                     void *dados_usuario);

#endif
