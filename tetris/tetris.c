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
#define NUM_SHAPES (sizeof(shapes)/sizeof(shapes[0]))

// --------------------------------------------------
// Colores y Background
// --------------------------------------------------
unsigned short shapeColors[NUM_SHAPES] = {COLOR_RED, COLOR_GREEN, COLOR_ORANGE, COLOR_BLUE};
#define BG_COLOR COLOR_BLACK

// --------------------------------------------------
// Estados de la máquina de juego
// --------------------------------------------------
enum GameState { 
  GS_INIT,    // Inicialización / reinicio
  GS_SPAWN,   // Generar nueva pieza
  GS_FALLING, // Pieza cayendo
  GS_LOCK,    // Bloqueo de pieza
  GS_CLEAR,   // Limpieza de filas
  GS_GAMEOVER// Fin de juego
};
static enum GameState gameState;

// --------------------------------------------------
// Variables globales
// --------------------------------------------------
static signed char grid[MAX_COLUMNS][MAX_ROWS];
static int score;
static unsigned long randState;
static int sw2HoldCount;
volatile int redrawScreen;

// Pieza activa
static short shapeCol, shapeRow;
static char shapeIndex, shapeRotation;

// Variables para input
#define SWITCHES 15
volatile int switches;

// Prototipos
static void draw_score_label(void);
static void draw_grid(void);
static void clear_full_rows(void);
static void spawn_new_piece(void);
static void update_moving_shape(void);
static void lock_piece(void);
static void reset_game(void);
static void handle_input(void);
static char switch_update_interrupt_sense(void);

// --------------------------------------------------
// Convierte entero a texto
// --------------------------------------------------
static void itoa_simple(int val, char *buf) {
  int i = 0;
  if (val == 0) buf[i++] = '0';
  else {
    char tmp[6]; int t = 0;
    while (val > 0 && t < 5) { tmp[t++] = '0' + (val % 10); val /= 10; }
    while (t--) buf[i++] = tmp[t];
  }
  buf[i] = '\0';
}

// --------------------------------------------------
// Dibuja texto SCORE
// --------------------------------------------------
static void draw_score_label(void) {
  fillRectangle(0, 0, SCREEN_WIDTH, 8, BG_COLOR);
  char buf[6]; itoa_simple(score, buf);
  drawString5x7(5,5,"SCORE:",COLOR_WHITE,BG_COLOR);
  drawString5x7(40,5,buf,COLOR_WHITE,BG_COLOR);
}

// --------------------------------------------------
// Dibuja todas las piezas fijas
// --------------------------------------------------
static void draw_grid(void) {
  fillScreen(BG_COLOR);
  draw_score_label();
  for (int c = 0; c < MAX_COLUMNS; c++)
    for (int r = 0; r < MAX_ROWS; r++)
      if (grid[c][r] >= 0)
        fillRectangle(c*BLOCK_SIZE, r*BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE,
                      shapeColors[grid[c][r]]);
}

// --------------------------------------------------
// Limpia filas completas
// --------------------------------------------------
static void clear_full_rows(void) {
  for (int r = 0; r < MAX_ROWS; r++) {
    char full = 1;
    for (int c = 0; c < MAX_COLUMNS; c++) if (grid[c][r] < 0) { full = 0; break; }
    if (full) {
      score += 5;
      for (int rr = r; rr > 0; rr--) for (int c = 0; c < MAX_COLUMNS; c++)
        grid[c][rr] = grid[c][rr-1];
      for (int c = 0; c < MAX_COLUMNS; c++) grid[c][0] = -1;
      draw_grid();
      r--; // re-evaluar misma fila
    }
  }
}

// --------------------------------------------------
// Genera nueva pieza
// --------------------------------------------------
static void spawn_new_piece(void) {
  shapeIndex = (randState >> 16) % NUM_SHAPES;
  shapeRotation = 0;
  shapeCol = ((MAX_COLUMNS/2)-1)*BLOCK_SIZE;
  shapeRow = -4*BLOCK_SIZE;
}

