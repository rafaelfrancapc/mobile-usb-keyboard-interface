# catraca-bridge (Python / pyautogui)

Substitui a versão em C. Faz a mesma ponte adb de rede (`adb connect` +
`adb forward`), mas digita a credencial com `pyautogui`, na sessão gráfica
local da Raspberry Pi — porque aqui o alvo é um programa com tela própria
rodando **na própria Pi**, não uma máquina externa via USB. (Se um dia o
alvo mudar pra uma máquina externa ligada por USB, essa abordagem para de
servir — nesse cenário só `uinput`/HID resolve, como na versão em C.)

## Se você já tinha instalado a versão em C

Desative e limpe antes de instalar esta:

```sh
sudo systemctl disable --now catraca-bridge   # a versão de sistema, em C
sudo rm /etc/systemd/system/catraca-bridge.service
sudo rm /etc/udev/rules.d/99-catraca-uinput.rules
sudo systemctl daemon-reload
```

(Não precisa remover o usuário `catraca` nem `/dev/uinput` — só não vão mais
ser usados por este projeto.)

## Dependências (na Pi, com Raspberry Pi OS Desktop)

```sh
sudo apt install python3-pip python3-tk python3-dev scrot android-tools-adb
pip install -r requirements.txt --break-system-packages
```

- `python3-tk` e `python3-dev`: o `pyautogui` depende deles no Linux.
- `scrot`: usado internamente pelo `pyautogui` (mesmo sem tirar screenshot
  você, ele importa isso na inicialização).
- **Importante**: isso só funciona com a Pi rodando o Raspberry Pi OS
  **Desktop** (com sessão gráfica). No PiOS **Lite** (headless) não tem X/
  Wayland nenhum rodando — `pyautogui` não tem pra onde digitar.

## Configuração e instalação (como o usuário logado na tela, ex.: `pi`)

```sh
mkdir -p ~/catraca-bridge
cp catraca_bridge.py ~/catraca-bridge/
cp etc/catraca-bridge.conf.example ~/.config/catraca-bridge.conf
nano ~/.config/catraca-bridge.conf   # defina CATRACA_TABLET_IP

mkdir -p ~/.config/systemd/user
cp systemd/catraca-bridge.service ~/.config/systemd/user/
systemctl --user daemon-reload
```

**Confirme o `DISPLAY` certo antes de habilitar** — abra um terminal na
própria tela da Pi (não por SSH) e rode:

```sh
echo $DISPLAY
```

Se não for `:0`, edite `Environment=DISPLAY=:0` em
`~/.config/systemd/user/catraca-bridge.service` pro valor real.

Habilite (também dentro da sessão gráfica, não por SSH puro — serviço de
usuário depende da sessão estar ativa):

```sh
systemctl --user enable --now catraca-bridge
journalctl --user -u catraca-bridge -f
```

## Testando sem digitar de verdade

```sh
CATRACA_INPUT_MODE=log CATRACA_TABLET_IP=192.168.42.129 python3 catraca_bridge.py
```

Esse modo funciona mesmo sem sessão gráfica (o `import pyautogui` só
acontece quando `CATRACA_INPUT_MODE=pyautogui`) — bom pra validar só a
parte de rede/adb primeiro.

## Coisas que eu já testei de verdade (não só assumi)

Rodei este script aqui com um `adb` de verdade instalado, sem device
nenhum plugado, pra conferir o que ele realmente faz — vale registrar
porque uma das duas pegadinhas abaixo teria passado despercebida com um
teste só "no papel":

- `adb connect` pra um endereço que não existe **retorna código de saída 0
  mesmo falhando** — o erro (`failed to connect to '...': Connection
  refused`) só aparece no texto da saída. Por isso o código checa o texto
  também, não só o código de saída; só olhar `returncode == 0` marcaria
  isso como sucesso.
- `adb forward` sem nenhum device conectado aí sim retorna código de saída
  diferente de zero (`adb: error: no devices/emulators found`) — esse
  comportamento é consistente com o esperado.
- Se o binário `adb` não estiver instalado, o script agora loga isso e
  tenta de novo no próximo ciclo, em vez de travar com um traceback (era
  esse o bug do primeiro rascunho).

## Diferenças em relação ao `rpi_adb_bridge.py` original

- ADB de rede (`adb connect`) em vez de assumir que o cabo USB já está
  plugado.
- Reconecta sozinho (adb + socket) se a conexão cair, com backoff simples.
- Roda como serviço systemd de usuário, reinicia sozinho se cair.
- Validação da credencial (só dígitos, até 11 caracteres) antes de digitar
  qualquer coisa.
