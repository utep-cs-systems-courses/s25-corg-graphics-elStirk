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
static signed char grid[MAX_COLUMNS][MAX_ROWS]; // índice de forma o -1
static const int numColumns = MAX_COLUMNS;
static const int numRows    = MAX_ROWS;

enum { FALSE = 0, TRUE = 1 };
volatile int redrawScreen     = TRUE;
volatile int pieceStoppedFlag = FALSE;

// <<< LONG-PRESS RESET: variables para SW2
volatile char pressing_SW2      = FALSE;
volatile unsigned char sw2_count = 0;
// <<< end

static short shapeCol, shapeRow;
static char  shapeIndex    = 0;
static char  shapeRotation = 0;
static char  colIndex      = 0;

unsigned short shapeColors[NUM_SHAPES] = {
  COLOR_RED, COLOR_GREEN, COLOR_ORANGE, COLOR_BLUE
};
#define BG_COLOR      COLOR_BLACK

static void draw_piece(short col, short row, char idx, char rot, unsigned short color);
static void draw_grid(void);
static void clear_full_rows(void);
static void draw_score_label(void);

// --------------------------------------------------
// Dibuja el texto "SCORE:" en la parte inferior derecha
// --------------------------------------------------
static void draw_score_label(void) {
  const char *label = "SCORE:";
  int len = strlen(label);
  int x = SCREEN_WIDTH - len * 5;
  int y = SCREEN_HEIGHT - 7;
  drawString5x7(x, y, (char *)label, COLOR_WHITE, BG_COLOR);
}

// --------------------------------------------------
// Dibuja la rejilla actual
// --------------------------------------------------
static void draw_grid(void) {
  for (int c = 0; c < numColumns; c++) {
    for (int r = 0; r < numRows; r++) {
      short x = c * BLOCK_SIZE;
      short y = r * BLOCK_SIZE;
      if (grid[c][r] >= 0) {
        unsigned short color = shapeColors[grid[c][r]];
        fillRectangle(x, y, BLOCK_SIZE, BLOCK_SIZE, color);
      } else {
        fillRectangle(x, y, BLOCK_SIZE, BLOCK_SIZE, BG_COLOR);
      }
    }
  }
}

// --------------------------------------------------
// Dibuja una pieza en coordenadas (col,row)
// --------------------------------------------------
static void draw_piece(short col, short row, char idx, char rot, unsigned short color) {
  for (int i = 0; i < 4; i++) {
    int ox = shapes[idx][i].x;
    int oy = shapes[idx][i].y;
    int rx = (rot==1? -oy : rot==2?-ox : rot==3? oy : ox);
    int ry = (rot==1?  ox : rot==2?-oy : rot==3?-ox : oy);
    short x = col + rx * BLOCK_SIZE;
    short y = row + ry * BLOCK_SIZE;
    fillRectangle(x, y, BLOCK_SIZE, BLOCK_SIZE, color);
  }
}

// --------------------------------------------------
// Detecta y elimina filas completas
// --------------------------------------------------
static void clear_full_rows(void) {
  for (int r = numRows-1; r >= 0; r--) {
    int full = TRUE;
    for (int c = 0; c < numColumns; c++) {
      if (grid[c][r] < 0) { full = FALSE; break; }
    }
    if (full) {
      // desplaza hacia abajo
      for (int rr = r; rr > 0; rr--) {
        for (int c = 0; c < numColumns; c++) {
          grid[c][rr] = grid[c][rr-1];
        }
      }
      for (int c = 0; c < numColumns; c++) grid[c][0] = -1;
      r++; // reevaluar misma fila tras desplazar
    }
  }
}

// --------------------------------------------------
// Botones con debounce y manejo de long-press SW2
// --------------------------------------------------
#define SWITCHES 15
volatile int switches = 0;

static char switch_update_interrupt_sense(void) {
  char p2val = P2IN;
  P2IES |= (p2val & SWITCHES);
  P2IES &= (p2val | ~SWITCHES);
  return p2val;
}

void switch_init(void) {
  P2REN |= SWITCHES;
  P2IE  |= SWITCHES;
  P2OUT |= SWITCHES;
  P2DIR &= ~SWITCHES;
  switch_update_interrupt_sense();
}

