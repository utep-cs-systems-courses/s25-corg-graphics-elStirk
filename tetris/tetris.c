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

#define MAX_COLUMNS    (SCREEN_WIDTH  / BLOCK_SIZE)
#define MAX_ROWS       (SCREEN_HEIGHT / BLOCK_SIZE)

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
static char grid[MAX_COLUMNS][MAX_ROWS];
static const int numColumns = MAX_COLUMNS;
static const int numRows    = MAX_ROWS;

enum { FALSE = 0, TRUE = 1 };
volatile int redrawScreen     = TRUE;
volatile int pieceStoppedFlag = FALSE;

static short shapeCol, shapeRow;
static char  shapeIndex = 0, colIndex = 0;

static short lastCol = 0, lastRow = 0;
static char  lastIdx = -1;

unsigned short shapeColors[NUM_SHAPES] = {
  COLOR_RED, COLOR_GREEN, COLOR_ORANGE, COLOR_BLUE
};
#define BG_COLOR      COLOR_BLACK

// Prototipo de función para evitar declaraciones implícitas
static void draw_piece(short col, short row, char idx, unsigned short color);

// --------------------------------------------------
// Botones (ejemplo extraído de msquares.c)
// --------------------------------------------------
#define SWITCHES       15    /* P2 bits 0-3: SW1-SW4 */
volatile int switches = 0;

static char switch_update_interrupt_sense() {
  char p2val = P2IN;
  P2IES |= (p2val & SWITCHES);
  P2IES &= (p2val | ~SWITCHES);
  return p2val;
}

void switch_init() {
  P2REN |= SWITCHES;
  P2IE  |= SWITCHES;
  P2OUT |= SWITCHES;
  P2DIR &= ~SWITCHES;
  switch_update_interrupt_sense();
}

void switch_interrupt_handler() {
  // Borrar posición anterior para evitar restos fantasma
  if (lastIdx >= 0) {
    draw_piece(lastCol, lastRow, lastIdx, BG_COLOR);
  }
  pieceStoppedFlag = FALSE;

  char p2val = switch_update_interrupt_sense();
  switches = ~p2val & SWITCHES;

  /* SW1: mover pieza a la izquierda */
  if (switches & (1<<0)) {
    short newCol = shapeCol - BLOCK_SIZE;
    int canMove = TRUE;
    for (int i = 0; i < 4; i++) {
      int x = newCol + shapes[shapeIndex][i].x * BLOCK_SIZE;
      int y = shapeRow + shapes[shapeIndex][i].y * BLOCK_SIZE;
      int c = x / BLOCK_SIZE;
      int r = y / BLOCK_SIZE;
      if (c < 0 || (r >= 0 && grid[c][r])) {
        canMove = FALSE;
        break;
      }
    }
    if (canMove) {
      shapeCol = newCol;
      colIndex = shapeCol / BLOCK_SIZE;
      redrawScreen = TRUE;
    }
  }

  /* SW2: mover pieza a la derecha */
  if (switches & (1<<1)) {
    short newCol = shapeCol + BLOCK_SIZE;
    int canMove = TRUE;
    for (int i = 0; i < 4; i++) {
      int x = newCol + shapes[shapeIndex][i].x * BLOCK_SIZE;
      int y = shapeRow + shapes[shapeIndex][i].y * BLOCK_SIZE;
      int c = x / BLOCK_SIZE;
      int r = y / BLOCK_SIZE;
      if (c >= numColumns || (r >= 0 && grid[c][r])) {
        canMove = FALSE;
        break;
      }
    }
    if (canMove) {
      shapeCol = newCol;
      colIndex = shapeCol / BLOCK_SIZE;
      redrawScreen = TRUE;
    }
  }

  /* SW4: reiniciar juego */
  if (switches & (1<<3)) {
    clearScreen(BG_COLOR);
    memset(grid, 0, sizeof grid);
    shapeIndex = 0;
    colIndex   = 0;
    shapeCol   = 0;
    shapeRow   = -BLOCK_SIZE * 4;
    redrawScreen = TRUE;
  }
}

void __interrupt_vec(PORT2_VECTOR) Port_2() {
  if (P2IFG & SWITCHES) {
    P2IFG &= ~SWITCHES;
    switch_interrupt_handler();
  }
}

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

void wdt_c_handler() {
  static int tick = 0;
  if (++tick < 64) return;
  tick = 0;

  short newRow = shapeRow + BLOCK_SIZE;
  int collided = FALSE;
  for (int i = 0; i < 4; i++) {
    int x = shapeCol + shapes[shapeIndex][i].x * BLOCK_SIZE;
    int y = newRow  + shapes[shapeIndex][i].y * BLOCK_SIZE;
    int c = x / BLOCK_SIZE;
    int r = y / BLOCK_SIZE;
    if (r >= numRows || (r >= 0 && grid[c][r])) {
      collided = TRUE;
      break;
    }
  }

  if (!collided) {
    shapeRow = newRow;
  } else {
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
    for (int i = 0; i < 4; i++) {
      int x = shapeCol + shapes[shapeIndex][i].x * BLOCK_SIZE;
      int y = shapeRow + shapes[shapeIndex][i].y * BLOCK_SIZE;
      int c = x / BLOCK_SIZE;
      int r = y / BLOCK_SIZE;
      if (r >= 0 && r < numRows) grid[c][r] = 1;
    }
    draw_piece(shapeCol, shapeRow, shapeIndex, shapeColors[shapeIndex]);
    pieceStoppedFlag = TRUE;
    shapeIndex = (shapeIndex + 1) % NUM_SHAPES;
    colIndex   = (colIndex   + 1) % numColumns;
    shapeCol   = colIndex * BLOCK_SIZE;
    shapeRow   = -BLOCK_SIZE * 4;
  }
  redrawScreen = TRUE;
}

int main() {
  P1DIR |= BIT6; P1OUT |= BIT6;
  configureClocks();
  lcd_init();
  clearScreen(BG_COLOR);

  switch_init();
  memset(grid, 0, sizeof grid);

  shapeIndex = 0;
  colIndex   = 0;
  shapeCol   = 0;
  shapeRow   = -BLOCK_SIZE * 4;

  enableWDTInterrupts();
  or_sr(0x8);

  while (TRUE) {
    if (redrawScreen) {
      redrawScreen = FALSE;
      update_moving_shape();
    }
    P1OUT &= ~BIT6;
    or_sr(0x10);
    P1OUT |= BIT6;
  }
}