// --------------------------------------------------
// Dibuja pieza móvil
// --------------------------------------------------
static void update_moving_shape(void) {
  static int lastCol, lastRow, lastIdx=-1, lastRot;
  // Borra anterior
  if (lastIdx >= 0) {
    for (int i=0;i<4;i++) {
      Offset o = shapes[lastIdx][i];
      int rx = (lastRot==1?-o.y:lastRot==2?-o.x:lastRot==3?o.y:o.x);
      int ry = (lastRot==1?o.x:lastRot==2?-o.y:lastRot==3?-o.x:o.y);
      fillRectangle(lastCol+rx*BLOCK_SIZE, lastRow+ry*BLOCK_SIZE,
                    BLOCK_SIZE, BLOCK_SIZE, BG_COLOR);
    }
  }
  // Dibuja actual
  for (int i=0;i<4;i++) {
    Offset o = shapes[shapeIndex][i];
    int rx = (shapeRotation==1?-o.y:shapeRotation==2?-o.x:shapeRotation==3?o.y:o.x);
    int ry = (shapeRotation==1?o.x:shapeRotation==2?-o.y:shapeRotation==3?-o.x:o.y);
    fillRectangle(shapeCol+rx*BLOCK_SIZE, shapeRow+ry*BLOCK_SIZE,
                  BLOCK_SIZE, BLOCK_SIZE, shapeColors[shapeIndex]);
  }
  lastCol=shapeCol; lastRow=shapeRow; lastIdx=shapeIndex; lastRot=shapeRotation;
}

// --------------------------------------------------
// Bloquea pieza en la rejilla
// --------------------------------------------------
static void lock_piece(void) {
  for (int i=0;i<4;i++) {
    Offset o = shapes[shapeIndex][i];
    int rx = (shapeRotation==1?-o.y:shapeRotation==2?-o.x:shapeRotation==3?o.y:o.x);
    int ry = (shapeRotation==1?o.x:shapeRotation==2?-o.y:shapeRotation==3?-o.x:o.y);
    int c = (shapeCol+rx*BLOCK_SIZE)/BLOCK_SIZE;
    int r = (shapeRow+ry*BLOCK_SIZE)/BLOCK_SIZE;
    if (r>=0 && r<MAX_ROWS) grid[c][r] = shapeIndex;
  }
  draw_grid();
}

// --------------------------------------------------
// Reiniciar juego
// --------------------------------------------------
static void reset_game(void) {
  memset(grid, -1, sizeof grid);
  score = 0;
  randState = TA0R;
  draw_grid();
  gameState = GS_SPAWN;
}

// --------------------------------------------------
// Manejo de entrada por switches
// --------------------------------------------------
void switch_init(void) {
  P2REN |= SWITCHES; P2IE |= SWITCHES; P2OUT |= SWITCHES; P2DIR &= ~SWITCHES;
  switch_update_interrupt_sense();
}

char switch_update_interrupt_sense(void) {
  char p2val = P2IN;
  P2IES |= (p2val & SWITCHES);
  P2IES &= (p2val | ~SWITCHES);
  return p2val;
}

void __interrupt_vec(PORT2_VECTOR) Port_2(void) {
  char p2val = switch_update_interrupt_sense();
  switches = ~p2val & SWITCHES;
  handle_input();
  P2IFG &= ~SWITCHES;
  P2IE |= SWITCHES;
}

