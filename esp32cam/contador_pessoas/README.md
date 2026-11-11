# Contador de pessoas com ESP32-CAM

Sistema para contar entradas e saídas de alunos em uma sala de aula usando uma **ESP32-CAM instalada no teto** (visão zenital), com visão computacional leve (subtração de fundo + rastreamento de centróide + linhas virtuais), sem redes neurais pesadas.

> Disciplina de Instrumentação — UFC Quixadá.

---

## 1. Ideia do sistema

- Dois sensores laser na porta foram descartados: a porta abrindo gera falsos positivos e falsos negativos.
- A ESP32-CAM fica no teto, apontada para baixo, sobre a soleira. Vistas de cima, as pessoas aparecem como manchas isoladas, o que evita oclusão.
- Algoritmo planejado:
  1. **Subtração de fundo** adaptativa em imagem de baixa resolução (grayscale).
  2. **Detecção de blobs** (componentes conexos) e centróides.
  3. **Rastreamento** por vizinho mais próximo.
  4. **Uma linha na vertical**  cruzar de cima para baixo = entrada (+1), de baixo para cima = saída (−1).
  5. Ocupação = entradas − saídas (nunca abaixo de 0).

---

## 2. Estado atual

| Etapa | Descrição | Status |
|---|---|---|
| 0 | Ambiente: projeto ESP-IDF + componente `esp32-camera` + `sdkconfig` | Concluída |
| 1 | Captura em grayscale QVGA (320x240), FPS no log | Concluída (~11 FPS) |
| 2 | Wi-Fi + servidor HTTP: ver a imagem no navegador (`/`, `/capture`, `/stream`) | Concluída (~11 FPS no stream) |
| 3 | Montagem no teto + gravação de clipes com gabarito (`gravar.py`) | Em andamento, precisa de mais coletas de daodos para treino do algoritmo |
| 4 | Subtração de fundo + máscara de movimento (prototipar em Python) | Concluido |
| 5 | Detecção de blobs e centróides |COncluido |
| 6 | Rastreamento + linhas virtuais + contagem | Em andamento |
| 7 | Validação com os clipes (esperado × contado) e ajuste fino | Em andamento |
| 8 | Integração com a central | Pendente |

### Hardware usado
- ESP32-CAM **AI-Thinker** (ESP32-D0WD, rev v3.1, flash de 4 MB, PSRAM de 8 MB, dos quais 4 MB são mapeados).
- Sensor detectado: **OV3660** (PID `0x3660`).
- Adaptador USB-serial (FTDI/CH340) para gravar, já que a placa não tem USB.
- Fonte 12v passando pelo regulador de tensão fazendo ter uma entrada na esp32cam estável de 5v.

### Software
- **ESP-IDF** v6.1-dev (testado com `v6.1-dev-3776-g508ba7ae04`), target `esp32`.
- Componente `espressif/esp32-camera` (gerenciador de componentes do IDF).
- Python 3 + OpenCV (`opencv-python`) no PC, só para gravar os clipes.

---

## 3. Estrutura do projeto

```
contador_pessoas/
├── CMakeLists.txt
├── sdkconfig.defaults
├── main/
│   ├── CMakeLists.txt          # SRCS "contador_pessoas.c" "web_stream.c"
│   ├── idf_component.yml       # dependência: espressif/esp32-camera
│   ├── contador.c              # código com algoritmo de detectação
│   ├── contador.h              
│   ├── contador_pessoas.c      # init da câmera (grayscale QVGA) e app_main
│   ├── web_stream.c            # Wi-Fi (STA) + servidores HTTP (80 e 81)
│   └── web_stream.h
└── gravar/
    ├── gravar.py               # grava clipes do stream no PC (ver seção 7)
``` ├── contar.py               # mesmo algoritmo do contador.c porem em python usado para testar com gabaritos na pasta clips 
    │
    └── clipes/

