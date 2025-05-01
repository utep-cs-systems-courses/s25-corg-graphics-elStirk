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
#define BLOCK_SIZE     8

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
// Bolsa “8-bag” (2 copias de cada) para spawn sin patrones
// --------------------------------------------------
#define BAG_MULT   2
#define BAG_SIZE   (NUM_SHAPES * BAG_MULT)
static unsigned char bag[BAG_SIZE];
static int bagPos = BAG_SIZE;  // fuerza refill inicial

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

static short lastCol = 0, lastRow = 0;
static char  lastIdx  = -1;
static char  lastRot  = 0;

static int score = 0;             // puntaje actual

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
static void draw_grid(void);
static void clear_full_rows(void);
static void draw_score_label(void);
static void itoa_simple(int val, char *buf);
static int rotatedX(char idx, char rot, int i);
static int rotatedY(char idx, char rot, int i);
static void refillBag(void);
static void update_moving_shape(void);
static char switch_update_interrupt_sense(void);
static void switch_init(void);
static void switch_interrupt_handler(void);

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
  fillRectangle(0, 0, SCREEN_WIDTH, 8, BG_COLOR);
  char buf[6];
  itoa_simple(score, buf);
  drawString5x7(5, 5, "SCORE:", COLOR_WHITE, BG_COLOR);
  drawString5x7(35, 5, buf, COLOR_WHITE, BG_COLOR);
}

// --------------------------------------------------
// Helpers de rotación
// --------------------------------------------------
static int rotatedX(char idx, char rot, int i) {
  int ox = shapes[idx][i].x;
  int oy = shapes[idx][i].y;
  switch(rot) {
    case 1: return -oy;
    case 2: return -ox;
    case 3: return oy;
    default: return ox;
  }
}
static int rotatedY(char idx, char rot, int i) {
  int ox = shapes[idx][i].x;
  int oy = shapes[idx][i].y;
  switch(rot) {
    case 1: return ox;
    case 2: return -oy;
    case 3: return -ox;
    default: return oy;
  }
}

// --------------------------------------------------
// Dibuja una pieza con rotación
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
// Dibuja todas las piezas estáticas
// --------------------------------------------------
static void draw_grid(void) {
  for (int c = 0; c < numColumns; c++) {
    for (int r = 0; r < numRows; r++) {
      signed char idx = grid[c][r];
      if (idx >= 0) {
        fillRectangle(c*BLOCK_SIZE,
                      r*BLOCK_SIZE,
                      BLOCK_SIZE, BLOCK_SIZE,
                      shapeColors[idx]);
      }
    }
  }
}

// --------------------------------------------------
// Elimina filas completas
// --------------------------------------------------
static void clear_full_rows(void) {
  for (int r = 0; r < numRows; r++) {
    int full = TRUE;
    for (int c = 0; c < numColumns; c++) {
      if (grid[c][r] < 0) { full = FALSE; break; }
    }
    if (full) {
      score += 5;
      for (int rr = r; rr > 0; rr--)
        for (int c = 0; c < numColumns; c++)
          grid[c][rr] = grid[c][rr-1];
      for (int c = 0; c < numColumns; c++)
        grid[c][0] = -1;
      clearScreen(BG_COLOR);
      draw_grid();
      draw_score_label();
      r--;
    }
  }
}

// --------------------------------------------------
// Rellena y baraja la bolsa (Fisher–Yates sobre 8)
// --------------------------------------------------
static void refillBag(void) {
  int idx = 0;
  for (int m = 0; m < BAG_MULT; m++) {
    for (int i = 0; i < NUM_SHAPES; i++) {
      bag[idx++] = i;
    }
  }
  for (int i = BAG_SIZE - 1; i > 0; i--) {
    randState = randState * 1103515245 + 12345;
    unsigned int j = (randState >> 16) % (i + 1);
    unsigned char tmp = bag[i];
    bag[i] = bag[j];
    bag[j] = tmp;
  }
  bagPos = 0;
}