// --------------------------------------------------
// Procesa movimiento, rotación y reinicios
// --------------------------------------------------
static void handle_input(void) {
  if (gameState == GS_FALLING) {
    // izquierda
    if (switches & BIT0) {
      short nc = shapeCol - BLOCK_SIZE; int ok=1;
      for (int i=0;i<4;i++) {
        Offset o=shapes[shapeIndex][i];
        int rx=(shapeRotation==1?-o.y:shapeRotation==2?-o.x:shapeRotation==3?o.y:o.x);
        int ry=(shapeRotation==1?o.x:shapeRotation==2?-o.y:shapeRotation==3?-o.x:o.y);
        int c=(nc+rx*BLOCK_SIZE)/BLOCK_SIZE;
        int r=(shapeRow+ry*BLOCK_SIZE)/BLOCK_SIZE;
        if (c<0 || (r>=0 && grid[c][r]>=0)) ok=0;
      }
      if (ok) shapeCol=nc;
    }
    // derecha
    if (switches & BIT3) {
      short nc = shapeCol + BLOCK_SIZE; int ok=1;
      for (int i=0;i<4;i++) {
        Offset o=shapes[shapeIndex][i];
        int rx=(shapeRotation==1?-o.y:shapeRotation==2?-o.x:shapeRotation==3?o.y:o.x);
        int ry=(shapeRotation==1?o.x:shapeRotation==2?-o.y:shapeRotation==3?-o.x:o.y);
        int c=(nc+rx*BLOCK_SIZE)/BLOCK_SIZE;
        int r=(shapeRow+ry*BLOCK_SIZE)/BLOCK_SIZE;
        if (c>=MAX_COLUMNS||(r>=0&&grid[c][r]>=0)) ok=0;
      }
      if (ok) shapeCol=nc;
    }
    // rotar corta
    if ((switches & BIT1) && sw2HoldCount==0) {
      char nr=(shapeRotation+1)%4; int ok=1;
      for (int i=0;i<4;i++) {
        Offset o=shapes[shapeIndex][i];
        int rx=(nr==1?-o.y:nr==2?-o.x:nr==3?o.y:o.x);
        int ry=(nr==1?o.x:nr==2?-o.y:nr==3?-o.x:o.y);
        int c=(shapeCol+rx*BLOCK_SIZE)/BLOCK_SIZE;
        int r=(shapeRow+ry*BLOCK_SIZE)/BLOCK_SIZE;
        if (c<0||c>=MAX_COLUMNS||r>=MAX_ROWS||(r>=0&&grid[c][r]>=0)) ok=0;
      }
      if (ok) shapeRotation=nr;
    }
    // SW3 reinicio manual
    if (switches & BIT2) {
      gameState = GS_INIT;
    }
  }
}

// --------------------------------------------------
// Watchdog: controla caída, colisiones y reinicio largo SW2
// --------------------------------------------------
void wdt_c_handler(void) {
  static int tick=0;
  if (++tick<64) return;
  tick=0;

  // largo SW2
  if (!(P2IN & BIT1)) {
    if (++sw2HoldCount>=3) gameState = GS_INIT;
  } else sw2HoldCount=0;

  if (gameState == GS_INIT) reset_game();
  else if (gameState == GS_SPAWN) spawn_new_piece(), gameState = GS_FALLING;
  else if (gameState == GS_FALLING) {
    short nr = shapeRow + BLOCK_SIZE;
    char col=0;
    for (int i=0;i<4;i++) {
      Offset o=shapes[shapeIndex][i];
      int rx=(shapeRotation==1?-o.y:shapeRotation==2?-o.x:shapeRotation==3?o.y:o.x);
      int ry=(shapeRotation==1?o.x:shapeRotation==2?-o.y:shapeRotation==3?-o.x:o.y);
      int c=(shapeCol+rx*BLOCK_SIZE)/BLOCK_SIZE;
      int r=(nr+ry*BLOCK_SIZE)/BLOCK_SIZE;
      if (r>=MAX_ROWS || (r>=0 && grid[c][r]>=0)) col=1;
    }
    if (!col) shapeRow = nr;
    else {
      if (shapeRow < 0) gameState = GS_GAMEOVER;
      else gameState = GS_LOCK;
    }
  }
  else if (gameState == GS_LOCK) { lock_piece(); gameState = GS_CLEAR; }
  else if (gameState == GS_CLEAR) { clear_full_rows(); gameState = GS_SPAWN; }
  // gameover se queda hasta reinicio

  redrawScreen = 1;
}

// --------------------------------------------------
// main
// --------------------------------------------------
int main(void) {
  configureClocks();
  lcd_init();
  fillScreen(BG_COLOR);

  redrawScreen = 1;
  randState = TA0R;
  gameState = GS_INIT;
  memset(grid, -1, sizeof grid);

  switch_init();
  enableWDTInterrupts();
  or_sr(0x8);

  while(1) {
    if (redrawScreen) {
      redrawScreen = 0;
      update_moving_shape();
    }
    or_sr(0x10);
  }
}