### Parâmetros da câmera (em `contador_pessoas.c`)
- `PIXFORMAT_GRAYSCALE`, `FRAMESIZE_QVGA` (320x240), `xclk_freq_hz = 20 MHz`
- `fb_count = 2`, `fb_location = CAMERA_FB_IN_PSRAM`, `grab_mode = CAMERA_GRAB_LATEST`

### Endpoints HTTP da ESP32
| URL | Função |
|---|---|
| `http://IP/` | Página com o vídeo ao vivo |
| `http://IP/capture` | Uma foto (JPEG) |
| `http://IP:81/stream` | Stream MJPEG (**um cliente por vez**) |

O JPEG serve só para visualização; o algoritmo de contagem usará o quadro grayscale cru.

---

## 4. Reproduzir em outro PC: firmware (ESP-IDF)

### 4.1 Instalar o ESP-IDF
Siga o guia oficial: <https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/>

Linux, resumo:
```bash
sudo apt install git wget flex bison gperf python3 python3-pip python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0
mkdir -p ~/esp && cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32
```

A cada terminal novo, carregue o ambiente:
```bash
. ~/esp/esp-idf/export.sh
```

> Usei o branch `master` (v6.1-dev). Uma versão estável recente (ex.: v5.5) deve funcionar, mas pode exigir pequenos ajustes de API. Se quiser a mesma versão, faça `git checkout 508ba7ae04` e `git submodule update --init --recursive`.

### 4.2 Permissão da porta serial (Linux)
```bash
sudo usermod -aG dialout $USER
```
Faça logout e login. No Windows, use a porta `COMx` (veja no Gerenciador de Dispositivos).

### 4.3 Configurar `sdkconfig.defaults`
Na raiz do projeto:
```
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y
CONFIG_COMPILER_OPTIMIZATION_PERF=y
CONFIG_FREERTOS_HZ=1000
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_QUAD=y
```

> O `sdkconfig.defaults` **só é lido quando o `sdkconfig` é gerado do zero**. Se mudar o arquivo, rode `rm sdkconfig && idf.py set-target esp32`.

Confira se pegou:
```bash
grep -E "FLASHSIZE=|OPTIMIZATION_PERF=|FREERTOS_HZ=|SPIRAM=|SINGLE_APP_LARGE" sdkconfig
```

> **Atenção à partição:** no último log de boot, a partição `factory` ainda aparecia com **1 MB** (`00100000`) e o app já ocupava ~945 KB. Se, ao adicionar código, o build falhar com "app partition is too small", a tabela `SINGLE_APP_LARGE` (1,5 MB) não foi aplicada. Ative em `idf.py menuconfig` → *Partition Table* → *Single factory app (large), no OTA*.

### 4.4 Componente da câmera
```bash
cd main
idf.py add-dependency "espressif/esp32-camera"
cd ..
```
O arquivo `main/idf_component.yml` fica com:
```yaml
dependencies:
  espressif/esp32-camera: "*"
```
O componente é baixado no primeiro `idf.py build` (precisa de internet).

### 4.5 Definir o Wi-Fi
Em `main/web_stream.c`:
```c
#define WIFI_SSID "ContadorCam"
#define WIFI_PASS "12345678"
```
Use exatamente o nome e a senha do hotspot da seção 5. Não suba a senha real para um repositório público.

### 4.6 Compilar e gravar
```bash
idf.py set-target esp32
idf.py build
```

Para gravar:
1. Ligue o **GPIO0 ao GND** e dê reset na placa.
2. Rode:
   ```bash
   idf.py -p /dev/ttyUSB0 flash monitor
   ```
   (no Windows: `-p COM5`, por exemplo).
3. Depois de gravar, **desligue o GPIO0 do GND** e dê reset para ela rodar o programa.
4. Para sair do monitor: `Ctrl+]`.

---

## 5. Ver a imagem usando o hotspot do notebook

