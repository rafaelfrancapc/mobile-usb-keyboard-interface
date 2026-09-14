#define _POSIX_C_SOURCE 200809L

#include "adb_bridge.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define TAMANHO_MAX_LINHA 256
#define DELAY_RETRY_ADB_S 5
#define DELAY_RETRY_SOCKET_S 2
#define TIMEOUT_CONNECT_S 4
#define TIMEOUT_RECV_S 1 /* baixo de propósito: permite checar *rodando com frequência */

/* Executa um comando externo (ex.: "adb", "connect", "1.2.3.4:5555") e
 * espera terminar. Retorna o código de saída, ou -1 se não conseguiu nem
 * executar (ex.: binário "adb" não encontrado no PATH). stdout/stderr do
 * comando são herdados do processo pai, então acabam no journal do systemd
 * também — útil pra depurar problemas de conexão adb. */
static int rodar_comando(char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "[adb] fork() falhou: %s\n", strerror(errno));
        return -1;
    }
    if (pid == 0) {
        execvp(argv[0], argv);
        fprintf(stderr, "[adb] não foi possível executar \"%s\": %s\n", argv[0], strerror(errno));
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        fprintf(stderr, "[adb] waitpid() falhou: %s\n", strerror(errno));
        return -1;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

static int adb_connect(const adb_bridge_config_t *cfg) {
    char alvo[128];
    snprintf(alvo, sizeof(alvo), "%s:%d", cfg->tablet_ip, cfg->adb_port);

    char *argv[] = {"adb", "connect", alvo, NULL};
    fprintf(stderr, "[adb] adb connect %s\n", alvo);
    int codigo = rodar_comando(argv);
    if (codigo != 0) {
        fprintf(stderr, "[adb] falha ao conectar em %s (o tablet está com adb tcpip ligado "
                        "e alcançável na rede privada?)\n", alvo);
        return 0;
    }
    return 1;
}

static int adb_forward(const adb_bridge_config_t *cfg) {
    char porta[32];
    snprintf(porta, sizeof(porta), "tcp:%d", cfg->app_port);

    char *argv[] = {"adb", "forward", porta, porta, NULL};
    fprintf(stderr, "[adb] adb forward %s %s\n", porta, porta);
    int codigo = rodar_comando(argv);
    if (codigo != 0) {
        fprintf(stderr, "[adb] falha ao configurar o forward da porta %d\n", cfg->app_port);
        return 0;
    }
    return 1;
}

/* Conecta em 127.0.0.1:porta com timeout, sem travar pra sempre se o
 * forward não estiver realmente de pé ainda. Retorna o fd conectado, ou -1. */
static int conectar_local_com_timeout(int porta, int timeout_s) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "[socket] socket() falhou: %s\n", strerror(errno));
        return -1;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in endereco;
    memset(&endereco, 0, sizeof(endereco));
    endereco.sin_family = AF_INET;
    endereco.sin_port = htons((uint16_t)porta);
    inet_pton(AF_INET, "127.0.0.1", &endereco.sin_addr);

    int r = connect(fd, (struct sockaddr *)&endereco, sizeof(endereco));
    if (r == 0) {
        fcntl(fd, F_SETFL, flags); /* volta a ser bloqueante pro resto da conexão */
        return fd;
    }
    if (errno != EINPROGRESS) {
        fprintf(stderr, "[socket] connect() falhou: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    fd_set escrita;
    FD_ZERO(&escrita);
    FD_SET(fd, &escrita);
    struct timeval tv = {.tv_sec = timeout_s, .tv_usec = 0};

    r = select(fd + 1, NULL, &escrita, NULL, &tv);
    if (r <= 0) {
        fprintf(stderr, "[socket] timeout conectando em 127.0.0.1:%d (o app no tablet está "
                        "rodando e escutando?)\n", porta);
        close(fd);
        return -1;
    }

    int erro = 0;
    socklen_t len = sizeof(erro);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &erro, &len);
    if (erro != 0) {
        fprintf(stderr, "[socket] connect() falhou: %s\n", strerror(erro));
        close(fd);
        return -1;
    }

    fcntl(fd, F_SETFL, flags); /* volta a ser bloqueante */
    return fd;
}

/* Espera *rodando ficar 0, ou os segundos indicados se passarem — o que
 * vier primeiro. Usado nos back-offs de retry pra sair rápido no SIGTERM. */
static void dormir_interrompivel(volatile sig_atomic_t *rodando, int segundos) {
    for (int i = 0; i < segundos && *rodando; i++) {
        sleep(1);
    }
}

void adb_bridge_run(const adb_bridge_config_t *cfg,
                     volatile sig_atomic_t *rodando,
                     adb_bridge_line_cb on_line,
                     void *dados_usuario) {
    while (*rodando) {
        if (!adb_connect(cfg)) {
            dormir_interrompivel(rodando, DELAY_RETRY_ADB_S);
            continue;
        }
        if (!adb_forward(cfg)) {
            dormir_interrompivel(rodando, DELAY_RETRY_ADB_S);
            continue;
        }

        int fd = conectar_local_com_timeout(cfg->app_port, TIMEOUT_CONNECT_S);
        if (fd < 0) {
            dormir_interrompivel(rodando, DELAY_RETRY_SOCKET_S);
            continue;
        }

        struct timeval tv_recv = {.tv_sec = TIMEOUT_RECV_S, .tv_usec = 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv_recv, sizeof(tv_recv));

        fprintf(stderr, "[socket] conectado ao app do tablet via túnel adb — pronto pra receber credenciais\n");

        char buffer[TAMANHO_MAX_LINHA];
        size_t usado = 0;

        while (*rodando) {
            char pedaco[128];
            ssize_t n = recv(fd, pedaco, sizeof(pedaco), 0);

            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue; /* só era o timeout pra checar *rodando; segue esperando */
                }
                fprintf(stderr, "[socket] erro lendo do tablet: %s\n", strerror(errno));
                break;
            }
            if (n == 0) {
                fprintf(stderr, "[socket] conexão fechada pelo tablet\n");
                break;
            }

            for (ssize_t i = 0; i < n; i++) {
                char c = pedaco[i];
                if (c == '\n') {
                    buffer[usado] = '\0';
                    if (usado > 0) {
                        on_line(buffer, dados_usuario);
                    }
                    usado = 0;
                } else if (c != '\r' && usado + 1 < sizeof(buffer)) {
                    buffer[usado++] = c;
                } else if (usado + 1 >= sizeof(buffer)) {
                    fprintf(stderr, "[socket] linha recebida do tablet estourou o limite; descartando\n");
                    usado = 0;
                }
            }
        }

        close(fd);
        dormir_interrompivel(rodando, DELAY_RETRY_SOCKET_S);
    }
}
