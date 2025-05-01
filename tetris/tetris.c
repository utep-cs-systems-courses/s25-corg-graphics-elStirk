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
static signed char grid[MAX_COLUMNS][MAX_ROWS];
static const int numColumns = MAX_COLUMNS;
static const int numRows    = MAX_ROWS;

enum { FALSE = 0, TRUE = 1 };
volatile int redrawScreen     = TRUE;
volatile int pieceStoppedFlag = FALSE;

static short shapeCol, shapeRow;
static char  shapeIndex    = 0;
static char  shapeRotation = 0;

static short lastCol = 0, lastRow = 0;
static char  lastIdx  = -1;
static char  lastRot  = 0;

static int score = 0;

unsigned short shapeColors[NUM_SHAPES] = {
  COLOR_RED, COLOR_GREEN, COLOR_ORANGE, COLOR_BLUE
};
#define BG_COLOR      COLOR_BLACK

// --------------------------------------------------
// Generador de pseudoaleatorios LCG
// --------------------------------------------------
static unsigned long randState;

// --------------------------------------------------
// Contador para pulsación larga en SW2 (~3s)
// --------------------------------------------------
static int sw2HoldCount = 0;

// --------------------------------------------------
// Prototipos
// --------------------------------------------------
static void draw_piece(short col, short row, char idx, char rot, unsigned short color);
static void draw_score_label(void);
static void itoa_simple(int val, char *buf);
static int rotatedX(char idx, char rot, int i);
static int rotatedY(char idx, char rot, int i);
static void clear_full_rows(void);

// --------------------------------------------------
// Convierte entero a texto simple (base 10)
// --------------------------------------------------
static void itoa_simple(int val, char *buf) {
  int i = 0;
  if (val == 0) {
    buf[i++] = '0';
  } else {
    char tmp[6]; int t = 0;
    while (val > 0 && t < 5) {
      tmp[t++] = '0' + (val % 10);
      val /= 10;
    }
    while (t--) buf[i++] = tmp[t];
  }
  buf[i] = '\0';
}

// --------------------------------------------------
// Dibuja el texto "SCORE:" y el valor
// --------------------------------------------------
static void draw_score_label(void) {
  // Solo redibuja la banda superior
  fillRectangle(0, 0, SCREEN_WIDTH, 8, BG_COLOR);
  char buf[6];
  itoa_simple(score, buf);
  drawString5x7(5, 5, "SCORE:", COLOR_WHITE, BG_COLOR);
  drawString5x7(35, 5, buf, COLOR_WHITE, BG_COLOR);
}

// --------------------------------------------------
// Dibuja/borra una pieza con rotación
// --------------------------------------------------
static void draw_piece(short col, short row, char idx, char rot, unsigned short color) {
  for (int i = 0; i < 4; i++) {
    int rx = rotatedX(idx, rot, i);
    int ry = rotatedY(idx, rot, i);
    fillRectangle(col + rx*BLOCK_SIZE,
                  row + ry*BLOCK_SIZE,
                  BLOCK_SIZE, BLOCK_SIZE,
                  color);
  }
}

// --------------------------------------------------
// Helpers para rotación sin duplicar
// --------------------------------------------------
static int rotatedX(char idx, char rot, int i) {
  int ox = shapes[idx][i].x, oy = shapes[idx][i].y;
  switch(rot) {
    case 1: return -oy;
    case 2: return -ox;
    case 3: return oy;
    default: return ox;
  }
}
static int rotatedY(char idx, char rot, int i) {
  int ox = shapes[idx][i].x, oy = shapes[idx][i].y;
  switch(rot) {
    case 1: return ox;
    case 2: return -oy;
    case 3: return -ox;
    default: return oy;
  }
}

// --------------------------------------------------
// Elimina filas completas (solo redibuja celdas afectadas)
// --------------------------------------------------
static void clear_full_rows(void) {
  for (int r = 0; r < numRows; r++) {
    int full = TRUE;
    for (int c = 0; c < numColumns; c++) {
      if (grid[c][r] < 0) { full = FALSE; break; }
    }
    if (!full) continue;

    // Aumenta puntaje y redibuja score
    score += 5;
    draw_score_label();

    // Borra visualmente la fila r
    for (int c = 0; c < numColumns; c++) {
      fillRectangle(c*BLOCK_SIZE, r*BLOCK_SIZE,
                    BLOCK_SIZE, BLOCK_SIZE,
                    BG_COLOR);
    }

    // Baja filas superiores
    for (int rr = r; rr > 0; rr--) {
      for (int c = 0; c < numColumns; c++) {
        grid[c][rr] = grid[c][rr-1];
        if (grid[c][rr] >= 0) {
          fillRectangle(c*BLOCK_SIZE, rr*BLOCK_SIZE,
                        BLOCK_SIZE, BLOCK_SIZE,
                        shapeColors[ grid[c][rr] ]);
        }
      }
    }
    // Limpia la fila 0
    for (int c = 0; c < numColumns; c++) {
      grid[c][0] = -1;
    }
    // Revisa de nuevo la misma r (por si hay múltiples líneas)
    r--;
  }
}