A ESP32 só conecta em Wi-Fi **2,4 GHz**. A rede da UFC (eduroam, WPA2-Enterprise) **não funciona**, então use um hotspot.

### Linux (NetworkManager)
```bash
nmcli device status                      # descubra a interface Wi-Fi (ex.: wlp2s0)
nmcli device wifi hotspot ifname wlp2s0 con-name HotspotCam ssid ContadorCam password 12345678 band bg channel 6
nmcli connection show --active           # confirme que subiu
```
- `band bg` força 2,4 GHz (essencial).
- A senha precisa ter pelo menos 8 caracteres.
- O notebook vira o gateway `10.42.0.1`; a ESP32 recebe `10.42.0.x`.
- Desligar: `nmcli connection down HotspotCam`. Ligar de novo: `nmcli connection up HotspotCam`.
- Com uma placa Wi-Fi só, o notebook **perde a internet** enquanto o hotspot estiver ligado.

### Windows
*Configurações → Rede e Internet → Hotspot móvel*. Escolha a banda **2,4 GHz** nas propriedades. O gateway costuma ser `192.168.137.1` e a ESP32 recebe `192.168.137.x`.

### Alternativa
Hotspot do celular (2,4 GHz, segurança WPA2).

### Abrir a imagem
1. Ligue o hotspot **antes** de ligar a ESP32.
2. Com a ESP32 gravada e rodando, o monitor mostra:
   ```
   web: IP obtido: 10.42.0.77  -> abra http://10.42.0.77/
   ```
3. No navegador do notebook (conectado ao hotspot), abra esse endereço.

O IP pode mudar a cada conexão. Para listar os dispositivos conectados no Linux: `ip neigh`.

---

## 6. Instalar o Python e o OpenCV (para gravar)

### Linux
```bash
sudo apt install python3 python3-venv python3-pip
cd contador_pessoas/gravar
python3 -m venv venv_cam
source venv_cam/bin/activate
pip install opencv-python
```
Quando o ambiente está ativo, o prompt mostra `(venv_cam)`. Em cada terminal novo, rode `source venv_cam/bin/activate` antes.

### Windows
1. Baixe o Python em <https://www.python.org/downloads/> e **marque "Add python.exe to PATH"** na instalação.
2. No PowerShell ou CMD:
   ```bat
   cd contador_pessoas\gravar
   py -m venv venv_cam
   venv_cam\Scripts\activate
   pip install opencv-python
   ```

### macOS
```bash
brew install python
python3 -m venv venv_cam
source venv_cam/bin/activate
pip install opencv-python
```

---

## 7. Gravar clipes com gabarito

Com o hotspot ligado, a ESP32 conectada e o IP em mãos:

```bash
python gravar.py nome_do_clipe http://10.42.0.77:81/stream
```

- `nome_do_clipe`: ex.: `entra_1`, `sai_2`, `vazio_1`.
- O segundo argumento é a URL do stream. Troque o IP pelo que aparece no monitor.

Teclas na janela do vídeo:

| Tecla | Ação |
|---|---|
| `e` | Marca uma **entrada** (quando a pessoa terminar de cruzar) |
| `s` | Marca uma **saída** |
| `q` | Encerra a gravação |

Saída em `clips/<nome>/`:
- `00000.png`, `00001.png`, ... (quadros em grayscale)
- `frames.csv` (índice e tempo de cada quadro)
- `gabarito.csv` (eventos marcados)

O instante marcado tem atraso de reação humana. Use o **total por clipe** como gabarito e mantenha uma planilha com *clipe, entradas esperadas, saídas esperadas*.

> **Importante:** o servidor do stream (porta 81) atende **um cliente por vez**. Feche a aba do navegador que estiver com o stream aberto antes de rodar o `gravar.py`.

### Roteiro de clipes sugerido (20 a 30 clipes de 10 a 20 s)

