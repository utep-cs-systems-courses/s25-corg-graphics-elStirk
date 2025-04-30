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
#define SWITCHES     (SW1|SW2)

static int switches = 0;

// ajusta P2IES para detectar flancos de los pulsadores
static char switch_update_interrupt_sense() {
  char p2val = P2IN;
  P2IES |=  (p2val & SWITCHES);    // si entrada alta, detectar bajada
  P2IES &= ~(p2val | ~SWITCHES);   // si entrada baja, detectar subida
  return p2val;
}

void switch_init() {
  P2REN |=  SWITCHES;   // habilita resistencias
  P2OUT |=  SWITCHES;   // pull-ups
  P2DIR &= ~SWITCHES;   // entradas
  P2IE  |=  SWITCHES;   // interrupciones por P2
  switch_update_interrupt_sense();
}

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

// para borrado selectivo de la pieza móvil
static short lastCol = 0, lastRow = 0;
static char  lastIdx = -1;

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
// Solo refresca la pieza móvil (borra la anterior si aplica)
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
// ISR del Watchdog: mueve pieza, checa colisión, stack,
// reinicio de juego y ahora manejo de SW1/SW2
// --------------------------------------------------
void wdt_c_handler() {
  static int tick = 0;
  if (++tick < 64) return;   // ralentiza (~512 Hz / 64)
  tick = 0;

  // --- manejo lateral por botones ---
  if (switches & SW1) {
    shapeCol -= 5;
    if (shapeCol < 0) shapeCol = 0;
    switches &= ~SW1;
  }
  if (switches & SW2) {
    shapeCol += 5;
    {
      int maxC = (numColumns - 1) * BLOCK_SIZE;
      if (shapeCol > maxC) shapeCol = maxC;
    }
    switches &= ~SW2;
  }

  // --- caída vertical ---
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
    // sin colisión → actualiza posición
    shapeRow = newRow;
  } else {
    // colisiona antes de entrar → Game Over y reinicio
    if (shapeRow < 0) {
      clearScreen(BG_COLOR);
      memset(grid, 0, sizeof grid);
      shapeIndex = colIndex = 0;
      shapeCol   = 0;
      shapeRow   = -BLOCK_SIZE * 4;
      redrawScreen = TRUE;
      return;
    }
    // colisiona dentro → fija la pieza actual
    for (int i = 0; i < 4; i++) {
      int x = shapeCol + shapes[shapeIndex][i].x * BLOCK_SIZE;
      int y = shapeRow + shapes[shapeIndex][i].y * BLOCK_SIZE;
      int c = x / BLOCK_SIZE;
      int r = y / BLOCK_SIZE;
      if (r >= 0 && r < numRows) grid[c][r] = 1;
    }
    draw_piece(shapeCol, shapeRow, shapeIndex, shapeColors[shapeIndex]);
    pieceStoppedFlag = TRUE;
    // nueva pieza
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
  P1DIR |= BIT6; P1OUT |= BIT6;    // LED en P1.6 como indicador de CPU
  configureClocks();
  lcd_init();
  switch_init();                   // inicializa SW1/SW2
  clearScreen(BG_COLOR);

  memset(grid, 0, sizeof grid);

  // pieza inicial arriba
  shapeIndex = 0;
  colIndex   = 0;
  shapeCol   = 0;
  shapeRow   = -BLOCK_SIZE * 4;

  enableWDTInterrupts();           // activa ISR del WDT
  or_sr(0x8);                      // GIE

  // bucle principal: solo redibuja la pieza móvil
  while (TRUE) {
    if (redrawScreen) {
      redrawScreen = FALSE;
      update_moving_shape();
    }
    // bajo consumo hasta la próxima interrupción
    P1OUT &= ~BIT6;
    or_sr(0x10);  
    P1OUT |= BIT6;
  }
}

// --------------------------------------------------
// ISR Puerto 2: detecta pulsaciones de SW1/SW2
// --------------------------------------------------
void __interrupt_vec(PORT2_VECTOR) Port_2() {
  if (P2IFG & SWITCHES) {
    P2IFG &= ~SWITCHES;
    switch_interrupt_handler();
  }
}

