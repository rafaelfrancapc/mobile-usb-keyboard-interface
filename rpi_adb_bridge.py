import subprocess
import socket
import time
import sys
import pyautogui

PORTA = 8765

def configurar_adb_forward():
    """Executa o comando adb forward no sistema operacional."""
    try:
        # Executa 'adb forward tcp:8765 tcp:8765'
        resultado = subprocess.run(
            ["adb", "forward", f"tcp:{PORTA}", f"tcp:{PORTA}"],
            capture_output=True,
            text=True,
            check=True
        )
        print("-> Ponte ADB USB configurada com sucesso.")
        return True
    except (subprocess.CalledProcessError, FileNotFoundError) as e:
        print(f"-> Erro ao configurar ADB: O tablet está plugado? O ADB está instalado? ({e})")
        return False

def rodar_daemon():
    print("=== Iniciando Daemon da Catraca USB ===")
    
    while True:
        # 1. Tenta configurar o encaminhamento de porta do ADB
        if not configurar_adb_forward():
            print("Aguardando 5 segundos para tentar detectar o tablet novamente...")
            time.sleep(5)
            continue
        
        # 2. Tenta conectar no socket local (que agora aponta para o tablet via USB)
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(5.0) # Timeout para não travar o script se o tablet congelar
        
        try:
            print(f"Tentando conectar ao aplicativo no tablet (localhost:{PORTA})...")
            s.connect(("127.0.0.1", PORTA))
            s.settimeout(None) # Remove o timeout após conectar para ouvir indefinidamente
            print("=> CONECTADO com sucesso ao tablet via USB!")
            
            buffer = ""
            while True:
                dados = s.recv(1024).decode('utf-8')
                if not dados:
                    print("-> Conexão fechada pelo tablet.")
                    break
                
                buffer += dados
                if "\n" in buffer:
                    linha, buffer = buffer.split("\n", 1)
                    linha = linha.strip()
                    if linha:
                        print(f"Recebido do tablet: {linha}")
                        # Simula a digitação no computador
                        pyautogui.write(linha)
                        pyautogui.press("enter")
                        
        except (socket.error, socket.timeout) as e:
            print(f"-> Conexão perdida ou recusada pelo tablet. Motivo: {e}")
        finally:
            try:
                s.close()
            except:
                pass
        
        # Aguarda um pouco antes de tentar restabelecer toda a estrutura USB
        print("Reiniciando ciclo de conexão em 3 segundos...\n")
        time.sleep(3)

if __name__ == "__main__":
    try:
        rodar_daemon()
    except KeyboardInterrupt:
        print("\nDaemon finalizado pelo usuário.")
        sys.exit(0)