| Cenário | Gabarito |
|---|---|
| Local vazio, 30 s (referência de fundo) | 0 / 0 |
| 1 pessoa entra (3x) | 1 / 0 |
| 1 pessoa sai (3x) | 0 / 1 |
| 2 pessoas lado a lado entrando | 2 / 0 |
| 2 pessoas em fila entrando | 2 / 0 |
| 1 entra e outra sai cruzando | 1 / 1 |
| Pessoa para na soleira e volta | 0 / 0 |
| Pessoa correndo | 1 / 0 |
| Mochila, boné, roupa clara e escura (inclusive da cor do piso) | conforme o caso |
| Porta abrindo e fechando sem ninguém passar | 0 / 0 |
| Luz ligando e apagando | 0 / 0 |

---

## 8. Montagem física (Etapa 3)

- Câmera **centrada sobre a linha de cruzamento** (soleira), apontada na vertical. O corredor ocupa a metade de cima da imagem e a sala a metade de baixo.
- Altura inicial: **2,5 m**. Meça quantos metros de piso cabem na imagem com uma fita métrica; o ideal é cobrir 1,5 a 2 m de largura.
- Se a folha da porta entrar no campo de visão ao abrir, anote a região: ela será mascarada no algoritmo.
- Se a imagem sair espelhada ou invertida, ajuste depois do `esp_camera_init` em `init_camera()`:
  ```c
  sensor_t *s = esp_camera_sensor_get();
  if (s) {
      s->set_vflip(s, 1);
      s->set_hmirror(s, 1);
  }
  ```
  Escolha a orientação em que **entrar na sala = movimento de cima para baixo** na imagem.
- Na Etapa 4, a exposição e o ganho automáticos serão travados (`s->set_exposure_ctrl(s, 0)` e `s->set_gain_ctrl(s, 0)`) para o brilho não variar sozinho.

---

## 9. Problemas comuns

| Sintoma | Causa provável e solução |
|---|---|
| `implicit declaration of function 'esp_timer_get_time'` | Faltou `#include "esp_timer.h"` em `web_stream.c` |
| Boot não passa do bootloader / "waiting for download" | GPIO0 ainda no GND. Desligue e dê reset |
| `Camera probe failed` / `0x105` | Fita flat da câmera mal encaixada ou alimentação fraca |
| Reinicia sozinha / *brownout* | Fonte ou cabo USB fraco; use 5 V estável |
| `frame buffer malloc failed` | PSRAM desabilitada (`CONFIG_SPIRAM=y`) |
| Log mostra `Detected OV3660` e `Mismatch PID` | Normal: o driver testa sensores até achar o certo |
| Avisos `GPIO 26/27 is not usable` e `I2C hardware timeout` no boot | Normais se logo depois aparecer `Detected camera at address=0x3c` |
| `cam_hal: EV-VSYNC-OVF` uma vez ao conectar o Wi-Fi | Sem problema; se repetir muito, avisar |
| `Wi-Fi caiu, reconectando...` em loop | Senha errada ou hotspot em 5 GHz |
| Sem IP no log | Hotspot desligado, ou rede não é 2,4 GHz |
| `No module named 'cv2'` | Ambiente virtual não ativo ou `pip install opencv-python` não rodado |
| `Connection ... timed out` ao rodar `gravar.py` | Ping não responde (hotspot caiu ou IP mudou) ou o stream está ocupado por outra aba. Confira o IP no monitor e feche o navegador |
| `Device does not support AP mode` | A placa Wi-Fi do notebook não faz hotspot; use o do celular |
| App não cabe na partição | Ver o aviso da seção 4.3 (`SINGLE_APP_LARGE`) |

---

## 10. Próximos passos

1. Gravar mais clipes para refinamento do algoritmo.
2. Fazer comunicação com central ou servidor do projeto para mandar os dados de quantas pessoas tem na sala.
3. Implementar economia de energia na esp32cam.
4. Desenvolver se possivel um detector de presença antes da pessoa passar para ligar a camera pois a camera ligada o tempo todo consome energia.