#include <msp430.h>
#include <libTimer.h>
#include <string.h>
#include <stdio.h>
#include "lcdutils.h"
#include "lcddraw.h"

// --------------------------------------------------
// Configuración de pantalla, rejilla y spawneo
// --------------------------------------------------
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   160
#define BLOCK_SIZE       10

#define MAX_COLUMNS    (SCREEN_WIDTH  / BLOCK_SIZE)
#define MAX_ROWS       (SCREEN_HEIGHT / BLOCK_SIZE)
// Spawn a 10 píxeles debajo del borde superior
#define SPAWN_START_Y   (10)

// --------------------------------------------------
// Formas Tetris
// --------------------------------------------------
typedef struct { short x, y; } Offset;
const Offset shapes[][4] = {
  {{0,0},{1,0},{0,1},{1,1}},  // O
  {{0,0},{1,0},{2,0},{3,0}},  // I
  {{0,0},{0,1},{1,1},{2,1}},  // L invertida
  {{1,0},{0,1},{1,1},{2,1}}   // T
};
#define NUM_SHAPES  (sizeof(shapes)/sizeof(shapes[0]))

// --------------------------------------------------
// Variables globales
// --------------------------------------------------
static signed char grid[MAX_COLUMNS][MAX_ROWS];
static const int numColumns = MAX_COLUMNS;
static const int numRows    = MAX_ROWS;

enum { FALSE = 0, TRUE = 1 };
volatile int redrawScreen     = TRUE;

static short shapeCol, shapeRow;
static char  shapeIndex;
static char  shapeRotation;
static char  colIndex;

static unsigned int rndState = 0xACE1u; // semilla LFSR
static int score = 0;
static volatile int switches;
unsigned short shapeColors[NUM_SHAPES] = {
  COLOR_RED, COLOR_GREEN, COLOR_ORANGE, COLOR_BLUE
};
#define BG_COLOR      COLOR_BLACK

// --------------------------------------------------
// Prototipos
// --------------------------------------------------
static void draw_piece(short col, short row, char idx, char rot, unsigned short color);
static void draw_grid(void);
static void clear_full_rows(void);
static void draw_score_label(void);
static void spawn_new_piece(void);
static void update_frame(void);
static unsigned int lfsr_rand(void);
static char switch_update_interrupt_sense(void);
static void switch_interrupt_handler(void);

// --------------------------------------------------
// Generador LFSR para piezas
// --------------------------------------------------
static unsigned int lfsr_rand(void) {
  unsigned int lsb = rndState & 1;
  rndState >>= 1;
  if (lsb) rndState ^= 0xB400u;
  return rndState;
}

// --------------------------------------------------
// Dibuja marcador en esquina superior izquierda
// --------------------------------------------------
static void draw_score_label(void) {
  char buf[16];
  sprintf(buf, "SCORE:%d", score);
  drawString5x7(0, 0, buf, COLOR_WHITE, BG_COLOR);
}

// --------------------------------------------------
// Dibuja pieza activa
// --------------------------------------------------
static void draw_piece(short col, short row, char idx, char rot, unsigned short color) {
  for (int i = 0; i < 4; i++) {
    int ox = shapes[idx][i].x;
    int oy = shapes[idx][i].y;
    int rx, ry;
    switch (rot) {
      case 0: rx = ox;  ry = oy;  break;
      case 1: rx = -oy; ry = ox;  break;
      case 2: rx = -ox; ry = -oy; break;
      case 3: rx = oy;  ry = -ox; break;
      default: rx = ox; ry = oy; break;
    }
    fillRectangle(col + rx*BLOCK_SIZE, row + ry*BLOCK_SIZE,
                  BLOCK_SIZE, BLOCK_SIZE, color);
  }
}

// --------------------------------------------------
// Dibuja grid estático
// --------------------------------------------------
static void draw_grid(void) {
  for (int c = 0; c < numColumns; c++) {
    for (int r = 0; r < numRows; r++) {
      signed char idx = grid[c][r];
      if (idx >= 0) {
        fillRectangle(c*BLOCK_SIZE, r*BLOCK_SIZE,
                      BLOCK_SIZE, BLOCK_SIZE, shapeColors[idx]);
      }
    }
  }
}

// --------------------------------------------------
// Elimina filas completas y suma 50 puntos
// --------------------------------------------------
static void clear_full_rows(void) {
  for (int r = 0; r < numRows; r++) {
    int full = TRUE;
    for (int c = 0; c < numColumns; c++) {
      if (grid[c][r] < 0) { full = FALSE; break; }
    }
    if (full) {
      score += 50;
      for (int rr = r; rr > 0; rr--)
        for (int c = 0; c < numColumns; c++)
          grid[c][rr] = grid[c][rr-1];
      for (int c = 0; c < numColumns; c++) grid[c][0] = -1;
      r--;
    }
  }
}

