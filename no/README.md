# WSN Environment Monitoring Node

Este repositorio contem o firmware para um no sensor sem fio (WSN) de baixo consumo de energia, baseado no microcontrolador STM32F103C8T6 (Blue Pill / AWCMCU). O no coleta dados de temperatura, umidade e luminosidade, transmitindo-os via radio nRF24L01 para um gateway central.

## Principais Caracteristicas

* Protocolo TEEN / APTEEN: O no utiliza logica baseada em eventos. A transmissao de dados so ocorre se houver uma mudanca significativa no ambiente (ultrapassando os limiares configurados) ou apos um tempo maximo pre-definido (Heartbeat).
* Ultra-Low Power: O microcontrolador entra em modo Stop (dezenas de microamperes) entre as leituras. 
* Clock Otimizado: O sistema opera com o oscilador interno (HSI) a 8 MHz, eliminando o tempo de estabilizacao do cristal externo (HSE) e reduzindo drasticamente o consumo em modo ativo.
* Espera Inteligente (WFI): Substituicao de loops de bloqueio (NOP) por instrucoes Wait For Interrupt (WFI) baseadas no SysTick para as esperas do sensor I2C e timeouts do radio.

## Hardware Utilizado

* Microcontrolador: STM32F103C8T6 (Placa Blue Pill ou AWCMCU)
* Radio RF: Modulo nRF24L01 (2.4 GHz)
* Sensor T/H: AHT10 (I2C)
* Sensor de Luz: LDR acoplado a um divisor de tensao (ADC)

## Pinagem (Pinout)

### nRF24L01 (SPI1)
* CE   -> PA4
* CSN  -> PA3
* SCK  -> PA5
* MISO -> PA6
* MOSI -> PA7

### AHT10 (I2C2)
* SCL  -> PB10
* SDA  -> PB11

### LDR (ADC1)
* OUT  -> PB0 (Canal 8)

### UART de Diagnostico (USART2)
* TX   -> PA2 (115200 bps)

## Estrutura do Codigo

O projeto foi desenvolvido em C puro (Bare-metal / CMSIS), acessando diretamente os registradores para maxima otimizacao.

* main.c: Loop principal, configuracao do clock (HSI 8MHz), inicializacao dos perifericos e logica de avaliacao TEEN/APTEEN.
* nrf24.c: Driver customizado para o modulo nRF24L01, focado em ligar o radio estritamente o tempo necessario para a transmissao (sem RX ativo desnecessariamente).
* sensors.c: Leitura I2C para o AHT10 e configuracao do ADC manual para o LDR, utilizando WFI para economizar energia durante a conversao dos sensores.
* sleep.c: Gerenciamento de energia, configuracao do RTC (Real Time Clock) via LSI para acordar o microcontrolador via alarme, e preparacao dos pinos GPIO para evitar fugas de corrente (leakage).

## Configuracao de Consumo e Debug (MUITO IMPORTANTE)

Para evitar que a interface de debug (ST-Link) perca conexao quando o chip entra em modo Stop, o codigo possui a flag DEV_KEEP_DEBUG no arquivo main.c.

#define DEV_KEEP_DEBUG 0

* DEV_KEEP_DEBUG = 1: Mantem o modulo de debug do Cortex-M3 ativo durante o Stop. O consumo ficara travado em torno de 1.5 a 2.0 mA. Utilize APENAS durante o desenvolvimento de software.
* DEV_KEEP_DEBUG = 0: Desliga a interface de debug no modo Stop, permitindo atingir o consumo real na casa dos microamperes. 

Como medir a corrente real (Microamperes):
1. Defina DEV_KEEP_DEBUG 0 e grave o firmware.
2. Desconecte FISICAMENTE todos os cabos do ST-Link da placa.
3. Desligue a alimentacao da placa por 3 a 5 segundos e religue (Power Cycle). O botao de reset nao e suficiente para limpar o registrador de debug.
4. Conecte o multimetro em serie.

Nota: Para placas AWCMCU/Blue Pill, remova o LED de energia (Power LED) da placa para evitar o consumo parasita constante do proprio LED.

## Compilacao

O projeto esta configurado para ser compilado utilizando o STM32CubeIDE com o toolchain GCC ARM. Importe a pasta raiz do projeto na sua workspace, compile e grave utilizando um ST-Link V2.