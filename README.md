# Analisador Portátil de Baterias

Firmware desenvolvido em **ESP-IDF** para um analisador portátil de baterias capaz de controlar a descarga por meio de uma carga eletrônica, medir tensão e corrente da bateria, acompanhar a temperatura durante o ensaio e registrar os dados para análise posterior.

O projeto faz parte do Trabalho Final de Graduação em Engenharia Eletrônica:

**“Desenvolvimento de um Analisador Portátil com Front-End Analógico para Perfilagem de Descarga e Estimativa de Autonomia de Baterias”**

---

## Objetivo

O sistema foi desenvolvido para permitir a caracterização experimental de baterias por meio de ensaios de descarga controlada.

A plataforma permite:

- medir a tensão da bateria;
- medir a corrente de descarga;
- controlar a corrente por meio do DAC do ESP32;
- realizar descarga aproximadamente constante;
- medir temperatura e umidade durante o ensaio;
- registrar periodicamente as variáveis do sistema;
- calcular potência instantânea;
- integrar a corrente para obtenção de capacidade em Ah;
- integrar a potência para obtenção de energia em Wh;
- gerar dados para construção das curvas de descarga e posterior estimativa de autonomia.

Os ensaios previstos para a bateria utilizada no projeto incluem:

| Taxa de descarga | Corrente prevista |
|---|---:|
| 0,5C | 2,5 A |
| 1C | 5 A |
| 2C | 10 A |

Durante a etapa atual de desenvolvimento, os ensaios são iniciados com correntes menores, principalmente **1 A**, para validação do hardware, calibração das medições e avaliação da estabilidade da carga eletrônica.

---

## Arquitetura do sistema

O analisador é composto por quatro blocos principais:

```text
                         +--------------------+
                         |       ESP32        |
                         |                    |
                         | ADC          DAC   |
                         +--^------------|-----+
                            |            |
                            |            v
                            |          Vref
                            |            |
                            |       +----v----+
                            |       | LM358   |
                            |       | controle|
                            |       +----|----+
                            |            |
                            |            v
Bateria + ------------------+--------> MOSFETs
     |                                  |
     |                                  |
     |                                  v
     |                                Shunt
     |                                  |
     +----> Medição de tensão           +----> Medição de corrente
                                        |
                                       GND
```

O ESP32 atua como unidade de aquisição, controle e registro.

A carga eletrônica utiliza MOSFETs operando na região linear. O amplificador operacional compara a referência gerada pelo DAC com a tensão medida no resistor shunt e ajusta a condução dos MOSFETs para regular a corrente.

---

## Microcontrolador

O firmware utiliza um **ESP32** programado em C com ESP-IDF.

Os principais recursos utilizados são:

- ADC1 para leitura de tensão;
- ADC1 para leitura de corrente;
- DAC interno para geração da referência de corrente;
- GPIO para comunicação com o DHT22;
- FreeRTOS para execução periódica das tarefas;
- SPIFFS para armazenamento dos resultados dos ensaios.

### Mapeamento atual de GPIOs

| Função | GPIO |
|---|---:|
| Medição da tensão da bateria | GPIO34 |
| Medição da corrente | GPIO35 |
| DAC / referência de corrente | GPIO25 |
| DHT22 | GPIO33 |

GPIO34 e GPIO35 são utilizados apenas como entradas analógicas.

---

# Medição de tensão

A tensão da bateria é reduzida antes de chegar ao ADC do ESP32 por meio de um divisor resistivo.

Valores utilizados:

```text
R1 = 68 kΩ
R2 = 12 kΩ
```

A relação do divisor é:

```text
VADC = Vbat × R2 / (R1 + R2)
```

Portanto:

```text
VADC = Vbat × 12 / 80
```

ou:

```text
VADC = 0,15 × Vbat
```

A tensão real é recuperada pelo firmware utilizando:

```text
Vbat = VADC × 6,6667
```

Para uma bateria com aproximadamente 12,6 V:

```text
VADC ≈ 1,89 V
```

mantendo a entrada do ESP32 em uma faixa adequada.

