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
static char  shapeIndex    = 0;
static char  shapeRotation = 0;    // estado de rotación (0-3)
static char  colIndex      = 0;

static short lastCol = 0, lastRow = 0;
static char  lastIdx  = -1;
static char  lastRot  = 0;

unsigned short shapeColors[NUM_SHAPES] = {
  COLOR_RED, COLOR_GREEN, COLOR_ORANGE, COLOR_BLUE
};
#define BG_COLOR      COLOR_BLACK

// Prototipo de función
static void draw_piece(short col, short row, char idx, char rot, unsigned short color);

// --------------------------------------------------
// Botones (msquares.c)
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
  // Borrar pieza antigua
  if (lastIdx >= 0) {
    draw_piece(lastCol, lastRow, lastIdx, lastRot, BG_COLOR);
  }
  pieceStoppedFlag = FALSE;

  char p2val = switch_update_interrupt_sense();
  switches = ~p2val & SWITCHES;

  /* SW1: mover izquierda */
  if (switches & (1<<0)) {
    short newCol = shapeCol - BLOCK_SIZE;
    int canMove = TRUE;
    for (int i = 0; i < 4; i++) {
      int ox = shapes[shapeIndex][i].x;
      int oy = shapes[shapeIndex][i].y;
      int rx, ry;
      switch(shapeRotation) {
        case 0: rx = ox;  ry = oy;  break;
        case 1: rx = -oy; ry = ox;  break;
        case 2: rx = -ox; ry = -oy; break;
        case 3: rx = oy;  ry = -ox; break;
      }
      int x = newCol + rx * BLOCK_SIZE;
      int y = shapeRow + ry * BLOCK_SIZE;
      int c = x / BLOCK_SIZE;
      int r = y / BLOCK_SIZE;
      if (c < 0 || (r >= 0 && grid[c][r])) { canMove = FALSE; break; }
    }
    if (canMove) { shapeCol = newCol; redrawScreen = TRUE; }
  }

  /* SW2: mover derecha */
  if (switches & (1<<1)) {
    short newCol = shapeCol + BLOCK_SIZE;
    int canMove = TRUE;
    for (int i = 0; i < 4; i++) {
      int ox = shapes[shapeIndex][i].x;
      int oy = shapes[shapeIndex][i].y;
      int rx, ry;
      switch(shapeRotation) {
        case 0: rx = ox;  ry = oy;  break;
        case 1: rx = -oy; ry = ox;  break;
        case 2: rx = -ox; ry = -oy; break;
        case 3: rx = oy;  ry = -ox; break;
      }
      int x = newCol + rx * BLOCK_SIZE;
      int y = shapeRow + ry * BLOCK_SIZE;
      int c = x / BLOCK_SIZE;
      int r = y / BLOCK_SIZE;
      if (c >= numColumns || (r >= 0 && grid[c][r])) { canMove = FALSE; break; }
    }
    if (canMove) { shapeCol = newCol; redrawScreen = TRUE; }
  }

  /* SW3: mover derecha (igual que SW2) */
  if (switches & (1<<2)) {
    short newCol = shapeCol + BLOCK_SIZE;
    int canMove = TRUE;
    for (int i = 0; i < 4; i++) {
      int ox = shapes[shapeIndex][i].x;
      int oy = shapes[shapeIndex][i].y;
      int rx, ry;
      switch(shapeRotation) {
        case 0: rx = ox;  ry = oy;  break;
        case 1: rx = -oy; ry = ox;  break;
        case 2: rx = -ox; ry = -oy; break;
        case 3: rx = oy;  ry = -ox; break;
      }
      int x = newCol + rx * BLOCK_SIZE;
      int y = shapeRow + ry * BLOCK_SIZE;
      int c = x / BLOCK_SIZE;
      int r = y / BLOCK_SIZE;
      if (c >= numColumns || (r >= 0 && grid[c][r])) { canMove = FALSE; break; }
    }
    if (canMove) { shapeCol = newCol; redrawScreen = TRUE; }
  }

  /* SW4: reinicio */
  if (switches & (1<<3)) {
    clearScreen(BG_COLOR);
    memset(grid, 0, sizeof grid);
    shapeIndex    = 0;
    shapeRotation = 0;
    colIndex      = 0;
    shapeCol      = 0;
    shapeRow      = -BLOCK_SIZE * 4;
    redrawScreen  = TRUE;
  }
}

void __interrupt_vec(PORT2_VECTOR) Port_2() {
  if (P2IFG & SWITCHES) {
    P2IFG &= ~SWITCHES;
    switch_interrupt_handler();
  }
}

