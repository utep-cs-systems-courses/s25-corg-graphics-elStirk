#include <msp430.h>
#include <libTimer.h>
#include <string.h>
#include "lcdutils.h"
#include "lcddraw.h"
#include "buzzer.h"

// --------------------------------------------------
// Configuración de pantalla y rejilla
// --------------------------------------------------
#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  160
#define BLOCK_SIZE     10
#define MAX_COLUMNS    (SCREEN_WIDTH  / BLOCK_SIZE)
#define MAX_ROWS       (SCREEN_HEIGHT / BLOCK_SIZE)

// --------------------------------------------------
// Formas Tetris
// --------------------------------------------------
typedef struct { short x, y; } Offset;
const Offset shapes[][4] = {
  {{0,0},{1,0},{0,1},{1,1}},
  {{0,0},{1,0},{2,0},{3,0}},
  {{0,0},{0,1},{1,1},{2,1}},
  {{1,0},{0,1},{1,1},{2,1}}
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
unsigned short shapeColors[NUM_SHAPES] = { COLOR_RED, COLOR_GREEN, COLOR_ORANGE, COLOR_BLUE };
#define BG_COLOR      COLOR_BLACK

// Prototipo
static void draw_piece(short col, short row, char idx, unsigned short color);

// --------------------------------------------------
// Botones y buzzer
// --------------------------------------------------
#define SWITCHES 15
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
  if (lastIdx >= 0) draw_piece(lastCol, lastRow, lastIdx, BG_COLOR);
  pieceStoppedFlag = FALSE;
  char p2val = switch_update_interrupt_sense();
  switches = ~p2val & SWITCHES;
  if (switches & (1<<0)) { // SW1: izquierda
    short newCol = shapeCol - BLOCK_SIZE;
    int canMove = TRUE;
    for (int i = 0; i < 4; i++) {
      int c = (newCol + shapes[shapeIndex][i].x * BLOCK_SIZE) / BLOCK_SIZE;
      int r = (shapeRow + shapes[shapeIndex][i].y * BLOCK_SIZE) / BLOCK_SIZE;
      if (c < 0 || (r >= 0 && grid[c][r])) { canMove = FALSE; break; }
    }
    if (canMove) { shapeCol = newCol; colIndex = shapeCol / BLOCK_SIZE; redrawScreen = TRUE; }
  }
  if (switches & (1<<1)) { // SW2: derecha
    short newCol = shapeCol + BLOCK_SIZE;
    int canMove = TRUE;
    for (int i = 0; i < 4; i++) {
      int c = (newCol + shapes[shapeIndex][i].x * BLOCK_SIZE) / BLOCK_SIZE;
      int r = (shapeRow + shapes[shapeIndex][i].y * BLOCK_SIZE) / BLOCK_SIZE;
      if (c >= numColumns || (r >= 0 && grid[c][r])) { canMove = FALSE; break; }
    }
    if (canMove) { shapeCol = newCol; colIndex = shapeCol / BLOCK_SIZE; redrawScreen = TRUE; }
  }
  if (switches & (1<<3)) { // SW4: reset
    clearScreen(BG_COLOR);
    memset(grid, 0, sizeof grid);
    shapeIndex = colIndex = shapeCol = 0;
    shapeRow = -BLOCK_SIZE * 4;
    redrawScreen = TRUE;
  }
}
void __interrupt_vec(PORT2_VECTOR) Port_2() {
  if (P2IFG & SWITCHES) { P2IFG &= ~SWITCHES; switch_interrupt_handler(); }
}

// --------------------------------------------------
// Dibujo de pieza
// --------------------------------------------------
static void draw_piece(short col, short row, char idx, unsigned short color) {
  for (int i = 0; i < 4; i++) {
    fillRectangle(col + shapes[idx][i].x * BLOCK_SIZE,
                  row + shapes[idx][i].y * BLOCK_SIZE,
                  BLOCK_SIZE, BLOCK_SIZE, color);
  }
}

