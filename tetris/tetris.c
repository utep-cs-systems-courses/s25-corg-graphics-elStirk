#include <msp430.h>
#include <libTimer.h>
#include <string.h>
#include "lcdutils.h"
#include "lcddraw.h"

// --------------------------------------------------
// Configuración de pantalla y rejilla
// --------------------------------------------------
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   160
#define BLOCK_SIZE      10

#define MAX_COLUMNS     (SCREEN_WIDTH  / BLOCK_SIZE)
#define MAX_ROWS        (SCREEN_HEIGHT / BLOCK_SIZE)

// Máscara de los 4 switches en P2 (SW1..SW4 = BIT0..BIT3)
#define SWITCHES        (BIT0 | BIT1 | BIT2 | BIT3)

// Constantes booleanas
#define TRUE            1
#define FALSE           0

// Tiempo aproximado de “long-press” de 3 segundos (MCLK = 1 MHz)
#define LONG_PRESS_CYCLES 3000000  

// --------------------------------------------------
// Definición de tetrominós
// --------------------------------------------------
typedef struct { int8_t x, y; } Point;
static const Point shapes[][4] = {
  // I
  {{0,1},{0,0},{0,-1},{0,-2}},
  // O
  {{0,0},{1,0},{0,-1},{1,-1}},
  // L
  {{0,1},{0,0},{0,-1},{1,-1}},
  // Z
  {{-1,0},{0,0},{0,-1},{1,-1}}
};
#define NUM_SHAPES (sizeof shapes / sizeof shapes[0])

// Colores para cada pieza
unsigned short shapeColors[NUM_SHAPES] = {
  COLOR_RED, COLOR_GREEN, COLOR_ORANGE, COLOR_BLUE
};
#define BG_COLOR      COLOR_BLACK

// --------------------------------------------------
// Variables de estado del juego
// --------------------------------------------------
static int grid[MAX_COLUMNS][MAX_ROWS];
static int numColumns = MAX_COLUMNS, numRows = MAX_ROWS;
static char shapeIndex, shapeRotation, colIndex;      // <-- colIndex añadido
static short shapeCol, shapeRow;
static char lastIdx, lastRot;
static short lastCol, lastRow;
static int pieceStoppedFlag = FALSE;
static int redrawScreen = FALSE;
static unsigned char switches;

// --------------------------------------------------
// Prototipos
// --------------------------------------------------
static void draw_piece(short col, short row, char idx, char rot, unsigned short color);
static void draw_grid(void);
static void clear_full_rows(void);
static void draw_score_label(void);
static void update_moving_shape(void);
static char switch_update_interrupt_sense(void);
static void switch_init(void);
void switch_interrupt_handler(void);

// --------------------------------------------------
// Funciones de dibujo y lógica
// --------------------------------------------------
static void draw_score_label(void) {
  const char *label = "SCORE:";
  int len = strlen(label);
  drawString5x7(0, 0, (char *)label, COLOR_WHITE, BG_COLOR);
}

static void draw_grid(void) {
  for (int c = 0; c < numColumns; c++) {
    for (int r = 0; r < numRows; r++) {
      unsigned short color = (grid[c][r] < 0 ? BG_COLOR : shapeColors[grid[c][r]]);
      drawRect(c*BLOCK_SIZE, r*BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE, color);
    }
  }
}

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
      draw_score_label();
      r--;
    }
  }
}

static void draw_piece(short col, short row, char idx, char rot, unsigned short color) {
  for (int i = 0; i < 4; i++) {
    int ox = shapes[idx][i].x, oy = shapes[idx][i].y;
    int rx, ry;
    switch (rot) {
      case 1: rx = -oy; ry = ox; break;
      case 2: rx = -ox; ry = -oy; break;
      case 3: rx = oy;  ry = -ox; break;
      default: rx = ox; ry = oy; break;
    }
    drawRect(col + rx*BLOCK_SIZE,
             row + ry*BLOCK_SIZE,
             BLOCK_SIZE, BLOCK_SIZE,
             color);
  }
}

