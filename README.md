# Tetris — ESP32 en Matriz LED 7×5

Implementación de un Tetris simplificado corriendo sobre un ESP32 DevKit, usando una matriz de LEDs bicolor (rojo/verde) de 7 filas × 5 columnas, controlada por multiplexación por filas.

## Hardware

### Componentes

- ESP32 
- Matriz LED bicolor 7×5 (ánodo común por fila)
- 3 pulsadores (izquierda, derecha, drop)
- Resistencias limitadoras según los LEDs utilizados
- Protoboard y cableado

### Conexiones

| Función | GPIOs |
|---|---|
| Filas (ánodos, 7) | 16, 17, 18, 19, 21, 22, 23 |
| Columnas rojas (5) | 27, 26, 25, 33, 5 |
| Columnas verdes (4) | 4, 15, 2, 32 |
| Botón izquierda | 13 (pull-up interno) |
| Botón derecha | 14 (pull-up interno) |
| Botón drop | 12 (pull-up interno) |

> Los LEDs verdes solo están disponibles en las primeras 4 columnas. La columna 5 solo tiene rojo.

### Lógica de activación

- **Filas:** se activan en alto (1 = encendida).
- **Columnas:** activas en bajo (0 = LED encendido). Se apagan con 1.
- Se multiplexa fila por fila con un retardo de 700 µs por fila.

## Piezas

El juego incluye 5 tipos de pieza, cada una definida por sus celdas relativas a la posición `(y, x)`:

```
Tipo 0 (T)     Tipo 1 (cuadrado)   Tipo 2 (línea)   Tipo 3 (L izq)   Tipo 4 (L der)

   . X .            X X                  X               X .              . X
   X X X            X X                  X               X X              X X
                                         X               X .              . X
```

Las piezas no rotan; solo se mueven lateralmente y caen.

## Controles

| Botón | Acción |
|---|---|
| Izquierda (GPIO 13) | Mover pieza a la izquierda |
| Derecha (GPIO 14) | Mover pieza a la derecha |
| Drop (GPIO 12) | Acelerar la caída (mantener presionado) |

Los botones se leen por polling dentro del loop principal. Se usa un contador de movimiento (`contador_mov > 20`) para evitar que la pieza se desplace demasiado rápido al mantener presionado un botón lateral.

## Mecánicas

- Las piezas aparecen en la parte superior (y = −3) y caen automáticamente.
- Al colisionar con el fondo o con piezas fijadas, la pieza se ancla al tablero.
- Si una fila queda completamente llena, se reproduce una animación de parpadeo en verde y luego se elimina, aplicando gravedad bloque a bloque hacia abajo.
- La condición de **Game Over** se evalúa al inicio de cada nueva pieza: si hay al menos una celda ocupada en la fila 0, se muestra una animación de dos "X" verdes parpadeantes y el tablero se reinicia.

## Estructura del código

| Sección | Descripción |
|---|---|
| `configurar_pines()` | Inicializa GPIOs de filas, columnas y botones |
| `mostrar_pantalla()` | Multiplexación de la matriz (1 frame completo) |
| `dibujar_pieza()` | Escribe una pieza en el buffer `pantalla` |
| `colision()` | Verifica si una pieza puede ocupar una posición |
| `fijar_pieza()` | Ancla la pieza al tablero y revisa filas completas |
| `revisar_filas()` | Busca y elimina filas llenas con animación |
| `aplicar_gravedad_completa()` | Baja bloques flotantes tras eliminar una fila |
| `animacion_game_over_x()` | Parpadeo de "X" verdes al perder |
| `juego()` | Loop principal del juego |
| `app_main()` | Punto de entrada ESP-IDF |

## Build

Proyecto ESP-IDF estándar. Compilar y flashear con:

