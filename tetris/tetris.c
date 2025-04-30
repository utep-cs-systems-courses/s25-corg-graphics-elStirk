#include <msp430.h>
#include <libTimer.h>
#include "lcdutils.h"
#include "lcddraw.h"

// Tamaño de cada bloque en píxeles
#define BLOCK_SIZE 10

// Dimensiones de la grilla en celdas
#define COLS (screenWidth / BLOCK_SIZE)   // 160/10 = 16 columnas
#define ROWS (screenHeight / BLOCK_SIZE)  // 128/10 = 12 filas

// Array que almacena el color de cada celda (BG = fondo)
static unsigned short grid[COLS][ROWS];

// Offsets de las piezas (4 bloques)
typedef struct { int x, y; } Offset;
const Offset shapes[][4] = {
  {{0,0},{1,0},{0,1},{1,1}},  // O
  {{0,0},{1,0},{2,0},{3,0}},  // I
  {{0,0},{0,1},{1,1},{2,1}},  // L
  {{1,0},{0,1},{1,1},{2,1}}   // T
};
#define NUM_SHAPES (sizeof(shapes)/sizeof(shapes[0]))

// Estado de la pieza en caída (índice y posición en celdas)
static volatile int redrawFlag = 1;
static int shapeI, posX, posY;

// Colores para cada forma
static const unsigned short colors[NUM_SHAPES] = {
  COLOR_RED, COLOR_GREEN, COLOR_ORANGE, COLOR_BLUE
};
#define BG_COLOR COLOR_BLACK

// Dibuja y actualiza una celda de la grilla
static void draw_cell(int cx, int cy, unsigned short color) {
  fillRectangle(cx*BLOCK_SIZE, cy*BLOCK_SIZE,
                BLOCK_SIZE, BLOCK_SIZE, color);
  grid[cx][cy] = color;
}

// Actualiza sólo la pieza móvil sin limpiar todo el LCD
static void update_piece(void) {(int cx, int cy, unsigned short color) {
  fillRectangle(cx*BLOCK_SIZE, cy*BLOCK_SIZE,
                BLOCK_SIZE, BLOCK_SIZE, color);
  grid[cx][cy] = color;
}

// Actualiza sólo la pieza móvil sin limpiar todo el LCD\static void update_piece(void) {
  static int last[4][2] = {{-1,-1}};
  int cur[4][2];
  int idx = shapeI;

  // Calcular celdas actuales de la pieza
  for (int i = 0; i < 4; i++) {
    cur[i][0] = posX + shapes[idx][i].x;
    cur[i][1] = posY + shapes[idx][i].y;
  }

  // Borrar bloques previos restaurando el color del grid
  for (int i = 0; i < 4; i++) {
    int lx = last[i][0], ly = last[i][1];
    if (lx >= 0 && lx < COLS && ly >= 0 && ly < ROWS) {
      draw_cell(lx, ly, grid[lx][ly]);
    }
  }

  // Dibujar bloques nuevos
  for (int i = 0; i < 4; i++) {
    int cx = cur[i][0], cy = cur[i][1];
    if (cx >= 0 && cx < COLS && cy >= 0 && cy < ROWS) {
      draw_cell(cx, cy, colors[idx]);
    }
  }

  // Guardar para la siguiente iteración
  for (int i = 0; i < 4; i++) {
    last[i][0] = cur[i][0];
    last[i][1] = cur[i][1];
  }
}

// Watchdog Timer ISR: hace caer la pieza y la fija al colisionar
void wdt_c_handler() {
  static int tick;
  if (++tick < 64) return;  // controla velocidad
  tick = 0;

  // Avanzar pieza en Y (celdas)
  posY++;

  // Detectar colisión: fondo o bloque fijo
  int hit = 0;
  for (int i = 0; i < 4; i++) {
    int cx = posX + shapes[shapeI][i].x;
    int cy = posY + shapes[shapeI][i].y;
    if (cy >= ROWS || grid[cx][cy] != BG_COLOR) {
      hit = 1;
      break;
    }
  }

  if (hit) {
    // Retroceder un paso
    posY--;
    // Fijar cada bloque en la grilla
    for (int i = 0; i < 4; i++) {
      int cx = posX + shapes[shapeI][i].x;
      int cy = posY + shapes[shapeI][i].y;
      if (cx >= 0 && cx < COLS && cy >= 0 && cy < ROWS) {
        draw_cell(cx, cy, colors[shapeI]);
      }
    }
    // Generar nueva pieza en siguiente columna
    shapeI = (shapeI + 1) % NUM_SHAPES;
    static int spawn;
    posX = (spawn++ % COLS);
    posY = -2;
  }

  redrawFlag = 1;
}

int main() {
  // Inicializar grilla a fondo
  for (int x = 0; x < COLS; x++)
    for (int y = 0; y < ROWS; y++)
      grid[x][y] = BG_COLOR;

  // Hardware: LED, relojes y LCD
  P1DIR |= BIT6;
  P1OUT |= BIT6;
  configureClocks();
  lcd_init();
  clearScreen(BG_COLOR);

  // Estado inicial de la primera pieza
  shapeI = 0;
  posX = 0;
  posY = -2;

  // Activar WDT e interrupciones globales
  enableWDTInterrupts();
  or_sr(0x8);

  // Bucle principal: redibuja sólo la pieza móvil
  while (1) {
    if (redrawFlag) {
      redrawFlag = 0;
      update_piece();
    }
    // Low-power mode hasta la próxima ISR
    P1OUT &= ~BIT6;
    or_sr(0x10);
    P1OUT |= BIT6;
  }
}
