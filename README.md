# Analisador Portátil de Baterias

Firmware desenvolvido em **ESP-IDF** para um analisador portátil de baterias, capaz de controlar a descarga por meio de uma carga eletrônica, medir tensão e corrente da bateria e acompanhar a temperatura durante os ensaios.

O projeto faz parte do Trabalho Final de Graduação em Engenharia Eletrônica:

**“Desenvolvimento de um Analisador Portátil com Front-End Analógico para Perfilagem de Descarga e Estimativa de Autonomia de Baterias”**

## Objetivo

O sistema foi desenvolvido para permitir a caracterização experimental de baterias por meio de ensaios de descarga controlada.

A arquitetura permite:

- medir a tensão da bateria;
- medir a corrente de descarga;
- controlar a corrente por meio do DAC do ESP32;
- realizar descarga aproximadamente constante;
- medir temperatura durante o ensaio;
- adquirir os dados periodicamente;
- futuramente calcular capacidade, energia entregue e estimativas de autonomia.

Os ensaios previstos incluem diferentes taxas de descarga, com destaque para:

| Taxa | Corrente prevista |
|---|---:|
| 0,5C | 2,5 A |
| 1C | 5 A |
| 2C | 10 A |

Durante a etapa atual de desenvolvimento, os ensaios iniciais são realizados em correntes menores, principalmente **1 A**, para validação do hardware e do firmware.

---

## Arquitetura do sistema

O sistema é dividido em quatro blocos principais:

```text
Bateria
   |
   +----------------------> Medição de tensão
   |
   +--> MOSFETs --> Shunt --> GND
          ^           |
          |           +------> Medição de corrente
          |
          +------------------> Controle de corrente
                                ^
                                |
                              ESP32
```

O ESP32 atua como unidade de aquisição e controle.

A malha de descarga funciona a partir de uma referência analógica gerada pelo DAC. O amplificador operacional compara essa referência com a tensão no resistor shunt e ajusta os MOSFETs para regular a corrente.

---

## Microcontrolador

O firmware utiliza um **ESP32** com ESP-IDF.

Principais periféricos utilizados:

- ADC para tensão;
- ADC para corrente;
- DAC para referência da carga eletrônica;
- GPIO para sensor de temperatura;
- FreeRTOS para execução periódica da aquisição e controle.

### Mapeamento atual de GPIOs

| Função | GPIO |
|---|---:|
| Medição da tensão da bateria | GPIO34 |
| Medição da corrente | GPIO35 |
| DAC / referência de corrente | GPIO25 |
| DHT22 | GPIO33 |

GPIO34 e GPIO35 são utilizados apenas como entradas analógicas.

---

## Medição de tensão

A tensão da bateria é reduzida por um divisor resistivo antes de chegar ao ADC do ESP32.

Valores utilizados:

```text
R1 = 68 kΩ
R2 = 12 kΩ
```

A relação do divisor é:

```text
VADC = Vbat × 12 / (68 + 12)
```

ou:

```text
VADC = 0,15 × Vbat
```

Assim, uma bateria de aproximadamente 12,6 V produz cerca de:

```text
VADC ≈ 1,89 V
```

na entrada do ESP32.

Após o divisor há também um filtro RC antes do ADC.

---

## Medição de corrente

A corrente é determinada pela queda de tensão em um resistor shunt.

A configuração em desenvolvimento utiliza:

```text
Rshunt = 0,2 Ω
```

A tensão sobre o shunt é:

```text
Vshunt = I × Rshunt
```

Exemplos:

| Corrente | Tensão no shunt |
|---:|---:|
| 1 A | 0,20 V |
| 2,5 A | 0,50 V |
| 5 A | 1,00 V |
| 10 A | 2,00 V |

O sinal é condicionado por um dos amplificadores do LM358 antes de ser conectado ao GPIO35.

Na configuração atual, o estágio é não inversor com:

```text
R5 = 10 kΩ
R6 = 1 kΩ
```

resultando em:

```text
G = 1 + R6/R5
G = 1,1
```

Portanto:

```text
VGPIO35 ≈ 1,1 × Vshunt
```

---

## Controle da corrente

O controle utiliza o primeiro amplificador do LM358.

O ESP32 gera uma tensão analógica por meio do DAC disponível no GPIO25.

A referência passa por um filtro RC e é aplicada à entrada não inversora do amplificador operacional.

A malha analógica procura manter:

```text
Vshunt ≈ Vref
```

Como:

```text
Vshunt = I × Rshunt
```

temos:

```text
I ≈ Vref / Rshunt
```

Para um shunt de 0,2 Ω:

| Corrente | Vref aproximado |
|---:|---:|
| 1 A | 0,20 V |
| 2,5 A | 0,50 V |
| 5 A | 1,00 V |
| 10 A | 2,00 V |

O firmware também utiliza a corrente medida para realizar uma correção incremental do DAC.

A estrutura possui, portanto, duas escalas de controle:

1. **malha analógica rápida**, implementada pelo LM358 e pelos MOSFETs;
2. **correção digital lenta**, executada pelo ESP32.

---

## Temperatura

O sistema utiliza um sensor **DHT22** conectado ao GPIO33.

O módulo permite obter:

- temperatura;
- umidade relativa.

