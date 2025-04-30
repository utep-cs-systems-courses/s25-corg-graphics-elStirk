#include <msp430.h>
#include <libTimer.h>
#include <string.h>
#include "lcdutils.h"
#include "lcddraw.h"

// --------------------------------------------------
// Configuración de pantalla y rejilla
// --------------------------------------------------
#define SCREEN_WIDTH   160
#define SCREEN_HEIGHT  128
#define BLOCK_SIZE     10

// Derivado de arriba: 160/10 = 16 columnas, 128/10 = 12 filas
#define MAX_COLUMNS    (SCREEN_WIDTH  / BLOCK_SIZE)  // 16
#define MAX_ROWS       (SCREEN_HEIGHT / BLOCK_SIZE)  // 12

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
// Rejilla de ocupación: 1 = bloque fijo, 0 = vacío
static char grid[MAX_COLUMNS][MAX_ROWS];
static int  numColumns = MAX_COLUMNS;
static int  numRows    = MAX_ROWS;

// Flags para refresco y para no borrar pieza fija
enum { FALSE = 0, TRUE = 1 };
volatile int redrawScreen     = TRUE;
volatile int pieceStoppedFlag = FALSE;

// Estado de la pieza móvil
static short shapeCol, shapeRow;
static char  shapeIndex = 0, colIndex = 0;

// Para borrado selectivo de la pieza móvil
static short lastCol = 0, lastRow = 0;
static char  lastIdx = -1;

// Colores por forma
unsigned short shapeColors[NUM_SHAPES] = {
  COLOR_RED, COLOR_GREEN, COLOR_ORANGE, COLOR_BLUE
};
#define BG_COLOR  COLOR_BLACK

// --------------------------------------------------
// Funciones de dibujo
// --------------------------------------------------
static void draw_piece(short col, short row, char idx, unsigned short color) {
  for (int i = 0; i < 4; i++) {
    int x = col + shapes[idx][i].x * BLOCK_SIZE;
    int y = row + shapes[idx][i].y * BLOCK_SIZE;
    fillRectangle(x, y, BLOCK_SIZE, BLOCK_SIZE, color);
  }
}

static void update_moving_shape(void) {
  // Borra la pieza previa sólo si no acabó de fijarse
  if (lastIdx >= 0 && !pieceStoppedFlag) {
    draw_piece(lastCol, lastRow, lastIdx, BG_COLOR);
  }
  // Dibuja pieza móvil en su posición actual
  draw_piece(shapeCol, shapeRow, shapeIndex, shapeColors[shapeIndex]);
  // Guarda para el siguiente ciclo
  lastCol = shapeCol;
  lastRow = shapeRow;
  lastIdx = shapeIndex;
  // Resetea la bandera
  pieceStoppedFlag = FALSE;
}

// --------------------------------------------------
// ISR del Watchdog: mueve pieza, comprueba colisión y apila
// --------------------------------------------------
void wdt_c_handler() {
  static int tick = 0;
  if (++tick < 64) return;  // ralentiza (~512Hz/64)
  tick = 0;

  // Adelanta la pieza
  shapeRow += BLOCK_SIZE;

  // Comprueba colisión bloque a bloque
  int collided = FALSE;
  for (int i = 0; i < 4; i++) {
    int x = shapeCol + shapes[shapeIndex][i].x * BLOCK_SIZE;
    int y = shapeRow + BLOCK_SIZE + shapes[shapeIndex][i].y * BLOCK_SIZE;
    int colBlock = x / BLOCK_SIZE;
    int rowBlock = y / BLOCK_SIZE;

    // Si sobrepasa suelo
    if (rowBlock >= numRows) {
      collided = TRUE;
      break;
    }
    // Si cae sobre un bloque previo (y dentro de pantalla)
    if (rowBlock >= 0 && grid[colBlock][rowBlock]) {
      collided = TRUE;
      break;
    }
  }

  if (collided) {
    // Retrocede un paso
    shapeRow -= BLOCK_SIZE;
    // Marca en grid los 4 bloques de la pieza fija
    for (int i = 0; i < 4; i++) {
      int x = shapeCol + shapes[shapeIndex][i].x * BLOCK_SIZE;
      int y = shapeRow + shapes[shapeIndex][i].y * BLOCK_SIZE;
      int colBlock = x / BLOCK_SIZE;
      int rowBlock = y / BLOCK_SIZE;
      if (rowBlock >= 0 && rowBlock < numRows) {
        grid[colBlock][rowBlock] = 1;
      }
    }
    // Dibuja la pieza ya fija
    draw_piece(shapeCol, shapeRow, shapeIndex, shapeColors[shapeIndex]);
    // Evita que update_moving_shape la borre
    pieceStoppedFlag = TRUE;

    // Prepara la siguiente pieza
    shapeIndex = (shapeIndex + 1) % NUM_SHAPES;
    colIndex   = (colIndex   + 1) % numColumns;
    shapeCol   = colIndex * BLOCK_SIZE;
    shapeRow   = -BLOCK_SIZE * 4;
  }

  // Señal para el bucle principal
  redrawScreen = TRUE;
}

// --------------------------------------------------
// main()
// --------------------------------------------------
int main() {
  // Hardware
  P1DIR |= BIT6; P1OUT |= BIT6;   // LED en P1.6
  configureClocks();
  lcd_init();
  clearScreen(BG_COLOR);

  // Inicializa rejilla a 0
  memset(grid, 0, sizeof grid);

  // Pieza inicial
  shapeIndex = 0;
  colIndex   = 0;
  shapeCol   = 0;
  shapeRow   = -BLOCK_SIZE * 4;

  // Activa WDT e interrupciones
  enableWDTInterrupts();
  or_sr(0x8);

  // Bucle principal
  while (TRUE) {
    if (redrawScreen) {
      redrawScreen = FALSE;
      update_moving_shape();
    }
    // Bajo consumo hasta la próxima ISR
    P1OUT &= ~BIT6;
    or_sr(0x10);
    P1OUT |= BIT6;
  }
}