void switch_interrupt_handler(void) {
  P2IE &= ~SWITCHES;
  __delay_cycles(50000);
  char p2val = switch_update_interrupt_sense();
  switches = ~p2val & SWITCHES;

  // SW1: mover izquierda
  if (switches & (1<<0)) {
    if (!pieceStoppedFlag) {
      short newCol = shapeCol - BLOCK_SIZE;
      if (newCol >= 0) shapeCol = newCol;
    }
  }

  // SW2: rotación o inicio de conteo long-press
  if (switches & (1<<1)) {
    pressing_SW2 = TRUE;
    sw2_count = 0;
  } else if (pressing_SW2) {
    pressing_SW2 = FALSE;
    if (sw2_count < 3) {
      // rotación normal
      char newRot = (shapeRotation + 1) % 4;
      int valid = TRUE;
      for (int i = 0; i < 4; i++) {
        int ox = shapes[shapeIndex][i].x;
        int oy = shapes[shapeIndex][i].y;
        int rx = (newRot==1? -oy : newRot==2?-ox : newRot==3? oy : ox);
        int ry = (newRot==1?  oy : newRot==2?-oy : newRot==3?-ox : oy);
        int c = (shapeCol + rx*BLOCK_SIZE)/BLOCK_SIZE;
        int r = (shapeRow + ry*BLOCK_SIZE)/BLOCK_SIZE;
        if (c<0||c>=numColumns||r>=numRows||(r>=0&&grid[c][r]>=0)) { valid=FALSE; break; }
      }
      if (valid) shapeRotation = newRot;
    }
  }

  // SW3: reiniciar manual
  if (switches & (1<<2)) {
    clearScreen(BG_COLOR);
    memset(grid, -1, sizeof grid);
    shapeIndex = shapeRotation = colIndex = 0;
    shapeCol = (numColumns/2 - 1) * BLOCK_SIZE;
    shapeRow = -BLOCK_SIZE * 2;
    draw_score_label();
  }

  // SW4: mover derecha
  if (switches & (1<<3)) {
    if (!pieceStoppedFlag) {
      short newCol = shapeCol + BLOCK_SIZE;
      if (newCol + BLOCK_SIZE <= SCREEN_WIDTH) shapeCol = newCol;
    }
  }

  redrawScreen = TRUE;
  P2IFG = 0;
  P2IE |= SWITCHES;
}

// --------------------------------------------------
// WDT: caída, apilamiento, game over y long-press SW2
// --------------------------------------------------
void wdt_c_handler(void) {
  static int tick = 0;
  if (++tick < 64) return;
  tick = 0;

  // long-press SW2 (~3s)
  if (pressing_SW2) {
    if (++sw2_count >= 3) {
      clearScreen(BG_COLOR);
      memset(grid, -1, sizeof grid);
      shapeIndex = shapeRotation = colIndex = 0;
      shapeCol = (numColumns/2 - 1) * BLOCK_SIZE;
      shapeRow = -BLOCK_SIZE * 2;
      draw_score_label();
      pressing_SW2 = FALSE;
      sw2_count = 0;
      redrawScreen = TRUE;
      return;
    }
  }

  // caída de la pieza
  short newRow = shapeRow + BLOCK_SIZE;
  int collided = FALSE;
  for (int i = 0; i < 4; i++) {
    int ox = shapes[shapeIndex][i].x;
    int oy = shapes[shapeIndex][i].y;
    int rx = (shapeRotation==1? -oy : shapeRotation==2?-ox : shapeRotation==3? oy : ox);
    int ry = (shapeRotation==1?  oy : shapeRotation==2?-oy : shapeRotation==3?-ox : oy);
    int c = (shapeCol + rx*BLOCK_SIZE)/BLOCK_SIZE;
    int r = (newRow + ry*BLOCK_SIZE)/BLOCK_SIZE;
    if (r >= numRows || (r >= 0 && grid[c][r] >= 0)) { collided = TRUE; break; }
  }
  
  if (!collided) {
    shapeRow = newRow;
  } else {
    // fija pieza
    for (int i = 0; i < 4; i++) {
      int ox = shapes[shapeIndex][i].x;
      int oy = shapes[shapeIndex][i].y;
      int rx = (shapeRotation==1? -oy : shapeRotation==2?-ox : shapeRotation==3? oy : ox);
      int ry = (shapeRotation==1?  oy : shapeRotation==2?-oy : shapeRotation==3?-ox : oy);
      int c = (shapeCol + rx*BLOCK_SIZE)/BLOCK_SIZE;
      int r = (shapeRow + ry*BLOCK_SIZE)/BLOCK_SIZE;
      if (r >= 0 && c>=0 && c<numColumns && r<numRows) grid[c][r] = shapeIndex;
    }
    clear_full_rows();
    // nueva pieza
    shapeIndex = (shapeIndex + 1) % NUM_SHAPES;
    shapeRotation = 0;
    shapeCol = (numColumns/2 - 1) * BLOCK_SIZE;
    shapeRow = -BLOCK_SIZE * 2;
  }

  redrawScreen = TRUE;
}

// --------------------------------------------------
// main
// --------------------------------------------------
int main(void) {
  WDTCTL = WDT_ADLY_1000;               // interrupción cada ~1ms
  IE1 |= WDTIE;

  configureClocks();
  lcd_init();

  switch_init();

  // inicializa rejilla
  memset(grid, -1, sizeof grid);

  // pieza inicial
  shapeCol = (numColumns/2 - 1) * BLOCK_SIZE;
  shapeRow = -BLOCK_SIZE * 2;
  shapeIndex = 0;
  shapeRotation = 0;

  draw_score_label();

  or_sr(0x18); // GIE on, CPU off
  while (1) {
    if (redrawScreen) {
      draw_grid();
      draw_piece(shapeCol, shapeRow, shapeIndex, shapeRotation,
                 shapeColors[shapeIndex]);
      redrawScreen = FALSE;
    }
  }

  return 0;
}
