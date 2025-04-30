#include <msp430.h>
#include <libTimer.h>
#include <stdio.h>
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

unsigned short shapeColors[NUM_SHAPES] = {
  COLOR_RED, COLOR_GREEN, COLOR_ORANGE, COLOR_BLUE
};
#define BG_COLOR      COLOR_BLACK

// ** Nuevo: contador de puntaje **
static int score = 0;

// --------------------------------------------------
// Prototipos
// --------------------------------------------------
static void draw_piece(short col, short row, char idx, char rot, unsigned short color);
static void draw_grid(void);
static void clear_full_rows(void);
static void draw_score_label(void);
static int  check_collision(short testCol, short testRow, char idx, char rot);
static void update_moving_shape(void);

// --------------------------------------------------
// Dibuja la etiqueta y el valor de SCORE
// --------------------------------------------------
static void draw_score_label(void) {
  char buf[16];
  snprintf(buf, sizeof(buf), "SCORE:%d", score);
  int len = strlen(buf);
  int x = SCREEN_WIDTH - len * 5;  // 5 px por carácter
  int y = SCREEN_HEIGHT - 7;       // justo encima del borde
  drawString5x7(x, y, buf, COLOR_WHITE, BG_COLOR);
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
      case 1: rx = -oy; ry =  ox; break;
      case 2: rx = -ox; ry = -oy; break;
      case 3: rx =  oy; ry = -ox; break;
      default: rx = ox;  ry =  oy; break;
    }
    fillRectangle(col + rx*BLOCK_SIZE,
                  row + ry*BLOCK_SIZE,
                  BLOCK_SIZE, BLOCK_SIZE,
                  color);
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
        fillRectangle(c * BLOCK_SIZE,
                      r * BLOCK_SIZE,
                      BLOCK_SIZE, BLOCK_SIZE,
                      shapeColors[idx]);
      }
    }
  }
}

// --------------------------------------------------
// Comprueba colisión para posición/rotación dada
// --------------------------------------------------
static int check_collision(short testCol, short testRow, char idx, char rot) {
  for (int i = 0; i < 4; i++) {
    int ox = shapes[idx][i].x;
    int oy = shapes[idx][i].y;
    int rx, ry;
    switch(rot) {
      case 1: rx = -oy; ry =  ox; break;
      case 2: rx = -ox; ry = -oy; break;
      case 3: rx =  oy; ry = -ox; break;
      default: rx = ox;  ry =  oy; break;
    }
    int c = (testCol + rx*BLOCK_SIZE) / BLOCK_SIZE;
    int r = (testRow + ry*BLOCK_SIZE) / BLOCK_SIZE;
    if (c < 0 || c >= numColumns || r >= numRows) return TRUE;
    if (r >= 0 && grid[c][r] >= 0)      return TRUE;
  }
  return FALSE;
}

// --------------------------------------------------
// Elimina filas completas, sube las de arriba y suma puntaje
// --------------------------------------------------
static void clear_full_rows(void) {
  for (int r = 0; r < numRows; r++) {
    int full = TRUE;
    for (int c = 0; c < numColumns; c++) {
      if (grid[c][r] < 0) { full = FALSE; break; }
    }
    if (full) {
      // Mueve todo hacia abajo
      for (int rr = r; rr > 0; rr--) {
        for (int c = 0; c < numColumns; c++) {
          grid[c][rr] = grid[c][rr-1];
        }
      }
      // Limpia la fila superior
      for (int c = 0; c < numColumns; c++) {
        grid[c][0] = -1;
      }
      // Aumenta el puntaje
      score += 100;
      clearScreen(BG_COLOR);
      draw_grid();
      draw_score_label();
      r--;  // revisa de nuevo esta fila
    }
  }
}

