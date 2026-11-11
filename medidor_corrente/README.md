# Medidor de Corrente e Tensão com INA226

Firmware para **ESP32** desenvolvido com **ESP-IDF**, destinado à leitura de tensão, corrente e potência usando o sensor de monitoramento INA226. O programa também integra a corrente ao longo do tempo para estimar a carga elétrica acumulada.

As leituras são enviadas pela saída serial em formato CSV, facilitando a captura e a análise dos dados em ferramentas como Python, Excel ou LibreOffice Calc.

## 1. Hardware

- ESP32 (o `sdkconfig` incluído está configurado para o ESP32 clássico);
- Módulo INA226 (por exemplo, WCMCU-226 ou CJMCU-226);
- Carga e fonte compatíveis com os limites elétricos do módulo;
- Resistores de pull-up I²C, caso a placa não os possua.

### Ligações I²C

| ESP32 | INA226 |
|---|---|
| 3V3 | VCC |
| GND | GND |
| GPIO 21 | SDA |
| GPIO 22 | SCL |

O endereço I²C configurado no firmware é `0x40`. A frequência do barramento é de 400 kHz.

### Ligação da carga

O INA226 mede a corrente por meio de um resistor shunt. A carga deve ser conectada **em série** com o caminho de corrente, respeitando as marcações `IN+` e `IN-` do módulo:

- `IN+`: lado da fonte;
- `IN-`: lado da carga.

A tensão do barramento também precisa estar conectada ao terminal de medição apropriado do módulo para que `vbus_V` represente a tensão da carga. Confira o esquema e a identificação dos terminais da sua placa específica antes de energizar o circuito.

> **Atenção:** não conecte a carga diretamente entre os pinos SDA/SCL nem aplique tensão acima dos limites do INA226 ou do módulo. Faça as conexões com a alimentação desligada. Verifique a corrente máxima suportada pelo shunt e pelas trilhas da placa.

## 2. O que o firmware mede

O código em `main/main.c` realiza as seguintes operações:

1. Inicializa o barramento I²C do ESP32.
2. Configura a comunicação com o INA226 no endereço `0x40`.
3. Lê os registradores de identificação do fabricante e do dispositivo.
4. Configura o modo de conversão do sensor.
5. Aguarda a indicação de conversão pronta (`CVRF`).
6. Lê a tensão do shunt e a tensão do barramento.
7. Calcula corrente e potência.
8. Integra a corrente ao longo do tempo para estimar a carga acumulada.
9. Envia os resultados pela serial em CSV.

### Colunas da saída serial

| Campo | Unidade | Descrição |
|---|---:|---|
| `t_ms` | ms | Tempo desde o início da execução, em milissegundos |
| `vbus_V` | V | Tensão do barramento |
| `i_mA` | mA | Corrente calculada a partir da tensão do shunt |
| `p_mW` | mW | Potência calculada (`vbus_V × i_mA`) |
| `q_uAh` | µAh | Carga acumulada pela integração da corrente |

Exemplo ilustrativo:

```csv
t_ms,vbus_V,i_mA,p_mW,q_uAh
1000,5.012,125.000,626.50,34.722
1100,5.010,124.800,624.05,38.189
```

Os números acima são apenas exemplos de formato, não medições reais.

## 3. Parâmetros importantes

Os parâmetros principais estão no início de `main/main.c`:

| Constante | Valor no projeto | Função |
|---|---:|---|
| `PIN_SDA` | GPIO 21 | Pino SDA do I²C |
| `PIN_SCL` | GPIO 22 | Pino SCL do I²C |
| `I2C_FREQ_HZ` | 400000 | Frequência do barramento I²C |
| `INA_ADDR` | `0x40` | Endereço I²C do INA226 |
| `SHUNT_OHMS` | 0,1 Ω | Resistência do shunt usada no cálculo |
| `SHUNT_OFFSET_UV` | 0,0 µV | Correção de offset da tensão do shunt |
| `INA_AVG` | 0 | Configuração de média |
| `INA_VBUSCT` | 4 | Tempo de conversão da tensão do barramento |

**Importante:** `SHUNT_OHMS` deve corresponder ao valor real do resistor shunt da sua placa. Um valor incorreto causa erro proporcional na corrente calculada. O offset pode ser ajustado após uma calibração com corrente conhecida.