// --------------------------------------------------
// Genera nueva pieza aleatoria
// --------------------------------------------------
static void spawn_new_piece(void) {
  shapeIndex = lfsr_rand() % NUM_SHAPES;
  shapeRotation = 0;
  colIndex = lfsr_rand() % numColumns;
  shapeCol = colIndex * BLOCK_SIZE;
  shapeRow = SPAWN_START_Y;
  // Si colisiona al spawnear, reiniciar grid
  for (int i = 0; i < 4; i++) {
    int ox = shapes[shapeIndex][i].x;
    int oy = shapes[shapeIndex][i].y;
    int c = (shapeCol + ox*BLOCK_SIZE)/BLOCK_SIZE;
    int r = (shapeRow + oy*BLOCK_SIZE)/BLOCK_SIZE;
    if (r >= 0 && grid[c][r] >= 0) {
      memset(grid, -1, sizeof grid);
      score = 0;
      break;
    }
  }
}

// --------------------------------------------------
// Redibuja pantalla entera para evitar ghosting
// --------------------------------------------------
static void update_frame(void) {
  clearScreen(BG_COLOR);
  draw_grid();
  draw_piece(shapeCol, shapeRow, shapeIndex, shapeRotation, shapeColors[shapeIndex]);
  draw_score_label();
}

// --------------------------------------------------
// Manejador de WDT: caída y apilamiento
// --------------------------------------------------
void wdt_c_handler(void) {
  static int tick = 0;
  if (++tick < 64) return;
  tick = 0;
  short newRow = shapeRow + BLOCK_SIZE;
  int collided = FALSE;
  for (int i = 0; i < 4; i++) {
    int ox = shapes[shapeIndex][i].x;
    int oy = shapes[shapeIndex][i].y;
    int rx = (shapeRotation==1?-oy:shapeRotation==2?-ox:shapeRotation==3?oy:ox);
    int ry = (shapeRotation==1?ox:shapeRotation==2?-oy:shapeRotation==3?-ox:oy);
    int px = shapeCol + rx*BLOCK_SIZE;
    int py = newRow + ry*BLOCK_SIZE;
    if (px < 0 || px + BLOCK_SIZE > SCREEN_WIDTH || py + BLOCK_SIZE > SCREEN_HEIGHT) { collided = TRUE; break; }
    int c = px/BLOCK_SIZE, r = py/BLOCK_SIZE;
    if (r >= 0 && grid[c][r] >= 0) { collided = TRUE; break; }
  }
  if (!collided) {
    shapeRow = newRow;
  } else {
    // fijar pieza
    for (int i = 0; i < 4; i++) {
      int ox = shapes[shapeIndex][i].x;
      int oy = shapes[shapeIndex][i].y;
      int rx = (shapeRotation==1?-oy:shapeRotation==2?-ox:shapeRotation==3?oy:ox);
      int ry = (shapeRotation==1?ox:shapeRotation==2?-oy:shapeRotation==3?-ox:oy);
      int c = (shapeCol + rx*BLOCK_SIZE)/BLOCK_SIZE;
      int r = (shapeRow + ry*BLOCK_SIZE)/BLOCK_SIZE;
      if (r >= 0) grid[c][r] = shapeIndex;
    }
    clear_full_rows();
    spawn_new_piece();
  }
  update_frame();
}

// --------------------------------------------------
// Debounce y lógica de botones
// --------------------------------------------------
#define SWITCHES 15
#define SW1 1
#define SW2 2
#define SW3 4
#define SW4 8
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
  // eliminar ghosting pieza previa
  draw_piece(shapeCol, shapeRow, shapeIndex, shapeRotation, BG_COLOR);
  // SW1: izquierda
  if (switches & SW1) {
    short newCol = shapeCol - BLOCK_SIZE;
    // colisión pared y grid
    if (newCol >= 0) shapeCol = newCol;
  }
  // SW2: rotar
  if (switches & SW2) {
    char newRot = (shapeRotation + 1) % 4;
    shapeRotation = newRot;
  }
  // SW3: reiniciar manual
  if (switches & SW3) {
    memset(grid, -1, sizeof grid);
    score = 0;
    spawn_new_piece();
  }
  // SW4: derecha
  if (switches & SW4) {
    short newCol = shapeCol + BLOCK_SIZE;
    if (newCol + BLOCK_SIZE <= SCREEN_WIDTH) shapeCol = newCol;
  }
  update_frame();
  P2IFG = 0; P2IE |= SWITCHES;
}

void __interrupt_vec(PORT2_VECTOR) Port_2(void) {
  if (P2IFG & SWITCHES) switch_interrupt_handler();
}

// --------------------------------------------------
// main
// --------------------------------------------------
int main(void) {
  P1DIR |= BIT6; P1OUT |= BIT6;
  configureClocks(); lcd_init();
  clearScreen(BG_COLOR);
  memset(grid, -1, sizeof grid);
  score = 0;
  spawn_new_piece();
  update_frame();

  switch_init();
  enableWDTInterrupts(); or_sr(0x8);
  while (TRUE) {
    // entrar en LPM y parpadear LED
    P1OUT &= ~BIT6; or_sr(0x10); P1OUT |= BIT6;
  }
}
