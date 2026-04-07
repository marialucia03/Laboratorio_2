#include <stdio.h>
#include <string.h>
#include <stdbool.h>
 
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/timer.h"
#include "esp_timer.h"


static const gpio_num_t ROW_PINS[8] = {
    GPIO_NUM_4,  GPIO_NUM_16, GPIO_NUM_17, GPIO_NUM_18, GPIO_NUM_19, GPIO_NUM_21, GPIO_NUM_22, GPIO_NUM_23
};

/* 74HC595 control (3 pines) */
#define SR_DATA    GPIO_NUM_13
#define SR_CLOCK   GPIO_NUM_14
#define SR_LATCH   GPIO_NUM_27

/* Botones (input-only, pull-down externo 10k) */
#define BTN_LEFT   GPIO_NUM_36
#define BTN_RIGHT  GPIO_NUM_39
#define BTN_DROP   GPIO_NUM_34

#define BOARD_W    8
#define BOARD_H    8
#define NUM_PIECES 5
 
/* Debounce para polling (ms) */
#define DEBOUNCE_MS 180
 
/* Timer de caida */
#define FALL_TIMER_GROUP   TIMER_GROUP_0
#define FALL_TIMER_IDX     TIMER_0
#define FALL_TIMER_DIVIDER 80        /* 80MHz/80 = 1MHz, 1 tick = 1us */
#define FALL_INTERVAL_US   600000    /* 600ms velocidad inicial */

/* ================================================================
 * PIEZAS (sin rotacion)
 * ================================================================ */
 
typedef struct {
    int blocks[4][2];   /* {fila_offset, col_offset} */
    int num_blocks;
    int width;
} piece_t;


static const piece_t PIECES[NUM_PIECES] = {
    /* 0: Linea 1x3  ###           */
    { .blocks={{0,0},{0,1},{0,2},{0,0}}, .num_blocks=3, .width=3 },
    /* 1: Cuadrado 2x2  ##         */
    /*                   ##         */
    { .blocks={{0,0},{0,1},{1,0},{1,1}}, .num_blocks=4, .width=2 },
    /* 2: L   #                     */
    /*        ##                    */
    { .blocks={{0,0},{1,0},{1,1},{0,0}}, .num_blocks=3, .width=2 },
    /* 3: L invertida  #            */
    /*                ##            */
    { .blocks={{0,1},{1,0},{1,1},{0,0}}, .num_blocks=3, .width=2 },
    /* 4: T  ###                    */
    /*        #                     */
    { .blocks={{0,0},{0,1},{0,2},{1,1}}, .num_blocks=4, .width=3 }
};

/* ================================================================
 * ESTADO DEL JUEGO
 * ================================================================ */

 static uint8_t board[BOARD_H][BOARD_W];  /* 0=vacio, 1=ocupado */
static int cur_piece;                     /* indice de pieza actual */
static int piece_row, piece_col;          /* posicion de la pieza */
 
static uint8_t disp_red[BOARD_H];         /* buffer rojo (1 bit/col) */
static uint8_t disp_green[BOARD_H];       /* buffer verde */
 
static volatile bool game_over    = false;
static volatile bool flash_active = false;
static uint8_t flash_rows_mask   = 0;
static int     flash_count       = 0;
static int     score             = 0;
static uint64_t fall_interval_us = FALL_INTERVAL_US;

/* Flag de caida - lo activa la ISR del timer */
static volatile bool flag_fall = false;
 
/* RNG simple (xorshift) */
static uint32_t rng_state = 12345;

/* ================================================================
 * RNG
 * ================================================================ */

static uint32_t rng_next(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}
 
static int random_piece(void) {
    return (int)(rng_next() % NUM_PIECES);
}


/* ================================================================
 * ISR DEL TIMER (caida periodica)
 *
 * Retorna bool como se vio en clase.
 * Solo activa una bandera volatile, sin logica pesada.
 * ================================================================ */
 

static bool IRAM_ATTR timer_fall_isr(void *args) {
    flag_fall = true;
    return false;
}



/* ================================================================
 * 74HC595 - SHIFT REGISTER
 *
 * 2 x 74HC595 en cascada (daisy chain):
 *   ESP32 --DATA--> 595 #1 (col verdes) --Q7S--> 595 #2 (col rojas)
 *   ESP32 --CLOCK--> ambos (pin 11)
 *   ESP32 --LATCH--> ambos (pin 12)
 *
 * Se envian 16 bits con gpio_set_level():
 *   - Primero 8 bits de rojo (pasan al 595 #2)
 *   - Luego 8 bits de verde (quedan en 595 #1)
 *
 * Catodo LOW = LED encendido, por eso se invierte la data.
 * ================================================================ */
 
