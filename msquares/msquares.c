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

static short shapeCol, shapeRow;
static char  shapeIndex    = 0;
static char  shapeRotation = 0;
static char  colIndex      = 0;

static short lastCol = 0, lastRow = 0;
static char  lastIdx  = -1;
static char  lastRot  = 0;
static unsigned int rand_state = 12345; // semilla inicial cualquiera

unsigned short shapeColors[NUM_SHAPES] = {
  COLOR_RED, COLOR_GREEN, COLOR_ORANGE, COLOR_BLUE
};
#define BG_COLOR      COLOR_BLACK

// Prototipos
static void draw_piece(short col, short row, char idx, char rot, unsigned short color);
static void draw_grid(void);
static void clear_full_rows(void);

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
    fillRectangle(col + rx*BLOCK_SIZE, row + ry*BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE, color);
  }
}

// --------------------------------------------------
// Dibuja todas las piezas estáticas en la rejilla
// --------------------------------------------------
static void draw_grid(void) {
  for (int c = 0; c < numColumns; c++) {
    for (int r = 0; r < numRows; r++) {
      signed char idx = grid[c][r];
      if (idx >= 0) {
        fillRectangle(c * BLOCK_SIZE, r * BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE, shapeColors[idx]);
      }
    }
  }
}

unsigned int simple_rand(unsigned int max) {
    rand_state = rand_state * 1103515245 + 12345;
    return (rand_state >> 16) % max;
}