// --------------------------------------------------
// Actualiza la pieza en movimiento en pantalla
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
// Manejo de botones con debounce y lógica de juego
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

  // Izquierda (SW1)
  if (switches & (1<<0)) {
    short nc = shapeCol - BLOCK_SIZE;
    if (!check_collision(nc, shapeRow, shapeIndex, shapeRotation)) {
      shapeCol = nc;
    }
  }
  // Rotar (SW2)
  if (switches & (1<<1)) {
    char nr = (shapeRotation + 1) & 3;
    if (!check_collision(shapeCol, shapeRow, shapeIndex, nr)) {
      shapeRotation = nr;
    }
  }
  // Reiniciar manual (SW3)
  if (switches & (1<<2)) {
    clearScreen(BG_COLOR);
    memset(grid, -1, sizeof grid);
    shapeIndex = shapeRotation = colIndex = 0;
    shapeCol = 0; shapeRow = -BLOCK_SIZE*4;
    score = 0;
    draw_score_label();
  }
  // Derecha (SW4)
  if (switches & (1<<3)) {
    short nc = shapeCol + BLOCK_SIZE;
    if (!check_collision(nc, shapeRow, shapeIndex, shapeRotation)) {
      shapeCol = nc;
    }
  }

  redrawScreen = TRUE;
  P2IFG = 0;
  P2IE |= SWITCHES;
}

void __interrupt_vec(PORT2_VECTOR) Port_2(void) {
  if (P2IFG & SWITCHES) switch_interrupt_handler();
}

// --------------------------------------------------
// WDT: caída, apilamiento y game over
// --------------------------------------------------
void wdt_c_handler(void) {
  static int tick = 0;
  if (++tick < 64) return;
  tick = 0;

  // Intenta bajar
  short newRow = shapeRow + BLOCK_SIZE;
  if (!check_collision(shapeCol, newRow, shapeIndex, shapeRotation)) {
    shapeRow = newRow;
  } else {
    // Si choca en spawn → game over
    if (shapeRow < 0) {
      clearScreen(BG_COLOR);
      memset(grid, -1, sizeof grid);
      shapeIndex = shapeRotation = colIndex = 0;
      shapeCol = 0; shapeRow = -BLOCK_SIZE*4;
      score = 0;
      draw_score_label();
      return;
    }
    // Fija la pieza
    for (int i = 0; i < 4; i++) {
      int ox = shapes[shapeIndex][i].x;
      int oy = shapes[shapeIndex][i].y;
      int rx, ry;
      switch(shapeRotation) {
        case 1: rx = -oy; ry =  ox; break;
        case 2: rx = -ox; ry = -oy; break;
        case 3: rx =  oy; ry = -ox; break;
        default: rx = ox;  ry =  oy; break;
      }
      int c = (shapeCol + rx*BLOCK_SIZE)/BLOCK_SIZE;
      int r = (shapeRow + ry*BLOCK_SIZE)/BLOCK_SIZE;
      if (r >= 0 && r < numRows) grid[c][r] = shapeIndex;
    }
    draw_grid();
    clear_full_rows();
    pieceStoppedFlag = TRUE;

    // Nuevo bloque
    shapeIndex = (shapeIndex + 1) % NUM_SHAPES;
    shapeRotation = 0;
    colIndex = (colIndex + 1) % numColumns;
    shapeCol = colIndex * BLOCK_SIZE;
    shapeRow = -BLOCK_SIZE*4;
  }

  redrawScreen = TRUE;
}

// --------------------------------------------------
// main
// --------------------------------------------------
int main(void) {
  P1DIR |= BIT6; P1OUT |= BIT6;
  configureClocks();
  lcd_init();
  clearScreen(BG_COLOR);
  score = 0;
  draw_score_label();
  switch_init();
  memset(grid, -1, sizeof grid);
  shapeIndex = shapeRotation = colIndex = 0;
  shapeCol = 0; shapeRow = -BLOCK_SIZE*4;
  enableWDTInterrupts();
  or_sr(0x8);
  while (TRUE) {
    if (redrawScreen) {
      redrawScreen = FALSE;
      update_moving_shape();
    }
    P1OUT &= ~BIT6; or_sr(0x10); P1OUT |= BIT6;
  }
}