static void sr_send_16bits(uint8_t red_data, uint8_t green_data) {
    uint8_t red_inv   = ~red_data;
    uint8_t green_inv = ~green_data;
 
    /* Enviar rojo primero (se desplaza al 595 #2) */
    for (int i = 7; i >= 0; i--) {
        gpio_set_level(SR_DATA, (red_inv >> i) & 1);
        gpio_set_level(SR_CLOCK, 0);
        gpio_set_level(SR_CLOCK, 1);
    }
    /* Enviar verde (queda en 595 #1) */
    for (int i = 7; i >= 0; i--) {
        gpio_set_level(SR_DATA, (green_inv >> i) & 1);
        gpio_set_level(SR_CLOCK, 0);
        gpio_set_level(SR_CLOCK, 1);
    }
 
    /* Pulso de latch: transferir a salidas */
    gpio_set_level(SR_LATCH, 0);
    gpio_set_level(SR_LATCH, 1);
}


/* ================================================================
 * CONFIGURACION GPIO
 * ================================================================ */


static void configure_gpio(void) {
    /* Filas como salida */
    for (int i = 0; i < 8; i++) {
        gpio_config_t conf = {
            .pin_bit_mask  = (1ULL << ROW_PINS[i]),
            .mode          = GPIO_MODE_OUTPUT,
            .pull_up_en    = GPIO_PULLUP_DISABLE,
            .pull_down_en  = GPIO_PULLDOWN_DISABLE,
            .intr_type     = GPIO_INTR_DISABLE
        };
        gpio_config(&conf);
        gpio_set_level(ROW_PINS[i], 0);
    }
 
    /* Pines 595 como salida */
    gpio_config_t sr_conf = {
        .pin_bit_mask = (1ULL << SR_DATA) |
                        (1ULL << SR_CLOCK) |
                        (1ULL << SR_LATCH),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE
    };
    gpio_config(&sr_conf);
    gpio_set_level(SR_DATA, 0);
    gpio_set_level(SR_CLOCK, 0);
    gpio_set_level(SR_LATCH, 0);
 
    /* Apagar todos los LEDs */
    sr_send_16bits(0x00, 0x00);
 
    /* Botones como entrada (pull-down externo, sin pull interno) */
    gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << BTN_LEFT) |
                        (1ULL << BTN_RIGHT) |
                        (1ULL << BTN_DROP),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE
    };
    gpio_config(&btn_conf);
}


/* ================================================================
 * CONFIGURACION TIMER (como se vio en clase)
 * ================================================================ */
 
static void configure_fall_timer(void) {
    timer_config_t config = {
        .divider     = FALL_TIMER_DIVIDER,
        .counter_dir = TIMER_COUNT_UP,
        .counter_en  = TIMER_PAUSE,
        .alarm_en    = TIMER_ALARM_EN,
        .auto_reload = true
    };
    timer_init(FALL_TIMER_GROUP, FALL_TIMER_IDX, &config);
    timer_set_counter_value(FALL_TIMER_GROUP, FALL_TIMER_IDX, 0);
    timer_set_alarm_value(FALL_TIMER_GROUP, FALL_TIMER_IDX, fall_interval_us);
    timer_isr_callback_add(FALL_TIMER_GROUP, FALL_TIMER_IDX,
                           timer_fall_isr, NULL, 0);
    timer_enable_intr(FALL_TIMER_GROUP, FALL_TIMER_IDX);
    timer_start(FALL_TIMER_GROUP, FALL_TIMER_IDX);
}
 
static void update_fall_speed(void) {
    int64_t new_val = (int64_t)FALL_INTERVAL_US - (int64_t)(score / 3) * 50000;
    if (new_val < 200000) new_val = 200000;
    fall_interval_us = (uint64_t)new_val;
    timer_set_alarm_value(FALL_TIMER_GROUP, FALL_TIMER_IDX, fall_interval_us);
}


/* ================================================================
 * MULTIPLEXADO DE LA MATRIZ
 *
 * Anodo comun: fila HIGH + catodo LOW = LED encendido
 * Se barre fila por fila. Cada barrido completo ~2ms -> ~500Hz
 * ================================================================ */

 static void display_scan_row(int row) {
    /* Apagar fila anterior */
    for (int r = 0; r < 8; r++)
        gpio_set_level(ROW_PINS[r], 0);
 
    /* Enviar datos de columnas por los 595 */
    sr_send_16bits(disp_red[row], disp_green[row]);
 
    /* Activar esta fila */
    gpio_set_level(ROW_PINS[row], 1);
}
 
static void full_scan(void) {
    for (int row = 0; row < 8; row++) {
        display_scan_row(row);
        for (volatile int d = 0; d < 300; d++);
        gpio_set_level(ROW_PINS[row], 0);
    }
}


/* ================================================================
 * POLLING DE BOTONES (con debounce por tiempo)
 *
 * Botones con pull-down externo: presionado = HIGH (1)
 * Se detecta flanco de subida (no estaba presionado -> presionado)
 * ================================================================ */

typedef struct {
    gpio_num_t pin;
    bool last_state;
    int64_t last_press_time;
    bool pressed;           /* flag de evento: "se acaba de presionar" */
} button_t;
 
static button_t btn_left  = { BTN_LEFT,  false, 0, false };
static button_t btn_right = { BTN_RIGHT, false, 0, false };
static button_t btn_drop  = { BTN_DROP,  false, 0, false };
 
static void poll_button(button_t *btn) {
    btn->pressed = false;
    bool current = (gpio_get_level(btn->pin) == 1);
    int64_t now = esp_timer_get_time();
 
    /* Flanco de subida + debounce */
    if (current && !btn->last_state) {
        if ((now - btn->last_press_time) > (DEBOUNCE_MS * 1000)) {
            btn->pressed = true;
            btn->last_press_time = now;
        }
    }
    btn->last_state = current;
}
 
static void poll_all_buttons(void) {
    poll_button(&btn_left);
    poll_button(&btn_right);
    poll_button(&btn_drop);
}


/* ================================================================
 * LOGICA DEL JUEGO
 * ================================================================ */

 static bool piece_fits(int pidx, int row, int col) {
    const piece_t *p = &PIECES[pidx];
    for (int i = 0; i < p->num_blocks; i++) {
        int r = row + p->blocks[i][0];
        int c = col + p->blocks[i][1];
        if (r < 0 || r >= BOARD_H || c < 0 || c >= BOARD_W) return false;
        if (board[r][c]) return false;
    }
    return true;
}
 
static void lock_piece(void) {
    const piece_t *p = &PIECES[cur_piece];
    for (int i = 0; i < p->num_blocks; i++) {
        int r = piece_row + p->blocks[i][0];
        int c = piece_col + p->blocks[i][1];
        if (r >= 0 && r < BOARD_H && c >= 0 && c < BOARD_W)
            board[r][c] = 1;
    }
}
 
static int clear_lines(void) {
    int cleared = 0;
    flash_rows_mask = 0;
    for (int r = 0; r < BOARD_H; r++) {
        bool full = true;
        for (int c = 0; c < BOARD_W; c++) {
            if (!board[r][c]) { full = false; break; }
        }
        if (full) {
            flash_rows_mask |= (1 << r);
            cleared++;
        }
    }
    if (cleared > 0) {
        flash_active = true;
        flash_count = 0;
    }
    return cleared;
}
 
static void remove_marked_lines(void) {
    /*
     * Estrategia: copiar las filas NO marcadas a un tablero temporal
     * de abajo hacia arriba, y luego copiar de vuelta.
     * Esto evita el bug de desplazar con mascara desactualizada.
     */
    uint8_t temp[BOARD_H][BOARD_W];
    memset(temp, 0, sizeof(temp));
 
    int dest = BOARD_H - 1;  /* escribir desde abajo */
    for (int src = BOARD_H - 1; src >= 0; src--) {
        if (!(flash_rows_mask & (1 << src))) {
            /* Esta fila NO fue eliminada, copiarla */
            memcpy(temp[dest], board[src], BOARD_W);
            dest--;
        }
    }
    /* Las filas restantes (dest..0) quedan en 0 (ya inicializadas) */
    memcpy(board, temp, sizeof(board));
    flash_rows_mask = 0;
}
 
static void spawn_piece(void) {
    cur_piece = random_piece();
    piece_row = 0;
    piece_col = (BOARD_W - PIECES[cur_piece].width) / 2;
    if (!piece_fits(cur_piece, piece_row, piece_col))
        game_over = true;
}
 
/* Colocar pieza y verificar lineas */
static void place_and_check(void) {
    lock_piece();
    int cl = clear_lines();
    if (cl > 0) {
        score += cl;
        update_fall_speed();
    }
    if (!flash_active) {
        remove_marked_lines();
        spawn_piece();
    }
}


/* ================================================================
 * ACTUALIZACION DE DISPLAY
 * ================================================================ */