// --------------------------------------------------
// Elimina filas completas y desplaza las de arriba
// --------------------------------------------------
static void clear_full_rows(void) {
  for (int r = 0; r < numRows; r++) {
    int full = TRUE;
    for (int c = 0; c < numColumns; c++) {
      if (grid[c][r] < 0) { full = FALSE; break; }
    }
    if (full) {
      for (int rr = r; rr > 0; rr--) {
        for (int c = 0; c < numColumns; c++) {
          grid[c][rr] = grid[c][rr-1];
        }
      }
      for (int c = 0; c < numColumns; c++) grid[c][0] = -1;
      clearScreen(BG_COLOR);
      draw_grid();
      r--;  // revisar misma fila tras desplazamiento
    }
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
// Botones con debounce (msquares.c)
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
  // Debounce: deshabilitar interrupción y pequeña espera
  P2IE &= ~SWITCHES;
  __delay_cycles(50000);  
  char p2val = switch_update_interrupt_sense();
  switches = ~p2val & SWITCHES;

  // Borrar pieza antigua
  if (lastIdx >= 0) draw_piece(lastCol, lastRow, lastIdx, lastRot, BG_COLOR);
  pieceStoppedFlag = FALSE;

  // SW1: mover izquierda
  if (switches & (1<<0)) {
    short newCol = shapeCol - BLOCK_SIZE;
    int canMove = TRUE;
    for (int i = 0; i < 4; i++) {
      int ox = shapes[shapeIndex][i].x, oy = shapes[shapeIndex][i].y;
      int rx = (shapeRotation==1? -oy : shapeRotation==2? -ox : shapeRotation==3? oy:ox);
      int ry = (shapeRotation==1? ox : shapeRotation==2? -oy: shapeRotation==3? -ox:oy);
      int c = (newCol + rx*BLOCK_SIZE)/BLOCK_SIZE;
      int r = (shapeRow + ry*BLOCK_SIZE)/BLOCK_SIZE;
      if (c<0 || (r>=0 && grid[c][r]>=0)) { canMove = FALSE; break; }
    }
    if (canMove) shapeCol = newCol;
  }

  // SW2: rotar
  if (switches & (1<<1)) {
    char newRot = (shapeRotation + 1) % 4;
    int canRotate = TRUE;
    for (int i = 0; i < 4; i++) {
      int ox = shapes[shapeIndex][i].x, oy = shapes[shapeIndex][i].y;
      int rx = (newRot==1? -oy:newRot==2?-ox:newRot==3?oy:ox);
      int ry = (newRot==1? ox:newRot==2?-oy:newRot==3?-ox:oy);
      int c = (shapeCol + rx*BLOCK_SIZE)/BLOCK_SIZE;
      int r = (shapeRow + ry*BLOCK_SIZE)/BLOCK_SIZE;
      if (c<0||c>=numColumns||r>=numRows||(r>=0&&grid[c][r]>=0)) { canRotate=FALSE; break; }
    }
    if (canRotate) shapeRotation = newRot;
  }

  // SW3: reiniciar manual
  if (switches & (1<<2)) {
    clearScreen(BG_COLOR);
    memset(grid, -1, sizeof grid);
    shapeIndex = shapeRotation = colIndex = 0;
    shapeCol = 0; shapeRow = -BLOCK_SIZE*4;
  }

  // SW4: mover derecha
  if (switches & (1<<3)) {
    short newCol = shapeCol + BLOCK_SIZE;
    int canMove = TRUE;
    for (int i = 0; i < 4; i++) {
      int ox = shapes[shapeIndex][i].x, oy = shapes[shapeIndex][i].y;
      int rx = (shapeRotation==1? -oy : shapeRotation==2? -ox: shapeRotation==3?oy:ox);
      int ry = (shapeRotation==1? ox : shapeRotation==2? -oy: shapeRotation==3?-ox:oy);
      int c = (newCol + rx*BLOCK_SIZE)/BLOCK_SIZE;
      int r = (shapeRow + ry*BLOCK_SIZE)/BLOCK_SIZE;
      if (c>=numColumns || (r>=0 && grid[c][r]>=0)) { canMove=FALSE; break; }
    }
    if (canMove) shapeCol = newCol;
  }

  redrawScreen = TRUE;
  // Rehabilitar interrupción
  P2IFG = 0;
  P2IE |= SWITCHES;
}

void __interrupt_vec(PORT2_VECTOR) Port_2() {
  if (P2IFG & SWITCHES) switch_interrupt_handler();
}

// --------------------------------------------------
// WDT: caída, apilamiento y game over
// --------------------------------------------------
void wdt_c_handler() {
  static int tick = 0;
  if (++tick < 64) return;
  tick = 0;

  short newRow = shapeRow + BLOCK_SIZE;
  int collided = FALSE;
  for (int i = 0; i < 4; i++) {
    int ox = shapes[shapeIndex][i].x, oy = shapes[shapeIndex][i].y;
    int rx = (shapeRotation==1? -oy : shapeRotation==2?-ox: shapeRotation==3?oy:ox);
    int ry = (shapeRotation==1? ox : shapeRotation==2?-oy: shapeRotation==3?-ox:oy);
    int c = (shapeCol + rx*BLOCK_SIZE)/BLOCK_SIZE;
    int r = (newRow + ry*BLOCK_SIZE)/BLOCK_SIZE;
    if (r>=numRows || (r>=0 && grid[c][r]>=0)) { collided = TRUE; break; }
  }

  if (!collided) {
    shapeRow = newRow;
  } else {
    // Game Over sólo si pieza ni siquiera entra mínimamente
    if (shapeRow < -BLOCK_SIZE*2) {
      clearScreen(BG_COLOR);
      memset(grid, -1, sizeof grid);
      shapeIndex = simple_rand(NUM_SHAPES);
      shapeRotation = 0;
      shapeCol = ((numColumns / 2) - 1) * BLOCK_SIZE; // columna central
      shapeRow = -BLOCK_SIZE * 4;
      return;
    }
    // fijar pieza en rejilla
    for (int i = 0; i < 4; i++) {
      int ox = shapes[shapeIndex][i].x, oy = shapes[shapeIndex][i].y;
      int rx = (shapeRotation==1? -oy: shapeRotation==2?-ox: shapeRotation==3?oy:ox);
      int ry = (shapeRotation==1? ox: shapeRotation==2?-oy: shapeRotation==3?-ox:oy);
      int c = (shapeCol + rx*BLOCK_SIZE)/BLOCK_SIZE;
      int r = (shapeRow + ry*BLOCK_SIZE)/BLOCK_SIZE;
      if (r>=0 && r<numRows) grid[c][r] = shapeIndex;
    }
    draw_grid();
    clear_full_rows();
    pieceStoppedFlag = TRUE;
    shapeIndex = simple_rand(NUM_SHAPES);
    shapeRotation = 0;
    shapeCol = ((numColumns / 2) - 1) * BLOCK_SIZE; // columna central
    shapeRow = -BLOCK_SIZE * 4;
  }
  redrawScreen = TRUE;
}

// --------------------------------------------------
// main
// --------------------------------------------------
int main(void) {
    configureClocks();
    lcd_init();
    clearScreen(BG_COLOR);
    score = 0;
    draw_score_label();

    // Combina varios registros del timer para inicializar la semilla:
    rand_state = TA0R ^ (TA1R << 8) ^ (TA0CCR0 << 4);

    switch_init();
    memset(grid, -1, sizeof grid);
    shapeIndex = simple_rand(NUM_SHAPES);
    shapeRotation = 0;
    shapeCol = ((numColumns / 2) - 1) * BLOCK_SIZE;
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