// --------------------------------------------------
// Actualiza la pieza móvil (borrado + repintado estáticos)
// --------------------------------------------------
static void update_moving_shape(void) {
  draw_score_label();
  if (lastIdx >= 0) {
    // Borrar la figura anterior y restaurar fondo o bloques estáticos
    for (int i = 0; i < 4; i++) {
      int c = (lastCol + rotatedX(lastIdx, lastRot, i)*BLOCK_SIZE)/BLOCK_SIZE;
      int r = (lastRow + rotatedY(lastIdx, lastRot, i)*BLOCK_SIZE)/BLOCK_SIZE;

      if (c >= 0 && c < numColumns && r >= 0 && r < numRows) {
        signed char idx = grid[c][r];
        unsigned short color = (idx >= 0 && idx < NUM_SHAPES) ? shapeColors[idx] : BG_COLOR;
        fillRectangle(c * BLOCK_SIZE, r * BLOCK_SIZE,
                      BLOCK_SIZE, BLOCK_SIZE,
                      color);
      }
    }
  }

  // Dibujar la figura en su nueva posición
  draw_piece(shapeCol, shapeRow, shapeIndex, shapeRotation,
             shapeColors[shapeIndex]);

  // Guardar nueva posición como "última"
  lastCol = shapeCol;
  lastRow = shapeRow;
  lastIdx = shapeIndex;
  lastRot = shapeRotation;
}

// --------------------------------------------------
// Switches con debounce e interrupciones
// --------------------------------------------------
#define SWITCHES (BIT0 | BIT1 | BIT2 | BIT3)
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

  // Borra figura actual para evitar rastro antes de moverla
  if (lastIdx >= 0) {
    for (int i = 0; i < 4; i++) {
      int c = (lastCol + rotatedX(lastIdx, lastRot, i)*BLOCK_SIZE)/BLOCK_SIZE;
      int r = (lastRow + rotatedY(lastIdx, lastRot, i)*BLOCK_SIZE)/BLOCK_SIZE;

      if (c >= 0 && c < numColumns && r >= 0 && r < numRows) {
        signed char idx = grid[c][r];
        unsigned short color = (idx >= 0 && idx < NUM_SHAPES) ? shapeColors[idx] : BG_COLOR;
        fillRectangle(c * BLOCK_SIZE, r * BLOCK_SIZE,
                      BLOCK_SIZE, BLOCK_SIZE,
                      color);
      }
    }
  }

  // SW1 izq
  if (switches & BIT0) {
    short newCol = shapeCol - BLOCK_SIZE;
    int valid = TRUE;
    for (int i = 0; i < 4; i++) {
      int c = (newCol + rotatedX(shapeIndex, shapeRotation, i)*BLOCK_SIZE)/BLOCK_SIZE;
      int r = (shapeRow + rotatedY(shapeIndex, shapeRotation, i)*BLOCK_SIZE)/BLOCK_SIZE;
      if (c<0 || (r>=0 && grid[c][r]>=0)) { valid=FALSE; break; }
    }
    if (valid) shapeCol = newCol;
  }
  // SW2 rotar
  if ((switches & BIT1) && sw2HoldCount == 0) {
    char newRot = (shapeRotation + 1) % 4;
    int valid = TRUE;
    for (int i = 0; i < 4; i++) {
      int c = (shapeCol + rotatedX(shapeIndex, newRot, i)*BLOCK_SIZE)/BLOCK_SIZE;
      int r = (shapeRow + rotatedY(shapeIndex, newRot, i)*BLOCK_SIZE)/BLOCK_SIZE;
      if (c<0||c>=numColumns||r>=numRows||(r>=0&&grid[c][r]>=0)) { valid=FALSE; break; }
    }
    if (valid) shapeRotation = newRot;
  }
  // SW3 reiniciar
  if (switches & BIT2) {
    clearScreen(BG_COLOR);
    memset(grid, -1, sizeof grid);
    score = 0;
    randState = TA0R;
    shapeRotation = 0;
    shapeCol = ((numColumns/2)-1)*BLOCK_SIZE;
    shapeRow = -BLOCK_SIZE*4;
    draw_score_label();
    sw2HoldCount = 0;
  }
  // SW4 der
  if (switches & BIT3) {
    short newCol = shapeCol + BLOCK_SIZE;
    int valid = TRUE;
    for (int i = 0; i < 4; i++) {
      int c = (newCol + rotatedX(shapeIndex, shapeRotation, i)*BLOCK_SIZE)/BLOCK_SIZE;
      int r = (shapeRow + rotatedY(shapeIndex, shapeRotation, i)*BLOCK_SIZE)/BLOCK_SIZE;
      if (c>=numColumns || (r>=0 && grid[c][r]>=0)) { valid=FALSE; break; }
    }
    if (valid) shapeCol = newCol;
  }

  redrawScreen = TRUE;
  P2IFG = 0;
  P2IE |= SWITCHES;
}