// --------------------------------------------------
// Manejo de botones (sin cambios relevantes aquí)
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
  if (lastIdx >= 0)
    draw_piece(lastCol, lastRow, lastIdx, lastRot, BG_COLOR);

  if (switches & (1<<0)) { /* SW1: izquierda */ /* ... */ }
  if ((switches & (1<<1)) && sw2HoldCount == 0) { /* SW2: rotar */ /* ... */ }
  if (switches & (1<<2)) { /* SW3: reiniciar manual */
    clearScreen(BG_COLOR);
    memset(grid, -1, sizeof grid);
    score = 0;
    randState = TA0R;
    shapeRotation = 0;
    shapeCol = ((numColumns / 2) - 1) * BLOCK_SIZE;
    shapeRow = -BLOCK_SIZE * 4;
    draw_score_label();
    sw2HoldCount = 0;
  }
  if (switches & (1<<3)) { /* SW4: derecha */ /* ... */ }

  redrawScreen = TRUE;
  P2IFG = 0;
  P2IE |= SWITCHES;
}
void __interrupt_vec(PORT2_VECTOR) Port_2(void) {
  if (P2IFG & SWITCHES)
    switch_interrupt_handler();
}

// --------------------------------------------------
// WDT: caída y larga pulsación en SW2
// --------------------------------------------------
void wdt_c_handler(void) {
  static int tick = 0;
  if (++tick < 64) return;
  tick = 0;

  // Larga pulsación SW2 (3s) → reinicio
  if (!(P2IN & (1<<1))) {
    sw2HoldCount++;
    if (sw2HoldCount >= 3) {
      clearScreen(BG_COLOR);
      memset(grid, -1, sizeof grid);
      score = 0;
      randState = randState * 1103515245 + 12345;
      shapeRotation = 0;
      shapeCol = ((numColumns / 2) - 1) * BLOCK_SIZE;
      shapeRow = -BLOCK_SIZE * 4;
      draw_score_label();
      sw2HoldCount = 0;
      return;
    }
  } else {
    sw2HoldCount = 0;
  }

  // Intento de caída
  short newRow = shapeRow + BLOCK_SIZE;
  int collided = FALSE;
  for (int i = 0; i < 4; i++) {
    int c = (shapeCol + rotatedX(shapeIndex, shapeRotation, i)*BLOCK_SIZE)/BLOCK_SIZE;
    int r = (newRow  + rotatedY(shapeIndex, shapeRotation, i)*BLOCK_SIZE)/BLOCK_SIZE;
    if (r >= numRows || (r >= 0 && grid[c][r] >= 0)) { collided = TRUE; break; }
  }

  if (!collided) {
    // 1) Borra la pieza antigua
    draw_piece(lastCol, lastRow, lastIdx, lastRot, BG_COLOR);
    // 2) Actualiza posición
    shapeRow = newRow;
    // 3) Dibuja la pieza en nueva posición
    draw_piece(shapeCol, shapeRow, shapeIndex, shapeRotation,
               shapeColors[shapeIndex]);
    // 4) Guarda para siguiente iteración
    lastCol = shapeCol; lastRow = shapeRow;
    lastIdx = shapeIndex; lastRot = shapeRotation;
    redrawScreen = FALSE;
    return;
  }

  // Si colisiona antes de spawn → game over
  if (shapeRow < 0) {
    clearScreen(BG_COLOR);
    memset(grid, -1, sizeof grid);
    score = 0;
    randState = TA0R;
    shapeRotation = 0;
    shapeCol = ((numColumns / 2) - 1) * BLOCK_SIZE;
    shapeRow = -BLOCK_SIZE * 4;
    draw_score_label();
    return;
  }

  // Fija la pieza en la grid
  for (int i = 0; i < 4; i++) {
    int c = (shapeCol + rotatedX(shapeIndex, shapeRotation, i)*BLOCK_SIZE)/BLOCK_SIZE;
    int r = (shapeRow + rotatedY(shapeIndex, shapeRotation, i)*BLOCK_SIZE)/BLOCK_SIZE;
    if (r >= 0 && r < numRows)
      grid[c][r] = shapeIndex;
  }

  // Limpia filas completas y genera nueva pieza
  clear_full_rows();
  pieceStoppedFlag = TRUE;
  lastIdx = -1;

  randState = randState * 1103515245 + 12345;
  shapeIndex = (randState >> 16) % NUM_SHAPES;
  shapeRotation = 0;
  shapeCol = ((numColumns / 2) - 1) * BLOCK_SIZE;
  shapeRow = -BLOCK_SIZE * 4;
  redrawScreen = TRUE;
}

// --------------------------------------------------
// main
// --------------------------------------------------
int main(void) {
  P1DIR |= BIT6;
  P1OUT |= BIT6;
  configureClocks();
  lcd_init();
  clearScreen(BG_COLOR);

  score = 0;
  draw_score_label();

  randState = TA0R;
  shapeIndex = (randState >> 16) % NUM_SHAPES;

  switch_init();
  memset(grid, -1, sizeof grid);
  shapeRotation = 0;
  shapeCol = ((numColumns / 2) - 1) * BLOCK_SIZE;
  shapeRow = -BLOCK_SIZE * 4;

  enableWDTInterrupts();
  or_sr(0x8);
  while (TRUE) {
    // Con el nuevo enfoque, ya no es necesario redibujar todo el tiempo
    if (redrawScreen) {
      redrawScreen = FALSE;
      // No hace nada aquí: la pieza ya se dibuja en wdt_c_handler
    }
    P1OUT &= ~BIT6;
    or_sr(0x10);
    P1OUT |= BIT6;
  }
}
