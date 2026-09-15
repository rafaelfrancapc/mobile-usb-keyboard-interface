#!/usr/bin/env python3
"""
catraca-bridge (versão Python / pyautogui)

Substitui o rpi_adb_bridge.py original. O que muda em relação a ele:

  - transporte por ADB DE REDE em vez de USB: antes de dar `adb forward`,
    a gente faz `adb connect <tablet_ip>:<porta_adb>` pela rede privada.
  - o resto é o mesmo mecanismo de sempre: o app no tablet é quem abre o
    socket (ServerSocket na porta 8765); a Pi conecta em 127.0.0.1:8765
    depois do `adb forward`, e o ADB encaminha isso pro tablet por trás
    dos panos.

Pré-requisito importante: isso precisa rodar DENTRO da sessão gráfica da
Raspberry Pi, com o programa que deve receber a credencial aberto e em
foco na tela. pyautogui só sabe digitar na tela local — ele não tem como
alcançar outra máquina (isso é o que uinput/USB HID fazia, e não é o caso
aqui: o alvo é local na própria Pi).
"""

import os
import re
import signal
import socket
import subprocess
import sys
import time

TABLET_IP = os.environ.get("CATRACA_TABLET_IP")
ADB_PORT = int(os.environ.get("CATRACA_ADB_PORT", "5555"))
APP_PORT = int(os.environ.get("CATRACA_APP_PORT", "8765"))
MODO = os.environ.get("CATRACA_INPUT_MODE", "pyautogui")  # "pyautogui" ou "log"

# Importado só quando MODO == "pyautogui": o pyautogui exige DISPLAY já no
# import (quebra com KeyError: 'DISPLAY' se não houver sessão gráfica), então
# em modo "log" a gente nem tenta importar — assim dá pra testar a conexão
# adb/rede numa Pi sem tela também.
pyautogui = None
if MODO == "pyautogui":
    import pyautogui

CREDENCIAL_RE = re.compile(r"^[0-9]{1,11}$")
INTERVALO_ENTRE_TECLAS_S = 0.012  # mesmo espaçamento usado na versão uinput
DELAY_RETRY_ADB_S = 5
DELAY_RETRY_SOCKET_S = 2
TIMEOUT_CONNECT_S = 4

rodando = True


def _parar(signum, frame):
    global rodando
    rodando = False


signal.signal(signal.SIGTERM, _parar)
signal.signal(signal.SIGINT, _parar)


def log(msg: str) -> None:
    print(msg, flush=True)


def digitar(linha: str) -> None:
    if MODO == "log":
        log(f'[pyautogui] [SIMULAÇÃO] digitaria "{linha}" + Enter')
        return
    pyautogui.write(linha, interval=INTERVALO_ENTRE_TECLAS_S)
    pyautogui.press("enter")


def _rodar_adb(argv: list) -> "subprocess.CompletedProcess | None":
    """Roda um comando adb. Retorna None (em vez de deixar a exceção subir)
    se o binário "adb" nem existir no PATH — erro comum de instalação que
    não deveria derrubar o serviço inteiro, só logar e deixar o loop
    de retry tentar de novo mais tarde."""
    try:
        return subprocess.run(argv, capture_output=True, text=True)
    except FileNotFoundError:
        log('[adb] binário "adb" não encontrado no PATH — instale android-tools-adb (ou platform-tools)')
        return None
    except OSError as e:
        log(f"[adb] erro executando {argv}: {e}")
        return None


def adb_connect() -> bool:
    alvo = f"{TABLET_IP}:{ADB_PORT}"
    log(f"[adb] adb connect {alvo}")
    r = _rodar_adb(["adb", "connect", alvo])
    if r is None:
        return False
    saida = (r.stdout + r.stderr).lower()
    ok = r.returncode == 0 and "unable to connect" not in saida and "failed" not in saida
    if not ok:
        log(f"[adb] falha ao conectar em {alvo}: {(r.stdout + r.stderr).strip()}")
    return ok


def adb_forward() -> bool:
    porta = f"tcp:{APP_PORT}"
    log(f"[adb] adb forward {porta} {porta}")
    r = _rodar_adb(["adb", "forward", porta, porta])
    if r is None:
        return False
    ok = r.returncode == 0
    if not ok:
        log(f"[adb] falha ao configurar o forward da porta {APP_PORT}: {r.stderr.strip()}")
    return ok


def esperar_interrompivel(segundos: float) -> None:
    fim = time.monotonic() + segundos
    while rodando and time.monotonic() < fim:
        time.sleep(0.2)


def tratar_conexao(sock: socket.socket) -> None:
    sock.settimeout(1.0)  # curto de propósito: permite checar `rodando` com frequência
    log("[socket] conectado ao app do tablet via túnel adb — pronto pra receber credenciais")

    buffer = ""
    while rodando:
        try:
            dado = sock.recv(128)
        except socket.timeout:
            continue
        except OSError as e:
            log(f"[socket] erro lendo do tablet: {e}")
            return

        if not dado:
            log("[socket] conexão fechada pelo tablet")
            return

        buffer += dado.decode("ascii", errors="ignore")
        while "\n" in buffer:
            linha, buffer = buffer.split("\n", 1)
            linha = linha.strip("\r")
            if not linha:
                continue
            if CREDENCIAL_RE.match(linha):
                log(f"[catraca] credencial recebida: {linha}")
                digitar(linha)
            else:
                log(f'[catraca] credencial inválida recebida do tablet: "{linha}"')


def main() -> None:
    if not TABLET_IP:
        log("[catraca] CATRACA_TABLET_IP não definido — configure o IP do tablet na rede privada")
        sys.exit(1)

    log("=== catraca-bridge (python/pyautogui) ===")
    log(f"[catraca] tablet: {TABLET_IP} (adb porta {ADB_PORT}, app porta {APP_PORT}, modo {MODO})")

    while rodando:
        if not adb_connect():
            esperar_interrompivel(DELAY_RETRY_ADB_S)
            continue
        if not adb_forward():
            esperar_interrompivel(DELAY_RETRY_ADB_S)
            continue

        try:
            with socket.create_connection(("127.0.0.1", APP_PORT), timeout=TIMEOUT_CONNECT_S) as sock:
                tratar_conexao(sock)
        except OSError as e:
            log(f"[socket] não conectou em 127.0.0.1:{APP_PORT}: {e}")

        esperar_interrompivel(DELAY_RETRY_SOCKET_S)

    log("[catraca] encerrando (sinal recebido)")


if __name__ == "__main__":
    main()
