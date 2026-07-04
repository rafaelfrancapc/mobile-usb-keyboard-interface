#!/usr/bin/env python3
"""
Bridge USB (RNDIS/tethering) <-> teclado virtual (uinput).

Escuta uma porta TCP na interface de rede que aparece quando o tablet
conecta via cabo USB com "tethering USB" ativado, e injeta eventos de
teclado via /dev/uinput -- replicando o comportamento do teclado
numérico físico que o software da catraca já reconhece.

IMPORTANTE: antes de usar, rode `sudo evtest` com o teclado físico
conectado, aperte cada tecla (0-9, Enter) e anote os keycodes exatos
que aparecem (ex: KEY_KP0 ou KEY_0, KEY_KPENTER ou KEY_ENTER). Ajuste
o KEY_MAP e ENTER_KEY abaixo para bater exatamente com isso -- é isso
que garante que o software da catraca não perceba diferença nenhuma.

Instalação:
    sudo apt install python3-pip
    sudo pip3 install evdev

Execução manual (teste):
    sudo python3 rpi_keypad_bridge.py

Para rodar sempre como serviço, ver catraca-bridge.service.

MODO SIMULADO: se /dev/uinput não existir (ex: rodando no WSL do Windows,
que não tem esse módulo de kernel por padrão), o script detecta isso
sozinho e cai em modo simulado, só imprimindo no terminal o que seria
digitado -- assim dá pra testar a parte de rede/protocolo sem precisar
de um ambiente Linux completo. A injeção de teclado real só funciona
numa Raspberry Pi de verdade (ou numa VM Linux completa).
"""

import socket
import threading
import time

try:
    from evdev import UInput, ecodes as e
    EVDEV_DISPONIVEL = True
except ImportError:
    EVDEV_DISPONIVEL = False

HOST = "0.0.0.0"      # escuta em todas as interfaces, incluindo a que aparece via USB
PORT = 8765

ui = None
MODO_SIMULADO = True

if EVDEV_DISPONIVEL:
    # --- AJUSTE AQUI conforme o `evtest` mostrar para o teclado físico real ---
    KEY_MAP = {
        "0": e.KEY_KP0, "1": e.KEY_KP1, "2": e.KEY_KP2, "3": e.KEY_KP3,
        "4": e.KEY_KP4, "5": e.KEY_KP5, "6": e.KEY_KP6, "7": e.KEY_KP7,
        "8": e.KEY_KP8, "9": e.KEY_KP9,
    }
    ENTER_KEY = e.KEY_KPENTER
    # -----------------------------------------------------------------------

    CAPABILITIES = {
        e.EV_KEY: list(KEY_MAP.values()) + [ENTER_KEY]
    }

    try:
        # name/vendor/product podem ser ajustados para bater com o `lsusb`/
        # `evtest` do teclado real, caso o software da catraca filtre por
        # identidade do dispositivo (em vez de só escutar qualquer teclado).
        ui = UInput(CAPABILITIES, name="Catraca Virtual Keypad", vendor=0x1234, product=0x5678)
        MODO_SIMULADO = False
    except (FileNotFoundError, PermissionError, OSError) as ex:
        print(f"[!] Não consegui abrir /dev/uinput ({ex}). Caindo em modo simulado.")
else:
    print("[!] Biblioteca 'evdev' não encontrada. Rodando em modo simulado.")
    KEY_MAP = {str(n): str(n) for n in range(10)}
    ENTER_KEY = "ENTER"

if MODO_SIMULADO:
    print("[i] MODO SIMULADO ATIVO: nenhuma tecla real será injetada,")
    print("    apenas impressa no terminal. Use isso para testar a rede/protocolo")
    print("    (ex: no WSL). Para injeção real, rode numa Raspberry Pi/Linux com uinput.")


def enviar_tecla(keycode) -> None:
    if MODO_SIMULADO:
        print(f"    [tecla simulada] {keycode}")
        return
    ui.write(e.EV_KEY, keycode, 1)  # tecla pressionada
    ui.syn()
    time.sleep(0.02)
    ui.write(e.EV_KEY, keycode, 0)  # tecla solta
    ui.syn()
    time.sleep(0.02)


def processar_digitos(digitos: str) -> None:
    for ch in digitos:
        keycode = KEY_MAP.get(ch)
        if keycode is not None:
            enviar_tecla(keycode)
    enviar_tecla(ENTER_KEY)


def tratar_cliente(conn: socket.socket, addr) -> None:
    print(f"[+] Conectado: {addr}")
    buffer = ""
    try:
        with conn:
            while True:
                data = conn.recv(64)
                if not data:
                    break
                buffer += data.decode("utf-8", errors="ignore")
                while "\n" in buffer:
                    linha, buffer = buffer.split("\n", 1)
                    linha = linha.strip()
                    if linha:
                        print(f"[>] Recebido: {linha}")
                        processar_digitos(linha)
    except Exception as ex:
        print(f"[!] Erro na conexão {addr}: {ex}")
    finally:
        print(f"[-] Desconectado: {addr}")


def main() -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as srv:
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind((HOST, PORT))
        srv.listen(5)
        print(f"Aguardando conexões em {HOST}:{PORT}...")
        while True:
            conn, addr = srv.accept()
            threading.Thread(target=tratar_cliente, args=(conn, addr), daemon=True).start()


if __name__ == "__main__":
    main()
