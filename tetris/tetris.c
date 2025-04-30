#include <msp430.h>
#include <libTimer.h>
#include "lcdutils.h"
#include "lcddraw.h"

// Tamaño de cada bloque (en píxeles)
#define BLOCK_SIZE 10

// Definición de formas Tetris (4 bloques cada una)
typedef struct { short x, y; } Offset;
const Offset shapes[][4] = {
  {{0,0},{1,0},{0,1},{1,1}},  // Cuadrado
  {{0,0},{1,0},{2,0},{3,0}},  // Línea
  {{0,0},{0,1},{1,1},{2,1}},  // L invertida
  {{1,0},{0,1},{1,1},{2,1}}   // T
};
#define NUM_SHAPES (sizeof(shapes)/sizeof(shapes[0]))

// Grilla: columnas según anchura de pantalla
static int numColumns;

// Piezas colocadas (stacking)
#define MAX_PLACED 48  // (160/10)*(128/10)/4 = 16*12/4

typedef struct { short col, row; char idx; } Placed;
static Placed placed[MAX_PLACED];
static int placedCount = 0;

// Estado pieza en caída
enum { FALSE=0, TRUE=1 };
volatile int redrawScreen = TRUE;
static short shapeCol, shapeRow;
static char shapeIndex, colIndex;

// Colores para cada forma
unsigned short shapeColors[NUM_SHAPES] = { COLOR_RED, COLOR_GREEN,
                                           COLOR_ORANGE, COLOR_BLUE };
#define BG_COLOR COLOR_BLACK

// Dibuja una pieza dada (placed o en caída)
static void draw_piece(short col, short row, char idx, unsigned short color) {
  for (int i = 0; i < 4; i++) {
    int x = col + shapes[idx][i].x * BLOCK_SIZE;
    int y = row + shapes[idx][i].y * BLOCK_SIZE;
    fillRectangle(x, y, BLOCK_SIZE, BLOCK_SIZE, color);
  }
}

// Dibuja solo la pieza en movimiento, borrando la anterior
static void update_moving_shape() {
  static short lastCol = 0, lastRow = 0;
  static char  lastIdx = -1;
  // Si hay forma previa, borrarla
  if (lastIdx >= 0) {
    draw_piece(lastCol, lastRow, lastIdx, BG_COLOR);
  }
  // Dibujar pieza actual
  draw_piece(shapeCol, shapeRow, shapeIndex,
             shapeColors[shapeIndex]);
  // Guardar para siguiente iteración
  lastCol = shapeCol;  lastRow = shapeRow;
  lastIdx = shapeIndex;
}

// Watchdog Timer Handler: mueve la pieza hacia abajo y detecta colisión
void wdt_c_handler() {
  static int tick = 0;
  if (++tick < 64) return;  // control de velocidad (~512Hz/64)
  tick = 0;

  // Avanzar en Y
  shapeRow += BLOCK_SIZE;

  // Comprobar colisión: suelo o tope de pieza colocada
  int collided = FALSE;
  if (shapeRow + BLOCK_SIZE > screenHeight - 1) {
    collided = TRUE;
  } else {
    for (int p = 0; p < placedCount; p++) {
      for (int i = 0; i < 4; i++) {
        int x = shapeCol + shapes[shapeIndex][i].x * BLOCK_SIZE;
        int y = shapeRow + shapes[shapeIndex][i].y * BLOCK_SIZE;
        if (y + BLOCK_SIZE > placed[p].row &&
            x == placed[p].col + shapes[placed[p].idx][i].x * BLOCK_SIZE) {
          collided = TRUE;
          break;
        }
      }
      if (collided) break;
    }
  }

  if (collided) {
    // Ajustar posición arriba de colisión
    shapeRow -= BLOCK_SIZE;
    // Guardar pieza en colocadas y dibujarla fija
    if (placedCount < MAX_PLACED) {
      placed[placedCount] = (Placed){shapeCol, shapeRow, shapeIndex};
      draw_piece(shapeCol, shapeRow, shapeIndex,
                 shapeColors[shapeIndex]);
      placedCount++;
    }
    // Nueva pieza en siguiente columna
    shapeIndex = (shapeIndex + 1) % NUM_SHAPES;
    colIndex   = (colIndex   + 1) % numColumns;
    shapeCol   = colIndex * BLOCK_SIZE;
    shapeRow   = -BLOCK_SIZE * 4;
  }
  redrawScreen = TRUE;
}

int main() {
  // Inicialización básica
  P1DIR |= BIT6;  P1OUT |= BIT6;       // LED en P1.6 ON
  configureClocks();
  lcd_init();
  clearScreen(BG_COLOR);

  // Columnas según pantalla rotada
  numColumns = screenWidth / BLOCK_SIZE;

  // Pieza inicial
  shapeIndex = 0;  colIndex = 0;
  shapeCol   = 0;
  shapeRow   = -BLOCK_SIZE * 4;

  enableWDTInterrupts();              // activar WDT
  or_sr(0x8);                         // interrupciones globales ON

  while (TRUE) {
    if (redrawScreen) {
      redrawScreen = FALSE;
      update_moving_shape();
    }
    // CPU OFF hasta próxima ISR, LED parpadea
    P1OUT &= ~BIT6;
    or_sr(0x10);
    P1OUT |= BIT6;
  }
}