void __interrupt_vec(PORT2_VECTOR) Port_2(void) {
  if (P2IFG & SWITCHES) switch_interrupt_handler();
}

// --------------------------------------------------
// WDT: caída, colisiones, bolsa y pulsación larga SW2
// --------------------------------------------------
void wdt_c_handler(void) {
  static short time = 64;
  if(score >= 10 && score < 20){
    time = 32;
  }else if(score >= 20 && score < 40){
    time = 24;
  }else{
    time = 64;
  }
  static int tick = 0;
  if (++tick < time) return;
  tick = 0;

  if (!(P2IN & BIT1)) {
    sw2HoldCount++;
    if (sw2HoldCount >= 3) {
      clearScreen(BG_COLOR);
      memset(grid, -1, sizeof grid);
      score = 0;
      randState = randState * 1103515245 + 12345;
      shapeRotation = 0;
      shapeCol = ((numColumns/2)-1)*BLOCK_SIZE;
      shapeRow = -BLOCK_SIZE*4;
      draw_score_label();
      sw2HoldCount = 0;
      return;
    }
  } else {
    sw2HoldCount = 0;
  }

  short newRow = shapeRow + BLOCK_SIZE;
  int collided = FALSE;
  for (int i = 0; i < 4; i++) {
    int c = (shapeCol + rotatedX(shapeIndex, shapeRotation, i)*BLOCK_SIZE)/BLOCK_SIZE;
    int r = (newRow + rotatedY(shapeIndex, shapeRotation, i)*BLOCK_SIZE)/BLOCK_SIZE;
    if (r>=numRows || (r>=0 && grid[c][r]>=0)) { collided = TRUE; break; }
  }
  if (!collided) {
    shapeRow = newRow;
  } else {
    if (shapeRow < 0) {
      clearScreen(BG_COLOR);
      memset(grid, -1, sizeof grid);
      score = 0;
      randState = TA0R;
      shapeRotation = 0;
      shapeCol = ((numColumns/2)-1)*BLOCK_SIZE;
      shapeRow = -BLOCK_SIZE*4;
      draw_score_label();
      return;
    }
    for (int i = 0; i < 4; i++) {
      int c = (shapeCol + rotatedX(shapeIndex, shapeRotation, i)*BLOCK_SIZE)/BLOCK_SIZE;
      int r = (shapeRow + rotatedY(shapeIndex, shapeRotation, i)*BLOCK_SIZE)/BLOCK_SIZE;
      if (r>=0 && r<numRows) grid[c][r] = shapeIndex;
    }
    draw_grid();
    clear_full_rows();
    pieceStoppedFlag = TRUE;
    lastIdx = -1;

    if (bagPos >= BAG_SIZE) refillBag();
    shapeIndex = bag[bagPos++];
    shapeRotation = 0;
    shapeCol = ((numColumns/2)-1)*BLOCK_SIZE;
    shapeRow = -BLOCK_SIZE*4;
  }
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
  refillBag();
  shapeIndex = bag[bagPos++];
  shapeRotation = 0;
  shapeCol = ((numColumns/2)-1)*BLOCK_SIZE;
  shapeRow = -BLOCK_SIZE*4;

  switch_init();
  memset(grid, -1, sizeof grid);

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
