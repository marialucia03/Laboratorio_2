#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include <stdlib.h>
#include <stdint.h>

#define NUM_FILAS 7
#define NUM_COLS  5
#define NUM_COLS_VERDE 4

// -------------------------
// PINES MATRIZ
// -------------------------
gpio_num_t filas[NUM_FILAS] = {
    GPIO_NUM_16, GPIO_NUM_17, GPIO_NUM_18, GPIO_NUM_19,
    GPIO_NUM_21, GPIO_NUM_22, GPIO_NUM_23
};

// Juego normal en rojo
gpio_num_t columnas_rojo[NUM_COLS] = {
    GPIO_NUM_27, GPIO_NUM_26, GPIO_NUM_25, GPIO_NUM_33, GPIO_NUM_5
};

// Verde real disponible solo en 4 columnas
gpio_num_t columnas_verde[NUM_COLS_VERDE] = {
    GPIO_NUM_4, GPIO_NUM_15, GPIO_NUM_2, GPIO_NUM_32
};

// -------------------------
// BOTONES
// -------------------------
#define BTN_IZQ   GPIO_NUM_13
#define BTN_DER   GPIO_NUM_14
#define BTN_DROP  GPIO_NUM_12

// -------------------------
// MATRICES
// 0 = apagado
// 1 = rojo
// 2 = verde
// -------------------------
uint8_t pantalla[NUM_FILAS][NUM_COLS];
uint8_t tablero[NUM_FILAS][NUM_COLS];

// -------------------------
// CONFIGURACIÓN
// -------------------------
void configurar_pines(void)
{
    for (int f = 0; f < NUM_FILAS; f++) {
        gpio_reset_pin(filas[f]);
        gpio_set_direction(filas[f], GPIO_MODE_OUTPUT);
        gpio_set_level(filas[f], 0);
    }

    for (int c = 0; c < NUM_COLS; c++) {
        gpio_reset_pin(columnas_rojo[c]);
        gpio_set_direction(columnas_rojo[c], GPIO_MODE_OUTPUT);
        gpio_set_level(columnas_rojo[c], 1);
    }

    for (int c = 0; c < NUM_COLS_VERDE; c++) {
        gpio_reset_pin(columnas_verde[c]);
        gpio_set_direction(columnas_verde[c], GPIO_MODE_OUTPUT);
        gpio_set_level(columnas_verde[c], 1);
    }

    gpio_reset_pin(BTN_IZQ);
    gpio_set_direction(BTN_IZQ, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BTN_IZQ, GPIO_PULLUP_ONLY);

    gpio_reset_pin(BTN_DER);
    gpio_set_direction(BTN_DER, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BTN_DER, GPIO_PULLUP_ONLY);

    gpio_reset_pin(BTN_DROP);
    gpio_set_direction(BTN_DROP, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BTN_DROP, GPIO_PULLUP_ONLY);
}

// -------------------------
// DISPLAY
// -------------------------
void mostrar_pantalla(void)
{
    for (int f = 0; f < NUM_FILAS; f++) {

        for (int i = 0; i < NUM_FILAS; i++) {
            gpio_set_level(filas[i], 0);
        }

        for (int c = 0; c < NUM_COLS; c++) {
            gpio_set_level(columnas_rojo[c], 1);
        }

        for (int c = 0; c < NUM_COLS_VERDE; c++) {
            gpio_set_level(columnas_verde[c], 1);
        }

        for (int c = 0; c < NUM_COLS; c++) {
            if (pantalla[f][c] == 1) {
                gpio_set_level(columnas_rojo[c], 0);
            }
            else if (pantalla[f][c] == 2) {
                if (c < NUM_COLS_VERDE) {
                    gpio_set_level(columnas_verde[c], 0);
                }
            }
        }

        gpio_set_level(filas[f], 1);
        esp_rom_delay_us(700);
    }
}

// -------------------------
// MANEJO DE MATRICES
// -------------------------
void limpiar_tablero(void)
{
    for (int f = 0; f < NUM_FILAS; f++) {
        for (int c = 0; c < NUM_COLS; c++) {
            tablero[f][c] = 0;
        }
    }
}

void copiar_tablero_a_pantalla(void)
{
    for (int f = 0; f < NUM_FILAS; f++) {
        for (int c = 0; c < NUM_COLS; c++) {
            pantalla[f][c] = tablero[f][c];
        }
    }
}

void limpiar_pantalla_total(void)
{
    for (int f = 0; f < NUM_FILAS; f++) {
        for (int c = 0; c < NUM_COLS; c++) {
            pantalla[f][c] = 0;
        }
    }
}

