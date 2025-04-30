#include <msp430.h>
#include <libTimer.h>
#include "lcdutils.h"
#include "lcddraw.h"

// Tamaño de cada bloque (en píxeles)
#define BLOCK_SIZE 10

// Formas Tetris (4 offsets cada una)
typedef struct { short x, y; } Offset;
const Offset shapes[][4] = {
  {{0,0},{1,0},{0,1},{1,1}},  // Cuadrado
  {{0,0},{1,0},{2,0},{3,0}},  // Línea
  {{0,0},{0,1},{1,1},{2,1}},  // L invertida
  {{1,0},{0,1},{1,1},{2,1}}   // T
};
#define NUM_SHAPES (sizeof(shapes)/sizeof(shapes[0]))

// Columnas según anchura de pantalla
static int numColumns;

// Apilamiento de piezas fijas (aunque sólo guardamos el “ancla” de cada pieza)
typedef struct { short col, row; char idx; } Placed;
#define MAX_PLACED 30
static Placed placed[MAX_PLACED];
static int placedCount = 0;

// Flags y estado global
enum { FALSE = 0, TRUE = 1 };
volatile int redrawScreen     = TRUE;      // para indicar al main loop que dibuje
volatile int pieceStoppedFlag = FALSE;     // para no borrar la pieza fija

static short shapeCol, shapeRow;
static char  shapeIndex = 0, colIndex = 0;

// Última posición/dibujo móvil para borrado selectivo
static short lastCol = 0, lastRow = 0;
static char  lastIdx = -1;

// Colores por forma
unsigned short shapeColors[NUM_SHAPES] = {
  COLOR_RED, COLOR_GREEN, COLOR_ORANGE, COLOR_BLUE
};
#define BG_COLOR COLOR_BLACK

// Dibuja una forma completa en (col,row) con el color dado
static void draw_piece(short col, short row, char idx, unsigned short color) {
  for (int i = 0; i < 4; i++) {
    int x = col + shapes[idx][i].x * BLOCK_SIZE;
    int y = row + shapes[idx][i].y * BLOCK_SIZE;
    fillRectangle(x, y, BLOCK_SIZE, BLOCK_SIZE, color);
  }
}

// Solo borra la pieza anterior si NO acabamos de fijarla
static void update_moving_shape(void) {
  if (lastIdx >= 0 && !pieceStoppedFlag) {
    // Borra la pieza móvil previa
    draw_piece(lastCol, lastRow, lastIdx, BG_COLOR);
  }
  // Dibuja la pieza en su nueva posición
  draw_piece(shapeCol, shapeRow, shapeIndex, shapeColors[shapeIndex]);
  // Guarda para el próximo ciclo
  lastCol = shapeCol;
  lastRow = shapeRow;
  lastIdx = shapeIndex;
  // Resetea la bandera
  pieceStoppedFlag = FALSE;
}

// WDT ISR: controla caída, detección de colisión y apilamiento
void wdt_c_handler() {
  static int tick = 0;
  if (++tick < 64) return;  // controla velocidad (~512Hz/64)
  tick = 0;

  // Mueve la pieza hacia abajo
  shapeRow += BLOCK_SIZE;

  // Comprueba colisión con suelo o con piezas ya fijas
  int collided = FALSE;
  if (shapeRow + BLOCK_SIZE > screenHeight - 1) {
    collided = TRUE;
  } else {
    for (int p = 0; p < placedCount && !collided; p++) {
      if (shapeCol == placed[p].col &&
          shapeRow + BLOCK_SIZE == placed[p].row) {
        collided = TRUE;
      }
    }
  }

  if (collided) {
    // Retrocede la pieza un paso
    shapeRow -= BLOCK_SIZE;
    // Guarda y dibuja la pieza fija
    if (placedCount < MAX_PLACED) {
      placed[placedCount++] = (Placed){ shapeCol, shapeRow, shapeIndex };
      draw_piece(shapeCol, shapeRow, shapeIndex, shapeColors[shapeIndex]);
    }
    // Indica que acabo de fijar para que no se borre
    pieceStoppedFlag = TRUE;

    // Genera nueva pieza en la siguiente columna
    shapeIndex = (shapeIndex + 1) % NUM_SHAPES;
    colIndex   = (colIndex + 1) % numColumns;
    shapeCol   = colIndex * BLOCK_SIZE;
    shapeRow   = -BLOCK_SIZE * 4;
  }

  // Señala al main loop que redibuje
  redrawScreen = TRUE;
}

int main() {
  // Inicialización de hardware
  P1DIR |= BIT6; P1OUT |= BIT6;  // LED en P1.6
  configureClocks();
  lcd_init();
  clearScreen(BG_COLOR);

  // Calcula cuántas columnas caben
  numColumns = screenWidth / BLOCK_SIZE;

  // Estado inicial de la pieza móvil
  shapeIndex = 0;
  colIndex   = 0;
  shapeCol   = 0;
  shapeRow   = -BLOCK_SIZE * 4;

  // Activa WDT e interrupciones
  enableWDTInterrupts();
  or_sr(0x8);

  // Bucle principal: solo redibuja la pieza móvil
  while (TRUE) {
    if (redrawScreen) {
      redrawScreen = FALSE;
      update_moving_shape();
    }
    // Entra en modo de bajo consumo hasta la próxima ISR
    P1OUT &= ~BIT6;
    or_sr(0x10);
    P1OUT |= BIT6;
  }
}


