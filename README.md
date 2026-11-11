# Sistema Inteligente de Monitoramento e Controle de Climatização

Projeto desenvolvido para monitorar as condições ambientais de uma sala e automatizar o controle do ar-condicionado a partir das informações coletadas por dispositivos distribuídos no ambiente.

A arquitetura combina uma rede de sensores sem fio, uma central de processamento e uma câmera para monitoramento da ocupação. Os dados ambientais e de ocupação podem ser utilizados pela central para apoiar decisões de climatização, buscando maior conforto térmico e melhor utilização da energia.

## Visão geral

O sistema é organizado em módulos que trabalham em conjunto:

- **Nós sensores:** realizam a aquisição de temperatura, umidade e luminosidade. Os dados são enviados sem fio para a central por meio de módulos nRF24L01.
- **Central de processamento:** recebe as informações dos nós, coordena o funcionamento do sistema e executa a lógica de controle da climatização.
- **Controle do ar-condicionado:** utiliza um emissor infravermelho conectado à central para enviar comandos ao aparelho.
- **ESP32-CAM:** captura imagens do ambiente e executa o sistema de detecção e contagem de pessoas, fornecendo informações sobre a ocupação da sala.
- **Medidor elétrico:** utiliza o sensor INA226 para monitorar tensão, corrente, potência e carga elétrica, permitindo acompanhar grandezas relacionadas ao consumo do sistema ou de uma carga monitorada.

## Arquitetura do sistema

```text
       AMBIENTE MONITORADO
                |
       +--------+---------+
       |                  |
  Nós sensores         ESP32-CAM
  STM32 + AHT10        Detecção e
  + LDR + nRF24L01     contagem de pessoas
       |                  |
       | Rádio            | Informações
       | nRF24L01         | de ocupação
       +--------+---------+
                |
                v
       CENTRAL DE PROCESSAMENTO
              ESP32
                |
       +--------+---------+
       |                  |
 Recepção e análise    Lógica de controle
 dos dados ambientais  da climatização
       |                  |
       |                  v
       |            Emissor infravermelho
       |                  |
       |                  v
       |             Ar-condicionado
       |
       v
 Monitoramento e integração
       dos dados
```

O diagrama representa a organização funcional geral. A integração efetiva entre os módulos depende das interfaces e etapas de comunicação implementadas em cada subsistema.

## Organização dos módulos

| Diretório | Descrição |
|---|---|
| `no/` | Firmware dos nós sensores baseados em STM32F103C8T6, responsáveis pela leitura de temperatura, umidade e luminosidade e pelo envio via nRF24L01. |
| `central/` | Módulos relacionados à central de processamento e à atuação infravermelha para controle do ar-condicionado. |
| `esp32cam/contador_pessoas/` | Sistema de captura de imagens, detecção de movimento, rastreamento e contagem de pessoas com ESP32-CAM. |
| `medidor_corrente/` | Firmware para leitura de tensão, corrente, potência e carga elétrica utilizando ESP32 e INA226. |

Cada diretório possui seu próprio README com informações específicas de hardware, configuração, compilação e execução.

## Tecnologias e componentes

- **STM32F103C8T6 (Blue Pill):** microcontrolador utilizado nos nós sensores.
- **ESP32:** plataforma utilizada na central e nos módulos auxiliares.
- **ESP32-CAM:** captura de imagens e monitoramento da ocupação.
- **AHT10:** sensor de temperatura e umidade.
- **LDR:** sensor de luminosidade.
- **nRF24L01:** comunicação de rádio entre os nós sensores e a central.
- **Emissor infravermelho KY-005:** envio de comandos ao ar-condicionado.
- **INA226:** monitoramento de grandezas elétricas.
- **ESP-IDF e CMSIS:** ambientes e interfaces de desenvolvimento utilizados nos respectivos firmwares.
- **C/C++ e Python:** linguagens utilizadas nos diferentes módulos e ferramentas auxiliares.

## Funcionamento geral

1. Os nós sensores realizam leituras periódicas das condições ambientais.
2. Quando necessário, os nós transmitem as informações para a central por meio do nRF24L01.
3. A ESP32-CAM captura imagens e processa os quadros para detectar e contabilizar a movimentação de pessoas na entrada da sala.
4. A central reúne as informações disponíveis e executa a lógica de controle da climatização.
5. Os comandos de controle são enviados ao ar-condicionado por meio do emissor infravermelho.
6. O módulo INA226 pode ser utilizado para acompanhar grandezas elétricas e auxiliar na análise do funcionamento e do consumo.

## Objetivos do projeto

- Monitorar as condições ambientais do espaço em tempo real.
- Distribuir a aquisição de dados entre nós sensores de baixo consumo.
- Obter informações sobre a ocupação da sala por meio de visão computacional.
- Integrar os dados coletados em uma central de processamento.
- Automatizar o acionamento e o controle do ar-condicionado.
- Investigar estratégias de climatização que considerem as condições ambientais e a ocupação do espaço.
- Permitir o acompanhamento de grandezas elétricas relacionadas ao sistema.

## Documentação dos módulos

Para configurar, compilar ou executar um subsistema, consulte o README correspondente dentro de seu diretório:

- [Nós sensores](no/README.md)
- [Detecção e contagem de pessoas com ESP32-CAM](esp32cam/contador_pessoas/README.md)
- [Controle infravermelho](central/ir/ControleIR_ESP32/README.md)
- [Medidor de corrente com INA226](medidor_corrente/README.md)

## Observação

Este README apresenta a visão geral do projeto e a função de seus módulos. Detalhes de pinagem, dependências, configuração de ambiente, compilação, gravação e operação devem ser consultados na documentação específica de cada subsistema.