void dibujar_pixel_color(int f, int c, uint8_t color)
{
    if (f >= 0 && f < NUM_FILAS && c >= 0 && c < NUM_COLS) {
        pantalla[f][c] = color;
    }
}

// -------------------------
// PIEZAS
// tipo 0:
// o x o
// x x x
//
// tipo 1:
// x x
// x x
//
// tipo 2:
// x
// x
// x
//
// tipo 3:
// x o
// x x
// x o
//
// tipo 4:
// o x
// x x
// o x
// -------------------------
void dibujar_pieza(int tipo, int y, int x, uint8_t color)
{
    if (tipo == 0) {
        dibujar_pixel_color(y,   x,   color);
        dibujar_pixel_color(y+1, x-1, color);
        dibujar_pixel_color(y+1, x,   color);
        dibujar_pixel_color(y+1, x+1, color);
    }

    if (tipo == 1) {
        dibujar_pixel_color(y,   x,   color);
        dibujar_pixel_color(y,   x+1, color);
        dibujar_pixel_color(y+1, x,   color);
        dibujar_pixel_color(y+1, x+1, color);
    }

    if (tipo == 2) { // línea de 3
        dibujar_pixel_color(y,   x, color);
        dibujar_pixel_color(y+1, x, color);
        dibujar_pixel_color(y+2, x, color);
    }

    if (tipo == 3) {
        dibujar_pixel_color(y,   x-1, color);
        dibujar_pixel_color(y+1, x-1, color);
        dibujar_pixel_color(y+1, x,   color);
        dibujar_pixel_color(y+2, x-1, color);
    }

    if (tipo == 4) {
        dibujar_pixel_color(y,   x,   color);
        dibujar_pixel_color(y+1, x-1, color);
        dibujar_pixel_color(y+1, x,   color);
        dibujar_pixel_color(y+2, x,   color);
    }
}

// -------------------------
// COLISIÓN
// -------------------------
int colision(int tipo, int y, int x)
{
    int coords[4][2];
    int n = 0;

    if (tipo == 0) {
        int t[4][2] = {{y,x},{y+1,x-1},{y+1,x},{y+1,x+1}};
        for (int i = 0; i < 4; i++) {
            coords[i][0] = t[i][0];
            coords[i][1] = t[i][1];
        }
        n = 4;
    }

    if (tipo == 1) {
        int t[4][2] = {{y,x},{y,x+1},{y+1,x},{y+1,x+1}};
        for (int i = 0; i < 4; i++) {
            coords[i][0] = t[i][0];
            coords[i][1] = t[i][1];
        }
        n = 4;
    }

    if (tipo == 2) {
        int t[3][2] = {{y,x},{y+1,x},{y+2,x}};
        for (int i = 0; i < 3; i++) {
            coords[i][0] = t[i][0];
            coords[i][1] = t[i][1];
        }
        n = 3;
    }

    if (tipo == 3) {
        int t[4][2] = {{y,x-1},{y+1,x-1},{y+1,x},{y+2,x-1}};
        for (int i = 0; i < 4; i++) {
            coords[i][0] = t[i][0];
            coords[i][1] = t[i][1];
        }
        n = 4;
    }

    if (tipo == 4) {
        int t[4][2] = {{y,x},{y+1,x-1},{y+1,x},{y+2,x}};
        for (int i = 0; i < 4; i++) {
            coords[i][0] = t[i][0];
            coords[i][1] = t[i][1];
        }
        n = 4;
    }

    for (int i = 0; i < n; i++) {
        int f = coords[i][0];
        int c = coords[i][1];

        if (c < 0 || c >= NUM_COLS) return 1;
        if (f >= NUM_FILAS) return 1;

        if (f >= 0 && tablero[f][c] != 0) return 1;
    }

    return 0;
}

// -------------------------
// ANIMACIONES
// -------------------------
void mostrar_frames(int repeticiones)
{
    for (int i = 0; i < repeticiones; i++) {
        mostrar_pantalla();
    }
}

void animar_fila_verde(int fila)
{
    for (int rep = 0; rep < 4; rep++) {

        copiar_tablero_a_pantalla();

        for (int c = 0; c < NUM_COLS; c++) {
            if (c < NUM_COLS_VERDE) {
                pantalla[fila][c] = 2;
            } else {
                pantalla[fila][c] = 1;
            }
        }

        mostrar_frames(80);

        copiar_tablero_a_pantalla();

        for (int c = 0; c < NUM_COLS; c++) {
            pantalla[fila][c] = 0;
        }

        mostrar_frames(80);
    }
}