static void update_display(void) {
    memset(disp_red, 0, sizeof(disp_red));
    memset(disp_green, 0, sizeof(disp_green));
 
    /* Tablero (piezas colocadas) en ROJO */
    for (int r = 0; r < BOARD_H; r++)
        for (int c = 0; c < BOARD_W; c++)
            if (board[r][c])
                disp_red[r] |= (1 << c);
 
    /* Flash: lineas completas en AMARILLO */
    if (flash_active) {
        for (int r = 0; r < BOARD_H; r++) {
            if (flash_rows_mask & (1 << r)) {
                if (flash_count % 2 == 0) {
                    disp_red[r]   = 0xFF;
                    disp_green[r] = 0xFF;
                } else {
                    disp_red[r]   = 0x00;
                    disp_green[r] = 0x00;
                }
            }
        }
        return;
    }
 
    /* Pieza activa en VERDE */
    if (!game_over) {
        const piece_t *p = &PIECES[cur_piece];
        for (int i = 0; i < p->num_blocks; i++) {
            int r = piece_row + p->blocks[i][0];
            int c = piece_col + p->blocks[i][1];
            if (r >= 0 && r < BOARD_H && c >= 0 && c < BOARD_W)
                disp_green[r] |= (1 << c);
        }
    }
}


/* ================================================================
 * ANIMACIONES
 * ================================================================ */
 
static void game_over_animation(void) {
    /* Llenar de rojo de abajo hacia arriba */
    for (int r = BOARD_H - 1; r >= 0; r--) {
        disp_red[r] = 0xFF;
        disp_green[r] = 0x00;
        for (int t = 0; t < 80; t++)
            full_scan();
    }
    /* Parpadear 3 veces */
    for (int blink = 0; blink < 6; blink++) {
        for (int r = 0; r < BOARD_H; r++)
            disp_red[r] = (blink % 2 == 0) ? 0xFF : 0x00;
        for (int t = 0; t < 80; t++)
            full_scan();
    }
}
 
static void reset_game(void) {
    memset(board, 0, sizeof(board));
    score = 0;
    game_over = false;
    flash_active = false;
    flash_rows_mask = 0;
    fall_interval_us = FALL_INTERVAL_US;
    rng_state = (uint32_t)(esp_timer_get_time() & 0xFFFFFFFF);
    if (rng_state == 0) rng_state = 42;
    timer_set_alarm_value(FALL_TIMER_GROUP, FALL_TIMER_IDX, fall_interval_us);
    timer_set_counter_value(FALL_TIMER_GROUP, FALL_TIMER_IDX, 0);
    spawn_piece();
    update_display();
}


/* ================================================================
 * app_main
 * ================================================================ */

 void app_main(void) {
    /* 1. Configurar hardware */
    configure_gpio();
 
    /* 2. Inicializar juego */
    rng_state = (uint32_t)(esp_timer_get_time() & 0xFFFFFFFF);
    if (rng_state == 0) rng_state = 42;
    memset(board, 0, sizeof(board));
    spawn_piece();
    update_display();
 
    /* 3. Iniciar timer de caida */
    configure_fall_timer();
 
    /* 4. Bucle principal */
    while (1) {
 
        /* Leer botones por polling */
        poll_all_buttons();
 
        /* --- GAME OVER --- */
        if (game_over) {
            if (btn_left.pressed || btn_right.pressed || btn_drop.pressed) {
                reset_game();
                timer_start(FALL_TIMER_GROUP, FALL_TIMER_IDX);
            }
            full_scan();
            continue;
        }
 
        /* --- FLASH (lineas eliminadas) --- */
        if (flash_active) {
            flag_fall = false;
            flash_count++;
            update_display();
            if (flash_count >= 6) {
                flash_active = false;
                remove_marked_lines();
                spawn_piece();
                update_display();
            }
            for (int t = 0; t < 40; t++)
                full_scan();
            continue;
        }
 
        /* --- CONTROLES --- */
        if (btn_left.pressed) {
            if (piece_fits(cur_piece, piece_row, piece_col - 1))
                piece_col--;
            update_display();
        }
 
        if (btn_right.pressed) {
            if (piece_fits(cur_piece, piece_row, piece_col + 1))
                piece_col++;
            update_display();
        }
 
        if (btn_drop.pressed) {
            /* Hard drop: bajar hasta el fondo */
            while (piece_fits(cur_piece, piece_row + 1, piece_col))
                piece_row++;
            place_and_check();
            update_display();
        }
 
        /* --- CAIDA PERIODICA (flag del timer ISR) --- */
        if (flag_fall) {
            flag_fall = false;
            if (piece_fits(cur_piece, piece_row + 1, piece_col)) {
                piece_row++;
            } else {
                place_and_check();
            }
            update_display();
        }
 
        /* --- GAME OVER CHECK --- */
        if (game_over) {
            timer_pause(FALL_TIMER_GROUP, FALL_TIMER_IDX);
            game_over_animation();
            continue;
        }
 
        /* --- MULTIPLEXADO (un barrido completo) --- */
        full_scan();
    }
}