A leitura é realizada em intervalo maior que a malha de controle, respeitando o tempo de atualização do sensor.

Para aplicações futuras, a medição de temperatura poderá ser substituída ou complementada por sensores diretamente acoplados às células da bateria.

---

## Estrutura do firmware

O projeto utiliza componentes separados para cada função:

```text
leitura_bateria/
│
├── CMakeLists.txt
│
├── main/
│   ├── CMakeLists.txt
│   └── main.c
│
└── components/
    │
    ├── battery_voltage/
    │   ├── CMakeLists.txt
    │   ├── voltage.c
    │   └── include/
    │       └── voltage.h
    │
    ├── current_baterry/
    │   ├── CMakeLists.txt
    │   ├── current_baterry.c
    │   └── include/
    │       └── current_baterry.h
    │
    ├── current_control/
    │   ├── CMakeLists.txt
    │   ├── current_control.c
    │   └── include/
    │       └── current_control.h
    │
    └── temperature_sensor/
        ├── CMakeLists.txt
        ├── temperature_sensor.c
        └── include/
            └── temperature_sensor.h
```

### `battery_voltage`

Responsável por:

- configuração do ADC1;
- aquisição do GPIO34;
- calibração do ADC;
- compensação do divisor resistivo;
- cálculo da tensão real da bateria.

### `current_baterry`

Responsável por:

- aquisição do GPIO35;
- calibração do ADC;
- compensação do ganho analógico;
- cálculo da tensão do shunt;
- conversão da tensão para corrente.

### `current_control`

Responsável por:

- configuração do DAC do GPIO25;
- definição da corrente de referência;
- cálculo inicial do DAC;
- correção incremental da corrente;
- limitação da saída do DAC;
- desligamento da carga eletrônica.

### `temperature_sensor`

Responsável pela comunicação com o DHT22 e pela leitura de temperatura e umidade.

### `main`

Coordena os diferentes módulos e executa periodicamente:

```text
leitura da corrente
        ↓
controle da corrente
        ↓
leitura de temperatura
        ↓
leitura da tensão
        ↓
exibição dos resultados
```

---

## Ambiente de desenvolvimento

O projeto é desenvolvido utilizando:

- Visual Studio Code;
- ESP-IDF;
- extensão ESP-IDF para VS Code;
- linguagem C;
- FreeRTOS;
- ESP32.

---

## Compilação

Com o ambiente ESP-IDF configurado, na raiz do projeto:

```bash
idf.py build
```

Para gravar o firmware:

```bash
idf.py flash
```

Para abrir o monitor serial:

```bash
idf.py monitor
```

Também é possível executar:

```bash
idf.py flash monitor
```

---

## Saída no monitor serial

Durante o funcionamento, o firmware apresenta informações semelhantes a:

```text
Vbat = 12.410 V |
Ibat = 1.002 A |
Iref = 1.000 A |
Temp = 25.4 C |
UR = 61.8 % |
DAC = 16 |
Vdac ~= 0.206 V |
Vref ~= 0.206 V
```

Esses valores permitem acompanhar simultaneamente a condição da bateria e o comportamento da malha de controle.

---

## Segurança

O circuito funciona como uma **carga eletrônica**, podendo dissipar potência significativa.

A potência aproximada retirada da bateria é:

```text
P = Vbat × I
```

Em uma bateria de 12 V:

| Corrente | Potência aproximada |
|---:|---:|
| 1 A | 12 W |
| 2,5 A | 30 W |
| 5 A | 60 W |
| 10 A | 120 W |

Portanto, ensaios em correntes elevadas exigem avaliação adequada de:

- dissipação térmica dos MOSFETs;
- região de operação segura dos transistores;
- potência do resistor shunt;
- dissipador;
- conexões e cabos;
- proteção contra sobrecorrente;
- proteção contra subtensão da bateria;
- temperatura da bateria e do estágio de potência.

Os testes devem ser realizados inicialmente com correntes reduzidas.

---

## Próximos desenvolvimentos

Entre as próximas etapas previstas estão:

- armazenamento das amostras dos ensaios;
- exportação em CSV;
- cálculo automático de Ah;
- cálculo automático de Wh;
- implementação de tensão de corte;
- desligamento automático ao final da descarga;
- ensaios em 0,5C, 1C e 2C;
- obtenção das curvas de descarga;
- comparação entre diferentes correntes;
- ajuste de modelos de autonomia;
- avaliação da influência da taxa de descarga;
- desenvolvimento de interface para análise dos resultados.

---

## Contexto acadêmico

Este projeto é desenvolvido como Trabalho Final de Graduação do curso de Engenharia Eletrônica da Universidade Federal de Itajubá — UNIFEI.

**Discente:** Arthur Sacramento Pamplona  
**Orientador:** Prof. Dr. Robson Bauwelz Gonzatti

O objetivo final é desenvolver uma plataforma experimental capaz de realizar a perfilagem de descarga de baterias e utilizar os dados obtidos para estimar capacidade disponível e autonomia em diferentes condições de operação.

---

## Status

O projeto encontra-se em desenvolvimento.

A arquitetura de aquisição e controle já foi implementada e está sendo validada experimentalmente em bancada antes da execução dos ensaios completos de descarga.