// X game over EXACTA como la pediste, en 4x7 verde
void dibujar_x_game_over_verde(void)
{
    limpiar_pantalla_total();

    // Fila 0: X o X o
    pantalla[0][0] = 2;
    pantalla[0][2] = 2;

    // Fila 1: o X o o
    pantalla[1][1] = 2;

    // Fila 2: X o X o
    pantalla[2][0] = 2;
    pantalla[2][2] = 2;

    // Fila 3: o o o o
    // nada

    // Fila 4: o X o X
    pantalla[4][1] = 2;
    pantalla[4][3] = 2;

    // Fila 5: o o X o
    pantalla[5][2] = 2;

    // Fila 6: o X o X
    pantalla[6][1] = 2;
    pantalla[6][3] = 2;
}

void animacion_game_over_x(void)
{
    for (int rep = 0; rep < 8; rep++) {
        dibujar_x_game_over_verde();
        mostrar_frames(110);

        limpiar_pantalla_total();
        mostrar_frames(90);
    }
}

// -------------------------
// BORRAR FILA Y GRAVEDAD
// -------------------------
void borrar_fila(int fila)
{
    for (int c = 0; c < NUM_COLS; c++) {
        tablero[fila][c] = 0;
    }
}

int aplicar_gravedad_un_paso(void)
{
    int hubo_cambio = 0;

    for (int f = NUM_FILAS - 2; f >= 0; f--) {
        for (int c = 0; c < NUM_COLS; c++) {
            if (tablero[f][c] != 0 && tablero[f+1][c] == 0) {
                tablero[f+1][c] = tablero[f][c];
                tablero[f][c] = 0;
                hubo_cambio = 1;
            }
        }
    }

    return hubo_cambio;
}

void aplicar_gravedad_completa(void)
{
    while (aplicar_gravedad_un_paso()) {
        copiar_tablero_a_pantalla();
        mostrar_frames(35);
    }
}

// -------------------------
// REVISAR FILAS
// -------------------------
void revisar_filas(void)
{
    int hubo_linea = 1;

    while (hubo_linea) {
        hubo_linea = 0;

        for (int f = 0; f < NUM_FILAS; f++) {
            int completa = 1;

            for (int c = 0; c < NUM_COLS; c++) {
                if (tablero[f][c] == 0) {
                    completa = 0;
                    break;
                }
            }

            if (completa) {
                animar_fila_verde(f);
                borrar_fila(f);
                aplicar_gravedad_completa();
                hubo_linea = 1;
                break;
            }
        }
    }
}

// -------------------------
// FIJAR PIEZA
// -------------------------
void fijar_pieza(int tipo, int y, int x)
{
    copiar_tablero_a_pantalla();
    dibujar_pieza(tipo, y, x, 1);

    for (int f = 0; f < NUM_FILAS; f++) {
        for (int c = 0; c < NUM_COLS; c++) {
            if (pantalla[f][c] != 0) {
                tablero[f][c] = pantalla[f][c];
            }
        }
    }

    revisar_filas();
}

// -------------------------
// GAME OVER
// Se pierde si queda al menos una casilla ocupada en la primera fila
// -------------------------
int hay_game_over(void)
{
    for (int c = 0; c < NUM_COLS; c++) {
        if (tablero[0][c] != 0) {
            return 1;
        }
    }
    return 0;
}

// -------------------------
// JUEGO
// -------------------------
void juego(void)
{
    while (1)
    {
        int tipo = rand() % 5;
        int x = 2;
        int y = -3;

        int velocidad = 120;
        int contador_caida = 0;
        int contador_mov = 0;

        if (hay_game_over()) {
            animacion_game_over_x();
            limpiar_tablero();
            continue;
        }

        while (1)
        {
            contador_mov++;

            if (contador_mov > 20) {
                contador_mov = 0;

                if (!gpio_get_level(BTN_IZQ) && !colision(tipo, y, x - 1)) {
                    x--;
                }

                if (!gpio_get_level(BTN_DER) && !colision(tipo, y, x + 1)) {
                    x++;
                }
            }

            if (!gpio_get_level(BTN_DROP)) {
                velocidad = 10;
            } else {
                velocidad = 120;
            }

            contador_caida++;
            if (contador_caida > velocidad) {
                contador_caida = 0;

                if (!colision(tipo, y + 1, x)) {
                    y++;
                } else {
                    fijar_pieza(tipo, y, x);
                    break;
                }
            }

            copiar_tablero_a_pantalla();
            dibujar_pieza(tipo, y, x, 1);
            mostrar_pantalla();
        }
    }
}

// -------------------------
// MAIN
// -------------------------
void app_main(void)
{
    configurar_pines();
    limpiar_tablero();
    juego();
}