static void update_moving_shape(void) {
  if (lastIdx >= 0 && !pieceStoppedFlag) {
    draw_piece(lastCol, lastRow, lastIdx, lastRot, BG_COLOR);
  }
  draw_piece(shapeCol, shapeRow, shapeIndex, shapeRotation, shapeColors[shapeIndex]);
}

void wdt_c_handler(void) {
  static int tick = 0;
  if (++tick < 64) return;
  tick = 0;
  short newRow = shapeRow + BLOCK_SIZE;
  int collided = FALSE;
  for (int i = 0; i < 4; i++) {
    int ox = shapes[shapeIndex][i].x;
    int oy = shapes[shapeIndex][i].y;
    int rx = (shapeRotation==1? -oy:
             (shapeRotation==2? -ox:
             (shapeRotation==3? oy: ox)));
    int ry = (shapeRotation==1?  ox:
             (shapeRotation==2? -oy:
             (shapeRotation==3? -ox: oy)));
    int c = (shapeCol + rx*BLOCK_SIZE)/BLOCK_SIZE;
    int r = (newRow + ry*BLOCK_SIZE)/BLOCK_SIZE;
    if (r >= numRows || (r >= 0 && grid[c][r] >= 0)) {
      collided = TRUE; break;
    }
  }
  if (!collided) {
    shapeRow = newRow;
  } else {
    for (int i = 0; i < 4; i++) {
      int ox = shapes[shapeIndex][i].x;
      int oy = shapes[shapeIndex][i].y;
      int rx = (shapeRotation==1? -oy:
               (shapeRotation==2? -ox:
               (shapeRotation==3? oy: ox)));
      int ry = (shapeRotation==1?  ox:
               (shapeRotation==2? -oy:
               (shapeRotation==3? -ox: oy)));
      int c = (shapeCol + rx*BLOCK_SIZE)/BLOCK_SIZE;
      int r = (shapeRow + ry*BLOCK_SIZE)/BLOCK_SIZE;
      if (r >= 0 && c >= 0 && c < numColumns && r < numRows) {
        grid[c][r] = shapeIndex;
      }
    }
    clear_full_rows();
    lastIdx = shapeIndex; lastRot = shapeRotation;
    lastCol = shapeCol;   lastRow = shapeRow;
    pieceStoppedFlag = TRUE;
    // nueva pieza
    colIndex = (colIndex + 1) % NUM_SHAPES;
    shapeIndex  = colIndex;
    shapeRotation = 0;
    shapeCol    = 0;
    shapeRow    = -BLOCK_SIZE*4;
  }
  redrawScreen = TRUE;
}