// --------------------------------------------------
// Dibuja una pieza con rotación
// --------------------------------------------------
static void draw_piece(short col, short row, char idx, char rot, unsigned short color) {
  for (int i = 0; i < 4; i++) {
    int ox = shapes[idx][i].x;
    int oy = shapes[idx][i].y;
    int rx, ry;
    switch(rot) {
      case 0: rx = ox;  ry = oy;  break;
      case 1: rx = -oy; ry = ox;  break;
      case 2: rx = -ox; ry = -oy; break;
      case 3: rx = oy;  ry = -ox; break;
      default: rx = ox; ry = oy; break;
    }
    int x = col + rx * BLOCK_SIZE;
    int y = row + ry * BLOCK_SIZE;
    fillRectangle(x, y, BLOCK_SIZE, BLOCK_SIZE, color);
  }
}

// --------------------------------------------------
// Actualiza la pieza móvil
// --------------------------------------------------
static void update_moving_shape(void) {
  if (lastIdx >= 0 && !pieceStoppedFlag) {
    draw_piece(lastCol, lastRow, lastIdx, lastRot, BG_COLOR);
  }
  draw_piece(shapeCol, shapeRow, shapeIndex, shapeRotation, shapeColors[shapeIndex]);
  lastCol = shapeCol; lastRow = shapeRow;
  lastIdx = shapeIndex; lastRot = shapeRotation;
  pieceStoppedFlag = FALSE;
}

// --------------------------------------------------
// Interrupción del WDT: caída de pieza
// --------------------------------------------------
void wdt_c_handler() {
  static int tick = 0; if (++tick < 64) return; tick = 0;
  short newRow = shapeRow + BLOCK_SIZE; int collided = FALSE;
  for (int i = 0; i < 4; i++) {
    int ox = shapes[shapeIndex][i].x, oy = shapes[shapeIndex][i].y;
    int rx, ry;
    switch(shapeRotation) { case 0: rx = ox; ry = oy; break;
      case 1: rx = -oy; ry = ox; break;
      case 2: rx = -ox; ry = -oy; break;
      case 3: rx = oy; ry = -ox; break;
      default: rx = ox; ry = oy; }
    int x = shapeCol + rx*BLOCK_SIZE, y = newRow + ry*BLOCK_SIZE;
    int c = x/BLOCK_SIZE, r = y/BLOCK_SIZE;
    if (r >= numRows || (r >= 0 && grid[c][r])) { collided = TRUE; break; }
  }
  if (!collided) shapeRow = newRow;
  else {
    if (shapeRow < 0) {
      clearScreen(BG_COLOR); memset(grid, 0, sizeof grid);
      shapeIndex=0; shapeRotation=0; colIndex=0;
      shapeCol=0; shapeRow=-BLOCK_SIZE*4; redrawScreen=TRUE; return; }
    for (int i=0;i<4;i++){int ox=shapes[shapeIndex][i].x, oy=shapes[shapeIndex][i].y;
      int rx, ry;
      switch(shapeRotation){case 0: rx=ox; ry=oy; break;
        case 1: rx=-oy; ry=ox; break;
        case 2: rx=-ox; ry=-oy; break;
        case 3: rx=oy; ry=-ox; break;
        default: rx=ox; ry=oy; }
      int x=shapeCol+rx*BLOCK_SIZE, y=shapeRow+ry*BLOCK_SIZE;
      int c=x/BLOCK_SIZE, r=y/BLOCK_SIZE;
      if (r>=0 && r<numRows) grid[c][r]=1; }
    draw_piece(shapeCol,shapeRow,shapeIndex,shapeRotation,shapeColors[shapeIndex]);
    pieceStoppedFlag=TRUE; shapeIndex=(shapeIndex+1)%NUM_SHAPES;
    shapeRotation=0; colIndex=(colIndex+1)%numColumns;
    shapeCol=colIndex*BLOCK_SIZE; shapeRow=-BLOCK_SIZE*4;
  }
  redrawScreen=TRUE;
}

// --------------------------------------------------
// Función principal
// --------------------------------------------------
int main(){ P1DIR|=BIT6; P1OUT|=BIT6;
  configureClocks(); lcd_init(); clearScreen(BG_COLOR);
  switch_init(); memset(grid,0,sizeof grid);
  shapeIndex=0; shapeRotation=0; colIndex=0;
  shapeCol=0; shapeRow=-BLOCK_SIZE*4;
  enableWDTInterrupts(); or_sr(0x8);
  while(TRUE){ if(redrawScreen){ redrawScreen=FALSE; update_moving_shape(); }
    P1OUT&=~BIT6; or_sr(0x10); P1OUT|=BIT6;} }
