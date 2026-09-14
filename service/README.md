# catraca-bridge

Substitui o `rpi_adb_bridge.py` do
[mobile-usb-keyboard-interface](https://github.com/rafaelfrancapc/mobile-usb-keyboard-interface)
por um serviço em C, rodando via systemd na Raspberry Pi, usando **ADB de
rede** em vez de ADB via cabo USB, e **uinput** (teclado virtual de kernel)
em vez de `pyautogui` pra reproduzir a credencial.

## Por que o app Android não precisou mudar

Vale registrar isso porque não é óbvio à primeira vista: o `MainActivity.kt`
já é **agnóstico de transporte**. Ele nunca discou pra lugar nenhum — ele
abre um `ServerSocket` local na porta 8765 e fica esperando alguém conectar
(`garantirConexao()`). Do ponto de vista do app, USB e rede são a mesma
coisa: ele só sabe que "alguém conectou no meu socket local". Quem decide
*como* essa conexão chega até lá é o lado de fora — antes era `adb forward`
puro (o computador já enxergava o tablet via cabo, e o Android Studio
autoriza isso automaticamente); agora precisamos de um passo a mais
(`adb connect <ip>`) antes do mesmo `adb forward` de sempre.

Ou seja: **nenhuma mudança é necessária no app** pra essa migração. O que
muda é só o lado da Pi.

> Sobre reescrever o app inteiro em C (NDK/NativeActivity, sem Kotlin): dá
> pra fazer, mas é um projeto grande à parte — sem Java você perde de graça
> coisas como `EditText`/`AlertDialog` (o menu oculto de IP viraria um
> teclado desenhado na mão), `Vibrator`/`AudioManager` (passam a exigir JNI
> manual) e a fonte com acentuação (exigiria rasterizar glifos via
> `stb_truetype` a partir de um `.ttf` do sistema). Como o app atual já
> funciona e é indiferente a USB vs. rede, não é um bloqueio pra essa
> mudança de arquitetura — me avisa se ainda assim quiser que eu encare essa
> reescrita à parte.

## Arquitetura

```
┌─────────────────────┐        rede privada         ┌──────────────────────────┐
│   Tablet (Android)   │  (RNDIS/USB ou Wi-Fi)       │      Raspberry Pi         │
│                       │◄────────────────────────────┤                          │
│  MainActivity.kt      │   adb connect <ip>:5555     │  catraca-bridge (C)      │
│  ServerSocket :8765   │   adb forward tcp:8765      │   1. adb connect         │
│  (menu oculto de IP   │   tcp:8765                  │   2. adb forward         │
│   ainda guarda o      │                              │   3. connect            │
│   rpiHost, hoje só    │◄────────────────────────────┤      127.0.0.1:8765      │
│   informativo)        │   credencial + "\n"          │   4. valida e chama     │
└─────────────────────┘                              │      uinput (KEY_0..9,   │
                                                        │      KEY_ENTER)          │
                                                        │           │              │
                                                        │           ▼              │
                                                        │   /dev/uinput            │
                                                        │   (teclado virtual)      │
                                                        └──────────────┬───────────┘
                                                                       │ USB
                                                                       ▼
                                                          PC do controle de acesso
```

## Pré-requisitos

**Na Pi:**
- `adb` instalado e no PATH: `sudo apt install android-tools-adb` (ou o
  pacote `platform-tools` da Google, se preferir uma versão mais nova)
- kernel com uinput disponível (`sudo modprobe uinput`; pra carregar sempre
  no boot, adicione `uinput` em `/etc/modules`)

**No tablet:**
- depuração USB habilitada (Opções do desenvolvedor)
- `adb tcpip 5555` executado ao menos uma vez (com o tablet plugado por USB
  numa máquina com adb, rode `adb tcpip 5555`). Isso liga o `adbd` em modo
  rede na porta 5555. **Isso não persiste sozinho após reiniciar o tablet**
  — se ele reiniciar, alguém precisa plugar o cabo de novo e rodar o comando
  uma vez, a não ser que vocês automatizem isso via root (`setprop
  service.adb.tcp.port 5555` num script de boot, se o tablet for rooted).
- app instalado e rodando (ele já abre a porta 8765 sozinho no `onCreate`)
- confirmado o IP do tablet na rede privada (o hidden menu — segure
  "IDENTIFICAÇÃO DIGITADA" — mostra/edita esse valor hoje só como referência
  informativa, já que quem precisa dele de verdade agora é a configuração da
  Pi, não o app)

## Build e instalação

```sh
make
sudo make install
```

Isso instala o binário em `/usr/local/bin/catraca-bridge`, o unit file, a
regra de udev, e copia `etc/catraca-bridge.conf.example` pra
`/etc/catraca-bridge.conf` (sem sobrescrever se já existir).

**Antes de habilitar o serviço:**

1. Crie o usuário dedicado, com HOME de verdade (o `adb` guarda a chave de
   pareamento RSA em `~/.android/` — precisa persistir entre reinícios do
   serviço, senão o tablet pede autorização de depuração USB de novo toda
   hora):
   ```sh
   sudo useradd --system --create-home --shell /usr/sbin/nologin \
     --groups input catraca
   ```
2. Edite `/etc/catraca-bridge.conf` e defina `CATRACA_TABLET_IP` com o IP
   real do tablet na rede privada.
3. Autorize a chave adb uma vez rodando o serviço manualmente como o
   usuário `catraca` com o tablet visível, e aceitando o prompt de
   autorização que aparece na tela do tablet:
   ```sh
   sudo -u catraca -H env $(cat /etc/catraca-bridge.conf | grep -v '^#') \
     /usr/local/bin/catraca-bridge
   ```
   (Ctrl+C depois de ver "conectado ao app do tablet" no log.)
4. Habilite de verdade:
   ```sh
   sudo systemctl enable --now catraca-bridge
   sudo journalctl -u catraca-bridge -f
   ```

## Testando sem mexer em `/dev/uinput`

```sh
CATRACA_INPUT_MODE=log CATRACA_TABLET_IP=192.168.42.129 ./catraca-bridge
```

Nesse modo, em vez de digitar de verdade, o programa só imprime no log o
que teria digitado — útil pra validar a conexão adb/rede sem correr o risco
de "digitar" numa máquina de verdade enquanto testa.

## Arquivos

- `src/main.c` — configuração (variáveis de ambiente), sinais, liga tudo
- `src/adb_bridge.c/.h` — `adb connect` + `adb forward` + socket local +
  parsing de linhas (substitui o `rpi_adb_bridge.py`)
- `src/uinput_backend.c/.h` — teclado virtual via `/dev/uinput` (porta em C
  do `input.rs` da versão Rust anterior deste projeto)
- `systemd/catraca-bridge.service` — unit file
- `udev/99-catraca-uinput.rules` — permissão pro usuário de serviço acessar
  `/dev/uinput` sem ser root
- `etc/catraca-bridge.conf.example` — configuração (IP do tablet, portas,
  modo de entrada)
