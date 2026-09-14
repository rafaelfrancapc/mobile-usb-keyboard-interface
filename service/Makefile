CC       ?= cc
CFLAGS   ?= -O2 -Wall -Wextra -std=c11
SRC      := src/main.c src/adb_bridge.c src/uinput_backend.c
BIN      := catraca-bridge

.PHONY: all clean install

all: $(BIN)

$(BIN): $(SRC) src/adb_bridge.h src/uinput_backend.h
	$(CC) $(CFLAGS) -o $(BIN) $(SRC)

clean:
	rm -f $(BIN)

# Instala o binário, o serviço systemd, a regra de udev e o arquivo de
# configuração de exemplo. Rode com sudo. Não habilita nem inicia o
# serviço automaticamente — veja o README para os passos de configuração
# (IP do tablet) antes de habilitar.
install: $(BIN)
	install -Dm755 $(BIN) /usr/local/bin/$(BIN)
	install -Dm644 systemd/catraca-bridge.service /etc/systemd/system/catraca-bridge.service
	install -Dm644 udev/99-catraca-uinput.rules /etc/udev/rules.d/99-catraca-uinput.rules
	install -Dm600 -b etc/catraca-bridge.conf.example /etc/catraca-bridge.conf
	udevadm control --reload-rules
	systemctl daemon-reload
	@echo ""
	@echo "Instalado. Antes de habilitar o serviço:"
	@echo "  1. edite /etc/catraca-bridge.conf (defina CATRACA_TABLET_IP)"
	@echo "  2. sudo systemctl enable --now catraca-bridge"