// --------------------------------------------------
// Actualiza pieza móvil
// --------------------------------------------------
static void update_moving_shape(void) {
  if (lastIdx >= 0 && !pieceStoppedFlag) draw_piece(lastCol, lastRow, lastIdx, BG_COLOR);
  draw_piece(shapeCol, shapeRow, shapeIndex, shapeColors[shapeIndex]);
  lastCol = shapeCol; lastRow = shapeRow; lastIdx = shapeIndex;
  pieceStoppedFlag = FALSE;
}

// --------------------------------------------------
// ISR Watchdog: mueve pieza y gestiona colisiones
// --------------------------------------------------
void wdt_c_handler() {
  static int tick = 0;
  if (++tick < 64) return;
  tick = 0;
  short newRow = shapeRow + BLOCK_SIZE;
  int collided = FALSE;
  for (int i = 0; i < 4; i++) {
    int c = (shapeCol + shapes[shapeIndex][i].x * BLOCK_SIZE) / BLOCK_SIZE;
    int r = (newRow  + shapes[shapeIndex][i].y * BLOCK_SIZE) / BLOCK_SIZE;
    if (r >= numRows || (r >= 0 && grid[c][r])) { collided = TRUE; break; }
  }
  if (!collided) {
    shapeRow = newRow;
  } else {
    if (shapeRow < 0) {
      clearScreen(BG_COLOR);
      memset(grid,0,sizeof grid);
      shapeIndex = colIndex = shapeCol = 0;
      shapeRow = -BLOCK_SIZE * 4;
      redrawScreen = TRUE;
      return;
    }
    // fijar pieza y colorear
    for (int i = 0; i < 4; i++) {
      int c = (shapeCol + shapes[shapeIndex][i].x * BLOCK_SIZE) / BLOCK_SIZE;
      int r = (shapeRow + shapes[shapeIndex][i].y * BLOCK_SIZE) / BLOCK_SIZE;
      if (r >= 0 && r < numRows) grid[c][r] = 1;
    }
    draw_piece(shapeCol, shapeRow, shapeIndex, shapeColors[shapeIndex]);
    pieceStoppedFlag = TRUE;
    // detectar líneas completas
    for (int row = 0; row < numRows; row++) {
      int full = TRUE;
      for (int col = 0; col < numColumns; col++) {
        if (!grid[col][row]) { full = FALSE; break; }
      }
      if (full) {
        // desplazar hacia abajo
        for (int r = row; r > 0; r--) {
          for (int c = 0; c < numColumns; c++) {
            grid[c][r] = grid[c][r-1];
          }
        }
        // limpiar fila 0
        for (int c = 0; c < numColumns; c++) grid[c][0] = 0;
        // redibujar rejilla
        clearScreen(BG_COLOR);
        for (int c = 0; c < numColumns; c++) {
          for (int r2 = 0; r2 < numRows; r2++) {
            if (grid[c][r2]) fillRectangle(c*BLOCK_SIZE, r2*BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE, COLOR_WHITE);
          }
        }
        // sonido de línea
        buzzer_set_period(1000);
      }
    }
    // nueva pieza
    shapeIndex = (shapeIndex + 1) % NUM_SHAPES;
    colIndex   = (colIndex   + 1) % numColumns;
    shapeCol   = colIndex * BLOCK_SIZE;
    shapeRow   = -BLOCK_SIZE * 4;
  }
  redrawScreen = TRUE;
}

// --------------------------------------------------
// main
// --------------------------------------------------
int main() {
  P1DIR |= BIT6; P1OUT |= BIT6;
  configureClocks();
  lcd_init();
  clearScreen(BG_COLOR);
  buzzer_init();
  switch_init();
  memset(grid,0,sizeof grid);
  shapeIndex = colIndex = shapeCol = 0;
  shapeRow = -BLOCK_SIZE * 4;
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