## 4. Requisito de configuração antes de compilar

No arquivo `main/main.c`, a constante `INA_VSHCT` aparece sem valor:

```c
#define INA_VSHCT
```

Ela é utilizada na expressão de configuração do INA226:

```c
uint16_t cfg = 0x4000 | (INA_AVG << 9) |
               (INA_VBUSCT << 6) | (INA_VSHCT << 3) | 0x07;
```

Defina um valor válido para o tempo de conversão do shunt antes de compilar. O INA226 utiliza os códigos de `0` a `7` para selecionar tempos de conversão; escolha o valor conforme a resolução e a velocidade de amostragem desejadas. Por exemplo, `4` seleciona 1,1 ms.

```c
#define INA_VSHCT 4
```

Esse valor é uma sugestão inicial, não uma calibração do hardware.

## 5. Compilação e gravação

É necessário ter o ESP-IDF instalado e o ambiente configurado no terminal.

Entre na pasta do projeto:

```bash
cd medidor_corrente
```

Ative o ambiente do ESP-IDF. Em uma instalação padrão, o comando pode ser semelhante a:

```bash
. $HOME/esp/esp-idf/export.sh
```

Ajuste o caminho caso o ESP-IDF esteja instalado em outro diretório.

Configure o alvo e compile:

```bash
idf.py set-target esp32
idf.py build
```

Conecte o ESP32 ao computador e grave o firmware:

```bash
idf.py -p /dev/ttyUSB0 flash
```

Para abrir o monitor serial:

```bash
idf.py -p /dev/ttyUSB0 monitor
```

Substitua `/dev/ttyUSB0` pela porta serial correta. Em algumas distribuições Linux, a porta pode ser `/dev/ttyACM0`. Para sair do monitor, use `Ctrl+]`.

No Windows, informe a porta correspondente, por exemplo `COM3`:

```powershell
idf.py -p COM3 flash monitor
```

## 6. Captura dos dados

O firmware imprime o cabeçalho CSV:

```text
t_ms,vbus_V,i_mA,p_mW,q_uAh
```

É possível redirecionar ou registrar a saída do monitor serial em um arquivo e depois importar os dados em um programa de análise. Garanta que mensagens de log do ESP-IDF sejam separadas das linhas CSV antes de importar.

## 7. Estrutura do projeto

```text
medidor_corrente/
├── CMakeLists.txt
├── main/
│   ├── CMakeLists.txt
│   └── main.c
├── sdkconfig
├── dependencies.lock
└── build/                 # arquivos gerados pela compilação
```

- `main/main.c`: inicialização do I²C, comunicação com o INA226, cálculos e saída serial.
- `main/CMakeLists.txt`: declaração do componente e das dependências do ESP-IDF.
- `CMakeLists.txt`: configuração principal do projeto.
- `sdkconfig`: configuração do projeto ESP-IDF.
- `build/`: arquivos temporários e binários gerados pelo processo de compilação.

## 8. Observações e limitações

- O firmware utiliza a API I²C `i2c_master` do ESP-IDF.
- O código verifica os identificadores do INA226 (`MANUF_ID` e `DIE_ID`) antes de iniciar as medições.
- Se o sensor não responder, o firmware executa uma varredura do barramento I²C para procurar dispositivos.
- `q_uAh` é calculado por integração numérica da corrente entre amostras. Corrente negativa reduz o acumulado; portanto, o campo representa carga líquida integrada, não necessariamente consumo absoluto.
- A precisão depende do resistor shunt, da montagem, da calibração, da estabilidade da alimentação e da configuração de conversão.
- O valor de tensão lido depende da ligação correta do terminal de barramento do módulo.

## 9. Possíveis melhorias

- Calibrar corrente e offset usando instrumentos de referência.
- Registrar também temperatura e condições do ensaio.
- Adicionar armazenamento local em cartão SD ou transmissão via Wi-Fi/Bluetooth.
- Criar scripts Python para visualizar corrente, tensão, potência e carga ao longo do tempo.
- Implementar limites e alarmes para sobrecorrente ou subtensão.

---

**Tecnologias:** C · ESP32 · ESP-IDF · FreeRTOS · I²C · INA226
