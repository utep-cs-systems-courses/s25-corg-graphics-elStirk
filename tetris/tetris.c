#include <msp430.h>
#include <libTimer.h>
#include "lcdutils.h"
#include "lcddraw.h"

// Tamaño de cada bloque en píxeles
enum { BLOCK_SIZE = 10 };

// Dimensiones de la grilla en celdas
enum { COLS = screenWidth / BLOCK_SIZE,   // 160/10 = 16 columnas
       ROWS = screenHeight / BLOCK_SIZE }; // 128/10 = 12 filas

// Buffer de pantalla: color de cada celda
static unsigned short grid[COLS][ROWS];

// Definición de formas Tetris (4 offsets cada una)
typedef struct { int x, y; } Offset;
static const Offset shapes[][4] = {
  {{0,0},{1,0},{0,1},{1,1}},  // O
  {{0,0},{1,0},{2,0},{3,0}},  // I
  {{0,0},{0,1},{1,1},{2,1}},  // L
  {{1,0},{0,1},{1,1},{2,1}}   // T
};
#define NUM_SHAPES (sizeof(shapes)/sizeof(shapes[0]))

// Colores para cada forma
static const unsigned short colors[NUM_SHAPES] = {
  COLOR_RED, COLOR_GREEN, COLOR_ORANGE, COLOR_BLUE
};
#define BG_COLOR COLOR_BLACK

// Estado de la pieza en caída (indice + coordenadas en celdas)
volatile int redrawScreen;
static int shapeI, posX, posY;

// Función: dibuja y actualiza valor en grid
static void draw_cell(int cx, int cy, unsigned short color) {
  fillRectangle(cx * BLOCK_SIZE,
                cy * BLOCK_SIZE,
                BLOCK_SIZE,
                BLOCK_SIZE,
                color);
  grid[cx][cy] = color;
}

// Dibuja solo la pieza móvil sin limpiar toda pantalla
static void update_piece(void) {
  static int last[4][2] = {{-1,-1},{-1,-1},{-1,-1},{-1,-1}};
  int cur[4][2];
  int idx = shapeI;

  // Calcular posiciones actuales en celdas
  for (int i = 0; i < 4; i++) {
    cur[i][0] = posX + shapes[idx][i].x;
    cur[i][1] = posY + shapes[idx][i].y;
  }

  // Borrar bloques previos restaurando grid
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

  // Guardar última posición
  for (int i = 0; i < 4; i++) {
    last[i][0] = cur[i][0];
    last[i][1] = cur[i][1];
  }
}

// Watchdog Timer ISR: mueve pieza y fija al colisionar
void wdt_c_handler() {
  static int tick;
  if (++tick < 64) return;
  tick = 0;

  // Avanzar en Y (celdas)
  posY++;

  // Comprobar colisión con fondo o bloque fijo
  int hit = 0;
  for (int i = 0; i < 4; i++) {
    int cx = posX + shapes[shapeI][i].x;
    int cy = posY + shapes[shapeI][i].y;
    if (cy >= ROWS || grid[cx][cy] != BG_COLOR) {
      hit = 1; break;
    }
  }

  if (hit) {
    // Retroceder un paso
    posY--;
    // Fijar bloques en grid
    for (int i = 0; i < 4; i++) {
      int cx = posX + shapes[shapeI][i].x;
      int cy = posY + shapes[shapeI][i].y;
      if (cx >= 0 && cx < COLS && cy >= 0 && cy < ROWS) {
        draw_cell(cx, cy, colors[shapeI]);
      }
    }
    // Nueva pieza en siguiente columna
    shapeI = (shapeI + 1) % NUM_SHAPES;
    static int spawn;
    posX = (spawn++ % COLS);
    posY = -2;
  }

  redrawScreen = 1;
}

int main(void) {
  // Inicializar grilla a fondo
  for (int x = 0; x < COLS; x++)
    for (int y = 0; y < ROWS; y++)
      grid[x][y] = BG_COLOR;

  // Hardware
  P1DIR |= BIT6;
  P1OUT |= BIT6;
  configureClocks();
  lcd_init();
  clearScreen(BG_COLOR);

  // Estado inicial
  shapeI = 0;
  posX   = 0;
  posY   = -2;
  redrawScreen = 1;

  // Activar WDT e interrupciones
  enableWDTInterrupts();
  or_sr(0x8);

  // Bucle principal
  while (1) {
    if (redrawScreen) {
      redrawScreen = 0;
      update_piece();
    }
    P1OUT &= ~BIT6;
    or_sr(0x10);
    P1OUT |= BIT6;
  }
}
