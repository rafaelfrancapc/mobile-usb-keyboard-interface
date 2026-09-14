#define _POSIX_C_SOURCE 200809L

#include "uinput_backend.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define INTERVALO_ENTRE_TECLAS_US 12000  /* 12ms, igual à versão Rust */
#define DELAY_APOS_CRIAR_DEVICE_US 500000 /* 500ms: tempo pro udev registrar o device */

struct uinput_backend {
    int use_log;  /* 1 = modo log (não digita nada de verdade) */
    int fd;       /* fd de /dev/uinput quando use_log == 0 */
};

static void dormir_us(long microssegundos) {
    struct timespec ts;
    ts.tv_sec = microssegundos / 1000000;
    ts.tv_nsec = (microssegundos % 1000000) * 1000L;
    nanosleep(&ts, NULL);
}

/* Mapeia um dígito ASCII pro keycode do Linux. Retorna -1 se não for dígito.
 * Os KEY_0..KEY_9 do kernel NÃO são sequenciais (KEY_1=2 ... KEY_9=10,
 * KEY_0=11), por isso a tabela explícita em vez de aritmética de char. */
static int keycode_para_digito(char c) {
    switch (c) {
        case '0': return KEY_0;
        case '1': return KEY_1;
        case '2': return KEY_2;
        case '3': return KEY_3;
        case '4': return KEY_4;
        case '5': return KEY_5;
        case '6': return KEY_6;
        case '7': return KEY_7;
        case '8': return KEY_8;
        case '9': return KEY_9;
        default: return -1;
    }
}

static int emitir_evento(int fd, unsigned short tipo, unsigned short codigo, int valor) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = tipo;
    ev.code = codigo;
    ev.value = valor;
    /* ev.time fica zerado; o kernel preenche o timestamp real. */
    if (write(fd, &ev, sizeof(ev)) != (ssize_t)sizeof(ev)) {
        fprintf(stderr, "[uinput] falha ao escrever evento (tipo=%u codigo=%u valor=%d): %s\n",
                tipo, codigo, valor, strerror(errno));
        return -1;
    }
    return 0;
}

static int clicar_tecla(int fd, unsigned short keycode) {
    if (emitir_evento(fd, EV_KEY, keycode, 1) != 0) return -1;          /* pressiona */
    if (emitir_evento(fd, EV_SYN, SYN_REPORT, 0) != 0) return -1;
    dormir_us(1000); /* segura por 1ms — leitores/softwares às vezes perdem cliques secos demais */
    if (emitir_evento(fd, EV_KEY, keycode, 0) != 0) return -1;          /* solta */
    if (emitir_evento(fd, EV_SYN, SYN_REPORT, 0) != 0) return -1;
    return 0;
}

/* Tenta abrir e criar o teclado virtual de verdade.
 * Retorna o fd em sucesso, ou -1 em erro (já loga o motivo). */
static int criar_device_uinput(void) {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "[uinput] não foi possível abrir /dev/uinput (confira permissão): %s\n",
                strerror(errno));
        return -1;
    }

    if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0) {
        fprintf(stderr, "[uinput] UI_SET_EVBIT falhou: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    static const int teclas[] = {
        KEY_0, KEY_1, KEY_2, KEY_3, KEY_4,
        KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
        KEY_ENTER,
    };
    for (size_t i = 0; i < sizeof(teclas) / sizeof(teclas[0]); i++) {
        if (ioctl(fd, UI_SET_KEYBIT, teclas[i]) < 0) {
            fprintf(stderr, "[uinput] UI_SET_KEYBIT(%d) falhou: %s\n", teclas[i], strerror(errno));
            close(fd);
            return -1;
        }
    }

    struct uinput_setup usetup;
    memset(&usetup, 0, sizeof(usetup));
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor = 0x1d6b;  /* vendor id genérico (Linux Foundation), só precisa existir */
    usetup.id.product = 0x0104;
    strncpy(usetup.name, "catraca-teclado-virtual", sizeof(usetup.name) - 1);

    if (ioctl(fd, UI_DEV_SETUP, &usetup) < 0) {
        fprintf(stderr, "[uinput] UI_DEV_SETUP falhou: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    if (ioctl(fd, UI_DEV_CREATE) < 0) {
        fprintf(stderr, "[uinput] UI_DEV_CREATE falhou: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    /* O kernel/udev leva um instante pra terminar de registrar o dispositivo
     * virtual; sem essa pausa o primeiro clique pode se perder silenciosamente
     * logo após o processo iniciar (mesmo comentário da versão Rust). */
    dormir_us(DELAY_APOS_CRIAR_DEVICE_US);

    fprintf(stderr, "[uinput] backend de entrada: uinput (teclado virtual de kernel)\n");
    return fd;
}

uinput_backend_t *uinput_backend_create(void) {
    uinput_backend_t *backend = calloc(1, sizeof(*backend));
    if (backend == NULL) {
        fprintf(stderr, "[uinput] sem memória — isso não deveria acontecer\n");
        exit(1);
    }

    const char *modo = getenv("CATRACA_INPUT_MODE");
    if (modo == NULL || modo[0] == '\0') {
        modo = "uinput";
    }

    if (strcmp(modo, "log") == 0) {
        fprintf(stderr, "[uinput] CATRACA_INPUT_MODE=log: nenhuma tecla real será enviada\n");
        backend->use_log = 1;
        backend->fd = -1;
        return backend;
    }

    if (strcmp(modo, "uinput") != 0) {
        fprintf(stderr, "[uinput] CATRACA_INPUT_MODE desconhecido (\"%s\"), usando modo log\n", modo);
        backend->use_log = 1;
        backend->fd = -1;
        return backend;
    }

    int fd = criar_device_uinput();
    if (fd < 0) {
        fprintf(stderr, "[uinput] caindo para modo log\n");
        backend->use_log = 1;
        backend->fd = -1;
    } else {
        backend->use_log = 0;
        backend->fd = fd;
    }
    return backend;
}

int uinput_backend_type_and_enter(uinput_backend_t *backend, const char *digits) {
    if (backend->use_log) {
        fprintf(stderr, "[uinput] [SIMULAÇÃO] digitaria \"%s\" + Enter\n", digits);
        return 0;
    }

    for (const char *p = digits; *p != '\0'; p++) {
        int keycode = keycode_para_digito(*p);
        if (keycode < 0) {
            fprintf(stderr, "[uinput] dígito inválido no buffer: '%c'\n", *p);
            return -1;
        }
        if (clicar_tecla(backend->fd, (unsigned short)keycode) != 0) {
            return -1;
        }
        /* pequeno intervalo entre teclas: alguns leitores/softwares de
         * controle de acesso perdem eventos se chegarem rápido demais. */
        dormir_us(INTERVALO_ENTRE_TECLAS_US);
    }

    if (clicar_tecla(backend->fd, KEY_ENTER) != 0) {
        return -1;
    }

    return 0;
}

void uinput_backend_destroy(uinput_backend_t *backend) {
    if (backend == NULL) return;
    if (!backend->use_log && backend->fd >= 0) {
        ioctl(backend->fd, UI_DEV_DESTROY);
        close(backend->fd);
    }
    free(backend);
}
