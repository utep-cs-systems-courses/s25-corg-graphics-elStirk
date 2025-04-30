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
// Definiciones de switches
// --------------------------------------------------
#define SW1           1
#define SW2           2
#define SW3           4
#define SW4           8
#define SWITCHES     (SW1|SW2|SW3|SW4)

static int switches = 0;

// actualiza P2IES según estado actual de P2IN
static char switch_update_interrupt_sense() {
  char p2val = P2IN;
  P2IES |=  (p2val & SWITCHES);    // si lectura alta, detectar bajada
  P2IES &= ~(p2val | ~SWITCHES);   // si lectura baja, detectar subida
  return p2val;
}

// inicializa resistencias, interrupciones y dirección de P2
void switch_init() {
  P2REN |=  SWITCHES;   // pull-up/down habilitadas
  P2OUT |=  SWITCHES;   // pull-ups
  P2DIR &= ~SWITCHES;   // entrada
  P2IE  |=  SWITCHES;   // interrupciones P2
  switch_update_interrupt_sense();
}

// handler de interrupción que guarda máscaras de botones pulsados
void switch_interrupt_handler() {
  char p2val = switch_update_interrupt_sense();
  switches = ~p2val & SWITCHES;
}

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
// Variables globales de juego
// --------------------------------------------------
static char grid[MAX_COLUMNS][MAX_ROWS];
static const int numColumns = MAX_COLUMNS;
static const int numRows    = MAX_ROWS;

enum { FALSE = 0, TRUE = 1 };
volatile int redrawScreen     = TRUE;
volatile int pieceStoppedFlag = FALSE;

static short shapeCol, shapeRow;
static char  shapeIndex = 0, colIndex = 0;

// para borrado selectivo
static short lastCol = 0, lastRow = 0;
static char  lastIdx = -1;

unsigned short shapeColors[NUM_SHAPES] = {
  COLOR_RED, COLOR_GREEN, COLOR_ORANGE, COLOR_BLUE
};
#define BG_COLOR  COLOR_BLACK

// --------------------------------------------------
// Dibuja una pieza en (col,row) con color
// --------------------------------------------------
static void draw_piece(short col, short row, char idx, unsigned short color) {
  for (int i = 0; i < 4; i++) {
    int x = col + shapes[idx][i].x * BLOCK_SIZE;
    int y = row + shapes[idx][i].y * BLOCK_SIZE;
    fillRectangle(x, y, BLOCK_SIZE, BLOCK_SIZE, color);
  }
}

// --------------------------------------------------
// Refresca sólo la pieza móvil
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
// ISR Watchdog: controla caída, colisiones, stack y Game Over
// --------------------------------------------------
void wdt_c_handler() {
  static int tick = 0;
  if (++tick < 64) return;   // ralentiza
  tick = 0;

  // 1) posible nueva Y
  short newRow = shapeRow + BLOCK_SIZE;

  // 2) comprueba colisión bloque a bloque
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
    // sin choque → cae
    shapeRow = newRow;
  } else {
    // si choca antes de entrar → reinicio total
    if (shapeRow < 0) {
      clearScreen(BG_COLOR);
      memset(grid, 0, sizeof grid);
      shapeIndex = colIndex = 0;
      shapeCol   = 0;
      shapeRow   = -BLOCK_SIZE * 4;
      redrawScreen = TRUE;
      return;
    }
    // choca dentro → fija la pieza actual
    for (int i = 0; i < 4; i++) {
      int x = shapeCol + shapes[shapeIndex][i].x * BLOCK_SIZE;
      int y = shapeRow + shapes[shapeIndex][i].y * BLOCK_SIZE;
      int c = x / BLOCK_SIZE;
      int r = y / BLOCK_SIZE;
      if (r >= 0 && r < numRows) grid[c][r] = 1;
    }
    draw_piece(shapeCol, shapeRow, shapeIndex, shapeColors[shapeIndex]);
    pieceStoppedFlag = TRUE;

    // siguiente pieza
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
  // hardware
  P1DIR |= BIT6; P1OUT |= BIT6;
  configureClocks();
  lcd_init();
  switch_init();            // inicializamos botones
  clearScreen(BG_COLOR);

  memset(grid, 0, sizeof grid);

  shapeIndex = 0;
  colIndex   = 0;
  shapeCol   = 0;
  shapeRow   = -BLOCK_SIZE * 4;

  enableWDTInterrupts();
  or_sr(0x8);               // GIE

  // bucle principal
  while (TRUE) {
    if (redrawScreen) {
      redrawScreen = FALSE;
      update_moving_shape();
    }
    // manejo de botones: SW1 izquierda, SW2 derecha
    if (switches & SW1) {
      shapeCol -= 5;
      if (shapeCol < 0) shapeCol = 0;
      switches &= ~SW1;
      redrawScreen = TRUE;
    }
    if (switches & SW2) {
      shapeCol += 5;
      {
        int maxCol = (numColumns - 1) * BLOCK_SIZE;
        if (shapeCol > maxCol) shapeCol = maxCol;
      }
      switches &= ~SW2;
      redrawScreen = TRUE;
    }
    // bajo consumo hasta próxima ISR
    P1OUT &= ~BIT6;
    or_sr(0x10);
    P1OUT |= BIT6;
  }
}

// --------------------------------------------------
// ISR Puerto 2: detecta pulsaciones
// --------------------------------------------------
void __interrupt_vec(PORT2_VECTOR) Port_2() {
  if (P2IFG & SWITCHES) {
    P2IFG &= ~SWITCHES;
    switch_interrupt_handler();
  }
}


