#include <msp430.h>
#include <libTimer.h>
#include <string.h>
#include "lcdutils.h"
#include "lcddraw.h"

// --------------------------------------------------
// Configuración de pantalla y rejilla
// --------------------------------------------------
#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  160
#define BLOCK_SIZE     10

// 128/10 = 12 columnas, 160/10 = 16 filas
#define MAX_COLUMNS    (SCREEN_WIDTH  / BLOCK_SIZE)  // 12
#define MAX_ROWS       (SCREEN_HEIGHT / BLOCK_SIZE)  // 16

// --------------------------------------------------
// Formas Tetris (4 offsets cada una)
// --------------------------------------------------
typedef struct { short x, y; } Offset;
const Offset shapes[][4] = {
  {{0,0},{1,0},{0,1},{1,1}},  // cuadrado
  {{0,0},{1,0},{2,0},{3,0}},  // línea
  {{0,0},{0,1},{1,1},{2,1}},  // L invertida
  {{1,0},{0,1},{1,1},{2,1}}   // T
};
#define NUM_SHAPES  (sizeof(shapes)/sizeof(shapes[0]))

// --------------------------------------------------
// Variables globales
// --------------------------------------------------
// Rejilla de ocupación: 1 = bloque fijo, 0 = vacío
static char grid[MAX_COLUMNS][MAX_ROWS];
static const int numColumns = MAX_COLUMNS;
static const int numRows    = MAX_ROWS;

// Flags para refresco y para no borrar pieza fija
enum { FALSE = 0, TRUE = 1 };
volatile int redrawScreen     = TRUE;
volatile int pieceStoppedFlag = FALSE;

// Estado de la pieza móvil
static short shapeCol, shapeRow;
static char  shapeIndex = 0, colIndex = 0;

// Para borrado selectivo de la pieza móvil
static short lastCol = 0, lastRow = 0;
static char  lastIdx = -1;

// Colores por forma
unsigned short shapeColors[NUM_SHAPES] = {
  COLOR_RED, COLOR_GREEN, COLOR_ORANGE, COLOR_BLUE
};
#define BG_COLOR  COLOR_BLACK

// --------------------------------------------------
// Dibuja una pieza en (col,row) con el color indicado
// --------------------------------------------------
static void draw_piece(short col, short row, char idx, unsigned short color) {
  for (int i = 0; i < 4; i++) {
    int x = col + shapes[idx][i].x * BLOCK_SIZE;
    int y = row + shapes[idx][i].y * BLOCK_SIZE;
    fillRectangle(x, y, BLOCK_SIZE, BLOCK_SIZE, color);
  }
}

// --------------------------------------------------
// Refresca sólo la pieza móvil, borrando la anterior
// --------------------------------------------------
static void update_moving_shape(void) {
  if (lastIdx >= 0 && !pieceStoppedFlag) {
    draw_piece(lastCol, lastRow, lastIdx, BG_COLOR);
  }
  draw_piece(shapeCol, shapeRow, shapeIndex, shapeColors[shapeIndex]);
  lastCol = shapeCol;
  lastRow = shapeRow;
  lastIdx = shapeIndex;
  pieceStoppedFlag = FALSE;
}

// --------------------------------------------------
// ISR del Watchdog: mueve pieza, comprueba colisión,
// gestiona apilamiento y reinicio de juego
// --------------------------------------------------
void wdt_c_handler() {
  static int tick = 0;
  if (++tick < 64) return;   // ralentiza (~512 Hz / 64)
  tick = 0;

  // 1) calculamos la nueva posición vertical potencial
  short newRow = shapeRow + BLOCK_SIZE;

  // 2) comprobamos bloque a bloque si choca al bajar
  int collided = FALSE;
  for (int i = 0; i < 4; i++) {
    int x = shapeCol + shapes[shapeIndex][i].x * BLOCK_SIZE;
    int y = newRow  + shapes[shapeIndex][i].y * BLOCK_SIZE;
    int c = x / BLOCK_SIZE;
    int r = y / BLOCK_SIZE;
    // colisión con suelo
    if (r >= numRows) {
      collided = TRUE;
      break;
    }
    // colisión con bloque fijo (solo si está dentro)
    if (r >= 0 && grid[c][r]) {
      collided = TRUE;
      break;
    }
  }

  if (!collided) {
    // no colisiona → aplicamos el movimiento
    shapeRow = newRow;
  } else {
    // colisiona antes de entrar → reinicio completo (Game Over)
    if (shapeRow < 0) {
      clearScreen(BG_COLOR);
      memset(grid, 0, sizeof grid);
      shapeIndex = 0;
      colIndex   = 0;
      shapeCol   = 0;
      shapeRow   = -BLOCK_SIZE * 4;
      redrawScreen = TRUE;
      return;
    }
    // choca ya dentro de pantalla → fijar en la posición actual
    for (int i = 0; i < 4; i++) {
      int x = shapeCol + shapes[shapeIndex][i].x * BLOCK_SIZE;
      int y = shapeRow + shapes[shapeIndex][i].y * BLOCK_SIZE;
      int c = x / BLOCK_SIZE;
      int r = y / BLOCK_SIZE;
      if (r >= 0 && r < numRows) {
        grid[c][r] = 1;
      }
    }
    // dibujamos la pieza fija
    draw_piece(shapeCol, shapeRow, shapeIndex, shapeColors[shapeIndex]);
    pieceStoppedFlag = TRUE;

    // preparamos la siguiente pieza
    shapeIndex = (shapeIndex + 1) % NUM_SHAPES;
    colIndex   = (colIndex   + 1) % numColumns;
    shapeCol   = colIndex * BLOCK_SIZE;
    shapeRow   = -BLOCK_SIZE * 4;
  }

  redrawScreen = TRUE;
}

// --------------------------------------------------
// main()
// --------------------------------------------------
int main() {
  // inicialización hardware
  P1DIR |= BIT6; P1OUT |= BIT6;   // LED en P1.6
  configureClocks();
  lcd_init();
  clearScreen(BG_COLOR);

  // vaciamos rejilla
  memset(grid, 0, sizeof grid);

  // estado inicial de la pieza móvil
  shapeIndex = 0;
  colIndex   = 0;
  shapeCol   = 0;
  shapeRow   = -BLOCK_SIZE * 4;

  // activamos WDT e interrupciones
  enableWDTInterrupts();
  or_sr(0x8);

  // bucle principal: pinta sólo la pieza móvil
  while (TRUE) {
    if (redrawScreen) {
      redrawScreen = FALSE;
      update_moving_shape();
    }
    // modo bajo consumo hasta la próxima ISR
    P1OUT &= ~BIT6;
    or_sr(0x10);
    P1OUT |= BIT6;
  }
}