O canal também possui filtragem RC antes da entrada do ADC.

---

# Medição de corrente

A corrente é determinada a partir da queda de tensão em um resistor shunt.

Na configuração atual:

```text
Rshunt = 0,2 Ω
```

A tensão desenvolvida no shunt é:

```text
Vshunt = I × Rshunt
```

Assim:

| Corrente | Tensão no shunt |
|---:|---:|
| 1 A | 0,20 V |
| 2,5 A | 0,50 V |
| 5 A | 1,00 V |
| 10 A | 2,00 V |

O sinal é condicionado pelo segundo amplificador do LM358 antes de chegar ao GPIO35.

A configuração atual utiliza:

```text
R5 = 10 kΩ
R6 = 1 kΩ
```

para um amplificador não inversor.

O ganho é:

```text
G = 1 + R6/R5
```

resultando em:

```text
G = 1,1
```

Logo:

```text
VGPIO35 ≈ 1,1 × Vshunt
```

A corrente é calculada pelo firmware por:

```text
I = VGPIO35 / (G × Rshunt)
```

ou, para os valores atuais:

```text
I = VGPIO35 / 0,22
```

---

# Controle da corrente

O controle da descarga utiliza o primeiro amplificador do LM358.

O ESP32 gera uma tensão analógica pelo DAC disponível no GPIO25. Essa tensão é filtrada e utilizada como referência da malha analógica.

A malha procura manter:

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

Para:

```text
Rshunt = 0,2 Ω
```

os pontos principais são:

| Corrente desejada | Vref aproximado |
|---:|---:|
| 1 A | 0,20 V |
| 2,5 A | 0,50 V |
| 5 A | 1,00 V |
| 10 A | 2,00 V |

O firmware calcula inicialmente o valor aproximado do DAC a partir da corrente desejada e posteriormente realiza uma correção incremental com base na corrente efetivamente medida.

A arquitetura possui, portanto, duas escalas de atuação:

1. **malha analógica rápida**, formada pelo LM358, MOSFETs e shunt;
2. **correção digital lenta**, executada periodicamente pelo ESP32.

Essa organização permite que a dinâmica rápida do circuito permaneça no domínio analógico enquanto o microcontrolador realiza a supervisão e correção da corrente.

---

# Temperatura e umidade

O sistema utiliza um sensor **DHT22** conectado ao GPIO33.

O módulo fornece:

- temperatura ambiente;
- umidade relativa.

A leitura ocorre em uma frequência menor que a malha principal de controle devido às características de atualização do sensor.

Essas informações são registradas junto com as grandezas elétricas para permitir avaliar as condições ambientais durante os ensaios.

---

# Registro dos ensaios em CSV

O firmware possui um componente dedicado chamado:

```text
csv_logger
```

responsável por registrar os resultados dos ensaios na memória flash interna do ESP32.

O armazenamento utiliza **SPIFFS**.

Cada novo ensaio recebe automaticamente um arquivo independente:

```text
/spiffs/ensaio_001.csv
/spiffs/ensaio_002.csv
/spiffs/ensaio_003.csv
...
```

Isso evita a sobrescrita automática dos ensaios anteriores.

Na configuração atual, os dados principais são registrados aproximadamente a cada:

```text
500 ms
```

correspondendo a cerca de:

```text
2 amostras/s
```

---

## Variáveis registradas

O CSV contém:

| Coluna | Descrição |
|---|---|
| `tempo_s` | Tempo decorrido desde o início do ensaio |
| `tensao_bateria_V` | Tensão medida da bateria |
| `corrente_A` | Corrente de descarga medida |
| `corrente_ref_A` | Referência utilizada pelo controlador |
| `potencia_W` | Potência instantânea |
| `temperatura_C` | Temperatura medida |
| `umidade_pct` | Umidade relativa |
| `DAC` | Valor digital aplicado ao DAC |
| `Vdac_V` | Tensão estimada na saída do DAC |
| `Vref_V` | Tensão de referência da malha |
| `carga_Ah` | Capacidade acumulada |
| `energia_Wh` | Energia acumulada |

Exemplo:

```text
tempo_s;tensao_bateria_V;corrente_A;corrente_ref_A;potencia_W;temperatura_C;umidade_pct;DAC;Vdac_V;Vref_V;carga_Ah;energia_Wh
0,500;12,4210;0,9980;1,0000;12,3962;25,10;61,80;16;0,2058;0,2058;0,000000;0,000000
1,000;12,4170;1,0030;1,0000;12,4543;25,10;61,80;16;0,2058;0,2058;0,000139;0,001726
```

O arquivo utiliza:

```text
;  como separador de colunas
,  como separador decimal
```

facilitando a abertura em instalações do Excel configuradas em português do Brasil.

---

# Potência, capacidade e energia

A potência instantânea é calculada por:

```text
P = Vbat × Ibat
```

O firmware também integra os valores obtidos ao longo do tempo para calcular a carga retirada da bateria:

```text
Q = ∫ I dt
```

expressa em:

```text
Ah
```

e a energia fornecida:

```text
E = ∫ P dt
```

expressa em:

```text
Wh
```

Na implementação atual, a integração entre duas amostras consecutivas utiliza a regra trapezoidal.

Essas informações permitem obter experimentalmente:

- capacidade efetivamente fornecida pela bateria;
- energia efetivamente fornecida;
- comportamento da tensão ao longo da descarga;
- influência da corrente sobre a capacidade disponível;
- comparação entre diferentes taxas C.

---

# Estrutura do firmware

O projeto é dividido em componentes independentes:

```text
leitura_bateria/
│
├── CMakeLists.txt
├── partitions.csv
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
    ├── temperature_sensor/
    │   ├── CMakeLists.txt
    │   ├── temperature_sensor.c
    │   └── include/
    │       └── temperature_sensor.h
    │
    └── csv_logger/
        ├── CMakeLists.txt
        ├── csv_logger.c
        └── include/
            └── csv_logger.h
```

---

# Componentes de software

## `battery_voltage`

Responsável por:

- configuração do ADC1;
- aquisição do GPIO34;
- calibração do ADC;
- compensação do divisor resistivo;
- cálculo da tensão real da bateria.

---

## `current_baterry`

Responsável por:

- aquisição do GPIO35;
- calibração do ADC;
- compensação do ganho analógico;
- determinação da tensão no shunt;
- conversão da tensão para corrente.

> O nome `current_baterry` é mantido atualmente por compatibilidade com a estrutura existente do projeto.

---

## `current_control`

Responsável por:

- configuração do DAC no GPIO25;
- definição da corrente desejada;
- cálculo inicial do DAC;
- correção incremental da corrente;
- limitação da saída;
- desligamento da carga eletrônica.

---

## `temperature_sensor`

Responsável por:

- comunicação com o DHT22;
- leitura de temperatura;
- leitura de umidade;
- validação das amostras recebidas.

---

## `csv_logger`

Responsável por:

- montagem do SPIFFS;
- criação automática de um novo arquivo de ensaio;
- registro periódico das variáveis;
- cálculo de potência;
- integração de Ah;
- integração de Wh;
- realização de `flush` periódico;
- armazenamento persistente dos resultados.

---

## `main`

O arquivo principal coordena os demais módulos.

O fluxo básico é:

```text
Inicialização
      |
      v
Medição de corrente
      |
      v
Correção da corrente
      |
      v
Leitura de temperatura
      |
      v
Leitura da tensão
      |
      v
Registro no CSV
      |
      v
Exibição no Serial Monitor
      |
      +------> repete
```

---

# Armazenamento SPIFFS

O `csv_logger` utiliza uma partição específica da memória flash chamada:

```text
storage
```

definida na tabela de partições do projeto.

Essa região é montada em:

```text
/spiffs
```

durante a inicialização.

O uso de uma partição dedicada permite manter os resultados armazenados independentemente do restante da aplicação enquanto a flash não for apagada ou reformatada.

---

# Ambiente de desenvolvimento

O projeto é desenvolvido utilizando:

- Visual Studio Code;
- ESP-IDF;
- extensão ESP-IDF para VS Code;
- linguagem C;
- FreeRTOS;
- ESP32.

