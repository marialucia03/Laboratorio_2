# Tetris Simplificado - ESP32 + Matriz LED 8x8 Bicolor

Laboratorio 2 - Sistemas Embebidos  
Universidad EIA - 2026-1  
Profesor: Juan José Londoño

## Descripción

Videojuego Tetris simplificado (sin rotación de piezas) implementado en un ESP32 con una matriz LED 8x8 bicolor SBM-2388ASRG y 2 registros de desplazamiento 74HC595.

### Características
- 5 tipos de piezas: línea (1x3), cuadrado (2x2), L, L invertida, T
- Sin rotación de piezas
- Colores: verde (pieza activa), rojo (piezas colocadas), amarillo (flash al eliminar línea)
- Velocidad de caída aumenta progresivamente
- Animación de game over
- Reinicio con cualquier botón

## Hardware

### Componentes
| Componente | Cantidad | Función |
|---|---|---|
| ESP32-WROOM (30 pines) | 1 | Microcontrolador |
| Matriz LED 8x8 SBM-2388ASRG | 1 | Display bicolor |
| 74HC595 (shift register) | 2 | Manejar 16 columnas con 3 GPIOs |
| Resistencia 100Ω | 8 | Limitar corriente en filas |
| Resistencia 10kΩ | 3 | Pull-down para botones |
| Pulsador | 3 | Controles del juego |
| Condensador 100nF | 2 | Desacoplo para cada 595 |

### Circuito - Conexiones ESP32

#### Filas de la matriz (ánodo común) → Resistencia 100Ω → Pin de la matriz
| GPIO ESP32 | Fila | Pin matriz |
|------------|------|------------|
| GPIO 4     | ROW1 | 9          |
| GPIO 16    | ROW2 | 14         |
| GPIO 17    | ROW3 | 8          |
| GPIO 18    | ROW4 | 12         |
| GPIO 19    | ROW5 | 1          |
| GPIO 21    | ROW6 | 7          |
| GPIO 22    | ROW7 | 2          |
| GPIO 23    | ROW8 | 5          |

#### 74HC595 - Control desde ESP32
| GPIO ESP32 | Señal | Pin 595 #1 | Descripción |
|------------|-------|------------|-------------|
| GPIO 13    | DATA  | Pin 14 (DS)   | Datos serie |
| GPIO 14    | CLOCK | Pin 11 (SHCP) | Reloj de desplazamiento |
| GPIO 27    | LATCH | Pin 12 (STCP) | Transferir a salidas |

#### Cableado de los 74HC595

Los dos 595 van en cascada (daisy chain):

**595 #1 (columnas verdes):**
| Pin 595 #1 | Conexión |
|------------|----------|
| Pin 14 (DS)   | GPIO 13 del ESP32 |
| Pin 11 (SHCP) | GPIO 14 del ESP32 |
| Pin 12 (STCP) | GPIO 27 del ESP32 |
| Pin 13 (OE)   | GND (siempre habilitado) |
| Pin 10 (MR)   | 3.3V (nunca resetear) |
| Pin 16 (VCC)  | 3.3V |
| Pin 8 (GND)   | GND |
| Pin 9 (Q7S)   | Pin 14 (DS) del 595 #2 |
| Q0-Q7         | Cátodos verdes CG1-CG8 (pines 24,23,22,21,20,19,18,17 de la matriz) |

**595 #2 (columnas rojas):**
| Pin 595 #2 | Conexión |
|------------|----------|
| Pin 14 (DS)   | Pin 9 (Q7S) del 595 #1 |
| Pin 11 (SHCP) | GPIO 14 del ESP32 (mismo clock) |
| Pin 12 (STCP) | GPIO 27 del ESP32 (mismo latch) |
| Pin 13 (OE)   | GND |
| Pin 10 (MR)   | 3.3V |
| Pin 16 (VCC)  | 3.3V |
| Pin 8 (GND)   | GND |
| Q0-Q7         | Cátodos rojos CR1-CR8 (pines 13,3,4,10,6,11,15,16 de la matriz) |

**Condensadores de desacoplo:** 100nF entre VCC y GND de cada 595, lo más cerca posible del chip.

#### Botones (con pull-down externo de 10kΩ)
| GPIO ESP32 | Función |
|------------|---------|
| GPIO 36 (VP) | Mover izquierda |
| GPIO 39 (VN) | Mover derecha |
| GPIO 34       | Hard drop |

Conexión de cada botón: un terminal a 3.3V, el otro terminal al GPIO + resistencia 10kΩ a GND.

## Software

### Requisitos
- ESP-IDF (v4.4 o superior)

### Compilar y flashear
```bash
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### Estructura del proyecto
```
tetris-esp32/
├── CMakeLists.txt
├── README.md
├── .gitignore
└── main/
    ├── CMakeLists.txt
    └── main.c
```

### APIs de ESP-IDF utilizadas
- `driver/gpio.h`: gpio_config(), gpio_set_level(), interrupciones GPIO con gpio_isr_handler_add()
- `driver/timer.h`: timer hardware con alarma e ISR para caída periódica
- `freertos/FreeRTOS.h` y `freertos/task.h`: soporte del RTOS
- `esp_timer.h`: esp_timer_get_time() para debounce en ISR

### Técnicas implementadas
- **74HC595 sin librería**: controlado bit a bit con gpio_set_level() (DATA, CLOCK, LATCH)
- **Multiplexado por filas** de la matriz LED (~500 Hz refresh)
- **Interrupciones externas** (GPIO ISR) para los botones con debounce por software
- **Timer hardware** con alarma e ISR para la caída periódica de las piezas
- **Variables volatile** para comunicación ISR ↔ bucle principal
- **IRAM_ATTR** en todas las ISR

## Controles

| Botón     | Acción |
|-----------|--------|
| Izquierda | Mover pieza a la izquierda |
| Derecha   | Mover pieza a la derecha |
| Drop      | Bajar pieza instantáneamente |
| Cualquiera| Reiniciar (en game over) |