// --------------------------------------------------
// Manejo de switches
// --------------------------------------------------
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
  if (lastIdx >= 0) {
    draw_piece(lastCol, lastRow, lastIdx, lastRot, BG_COLOR);
  }
  pieceStoppedFlag = FALSE;

  // SW1: mover izquierda
  if (switches & BIT0) {
    short newCol = shapeCol - BLOCK_SIZE;
    int valid = TRUE;
    for (int i = 0; i < 4; i++) {
      int ox = shapes[shapeIndex][i].x;
      int oy = shapes[shapeIndex][i].y;
      int rx = (shapeRotation==1? -oy:
               (shapeRotation==2? -ox:
               (shapeRotation==3? oy: ox)));
      int ry = (shapeRotation==1?  ox:
               (shapeRotation==2? -oy:
               (shapeRotation==3? -ox: oy)));
      int c = (newCol + rx*BLOCK_SIZE)/BLOCK_SIZE;
      int r = (shapeRow + ry*BLOCK_SIZE)/BLOCK_SIZE;
      if (c < 0 || (r >= 0 && grid[c][r] >= 0)) {
        valid = FALSE; break;
      }
    }
    if (valid) shapeCol = newCol;
  }

  // SW2: rotar / long-press reinicia
  if (switches & BIT1) {
    unsigned long cnt = LONG_PRESS_CYCLES;
    while ((~P2IN & BIT1) && cnt--) {
      __delay_cycles(1);
    }
    if (cnt == 0) {
      // long-press: reiniciar
      clearScreen(BG_COLOR);
      memset(grid, -1, sizeof grid);
      shapeIndex = shapeRotation = colIndex = 0;
      shapeCol = 0;
      shapeRow = -BLOCK_SIZE*4;
      draw_score_label();
    } else {
      // short-press: rotar
      char newRot = (shapeRotation + 1) % 4;
      int valid = TRUE;
      for (int i = 0; i < 4; i++) {
        int ox = shapes[shapeIndex][i].x;
        int oy = shapes[shapeIndex][i].y;
        int rx, ry;
        switch (newRot) {
          case 1: rx = -oy; ry = ox;   break;
          case 2: rx = -ox; ry = -oy;  break;
          case 3: rx = oy;  ry = -ox;  break;
          default: rx = ox; ry = oy;   break;
        }
        int c = (shapeCol + rx*BLOCK_SIZE)/BLOCK_SIZE;
        int r = (shapeRow + ry*BLOCK_SIZE)/BLOCK_SIZE;
        if (c<0 || c>=numColumns || r>=numRows || (r>=0 && grid[c][r]>=0)) {
          valid = FALSE; break;
        }
      }
      if (valid) shapeRotation = newRot;
    }
  }

  // SW3: reinicio manual
  if (switches & BIT2) {
    clearScreen(BG_COLOR);
    memset(grid, -1, sizeof grid);
    shapeIndex = shapeRotation = colIndex = 0;
    shapeCol = 0;
    shapeRow = -BLOCK_SIZE*4;
    draw_score_label();
  }

  // SW4: mover derecha
  if (switches & BIT3) {
    short newCol = shapeCol + BLOCK_SIZE;
    int valid = TRUE;
    for (int i = 0; i < 4; i++) {
      int ox = shapes[shapeIndex][i].x;
      int oy = shapes[shapeIndex][i].y;
      int rx = (shapeRotation==1? -oy:
               (shapeRotation==2? -ox:
               (shapeRotation==3? oy: ox)));
      int ry = (shapeRotation==1?  ox:
               (shapeRotation==2? -oy:
               (shapeRotation==3? -ox: oy)));
      int c = (newCol + rx*BLOCK_SIZE)/BLOCK_SIZE;
      int r = (shapeRow + ry*BLOCK_SIZE)/BLOCK_SIZE;
      if (c>=numColumns || (r>=0 && grid[c][r]>=0)) {
        valid = FALSE; break;
      }
    }
    if (valid) shapeCol = newCol;
  }

  redrawScreen = TRUE;
  P2IFG = 0;
  P2IE |= SWITCHES;
}

__interrupt_vec(PORT2_VECTOR) void Port_2(void) {
  if (P2IFG & SWITCHES) switch_interrupt_handler();
}

// --------------------------------------------------
// Bucle principal
// --------------------------------------------------
int main(void) {
  P1DIR |= BIT6; P1OUT |= BIT6;      // LED de debugging
  configureClocks();
  lcd_init();
  clearScreen(BG_COLOR);
  draw_score_label();
  switch_init();
  memset(grid, -1, sizeof grid);
  shapeIndex = shapeRotation = colIndex = 0;
  shapeCol = 0;
  shapeRow = -BLOCK_SIZE*4;
  enableWDTInterrupts();
  or_sr(0x8);  // entra en modo LPM0 con interrupciones habilitadas
  while (TRUE) {
    if (redrawScreen) {
      redrawScreen = FALSE;
      update_moving_shape();
    }
    P1OUT &= ~BIT6;
    or_sr(0x10);  // LPM3
    P1OUT |= BIT6;
  }
  return 0;
}