---

# Compilação

Com o ambiente ESP-IDF configurado, execute na raiz do projeto:

```bash
idf.py build
```

Para gravar:

```bash
idf.py flash
```

Para abrir o monitor serial:

```bash
idf.py monitor
```

Ou:

```bash
idf.py flash monitor
```

---

# Monitoramento durante o ensaio

Durante a execução, o Serial Monitor apresenta informações semelhantes a:

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

Esses valores permitem verificar em tempo real:

- comportamento da bateria;
- estabilidade da corrente;
- ação do controlador;
- tensão de referência;
- temperatura durante o ensaio.

O arquivo CSV é armazenado paralelamente na flash.

---

# Segurança

O circuito funciona como uma **carga eletrônica de potência**.

A potência aproximadamente retirada da bateria é:

```text
P = Vbat × I
```

Para uma bateria próxima de 12 V:

| Corrente | Potência aproximada |
|---:|---:|
| 1 A | 12 W |
| 2,5 A | 30 W |
| 5 A | 60 W |
| 10 A | 120 W |

O resistor shunt também dissipa:

```text
Pshunt = I² × Rshunt
```

Com:

```text
Rshunt = 0,2 Ω
```

temos:

| Corrente | Potência no shunt |
|---:|---:|
| 1 A | 0,2 W |
| 2,5 A | 1,25 W |
| 5 A | 5 W |
| 10 A | 20 W |

Ensaios em correntes maiores exigem avaliação adequada de:

- potência nominal do shunt;
- dissipação dos MOSFETs;
- região de operação segura dos transistores;
- dissipador;
- ventilação;
- cabos;
- conectores;
- trilhas;
- proteção contra sobrecorrente;
- proteção contra subtensão da bateria;
- temperatura da bateria e do estágio de potência.

Os ensaios devem ser validados progressivamente, começando com correntes reduzidas.

---

# Estado atual

Atualmente estão implementados:

- aquisição da tensão da bateria;
- aquisição da corrente;
- calibração dos ADCs;
- controle da corrente pelo DAC;
- malha de correção da corrente;
- leitura do DHT22;
- monitoramento pelo Serial Monitor;
- registro dos ensaios em CSV;
- armazenamento em SPIFFS;
- cálculo da potência instantânea;
- cálculo de capacidade acumulada em Ah;
- cálculo de energia acumulada em Wh.

A etapa atual concentra-se na **validação experimental do protótipo e das medições** antes da execução dos ensaios completos de descarga.

---

# Próximos desenvolvimentos

As próximas etapas previstas incluem:

- implementação de tensão mínima de corte;
- encerramento automático da descarga;
- combinação de critérios de tensão, temperatura e tempo máximo;
- automatização da transferência dos arquivos CSV para o computador;
- calibração experimental dos canais de tensão e corrente;
- avaliação do erro de medição;
- ensaios completos em 0,5C, 1C e 2C;
- obtenção das curvas de descarga;
- comparação da capacidade obtida em diferentes taxas;
- comparação da energia fornecida em diferentes condições;
- análise da influência da taxa de descarga;
- ajuste de modelos de autonomia;
- comparação dos resultados experimentais com modelos encontrados na literatura;
- desenvolvimento de uma interface para análise dos resultados.

---

# Contexto acadêmico

Projeto desenvolvido como Trabalho Final de Graduação do curso de Engenharia Eletrônica da **Universidade Federal de Itajubá — UNIFEI**.

**Discente:** Arthur Sacramento Pamplona  
**Orientador:** Prof. Dr. Robson Bauwelz Gonzatti

O objetivo final é desenvolver uma plataforma experimental capaz de realizar a perfilagem de descarga de baterias e utilizar os dados adquiridos para avaliar capacidade, energia disponível e autonomia em diferentes condições de operação.

---

# Status do projeto

**Em desenvolvimento e validação experimental.**

A arquitetura de aquisição, controle e registro já está implementada. Os próximos trabalhos concentram-se na validação metrológica do sistema, proteção da bateria e execução dos ensaios completos de descarga.
