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

// Apilamiento de piezas fijas
typedef struct { short col, row; char idx; } Placed;
#define MAX_PLACED 30
static Placed placed[MAX_PLACED];
static int placedCount = 0;

// Estado de la pieza en caída
enum { FALSE = 0, TRUE = 1 };
volatile int redrawScreen = TRUE;      // para el main loop
volatile int pieceStoppedFlag = FALSE; // indica que acaba de fijarse una pieza

static short shapeCol, shapeRow;
static char shapeIndex = 0, colIndex = 0;

// Para el borrado selectivo en update_moving_shape
static short lastCol = 0, lastRow = 0;
static char  lastIdx = -1;

// Colores por forma
unsigned short shapeColors[NUM_SHAPES] = {
  COLOR_RED, COLOR_GREEN, COLOR_ORANGE, COLOR_BLUE
};
#define BG_COLOR COLOR_BLACK

// Dibuja una forma completa en (col,row)
static void draw_piece(short col, short row, char idx, unsigned short color) {
  for (int i = 0; i < 4; i++) {
    int x = col + shapes[idx][i].x * BLOCK_SIZE;
    int y = row + shapes[idx][i].y * BLOCK_SIZE;
    fillRectangle(x, y, BLOCK_SIZE, BLOCK_SIZE, color);
  }
}

// Actualiza sólo la pieza móvil, borrando la anterior si no acabo de fijarse
static void update_moving_shape(void) {
  // Si había una pieza anterior y no ha sido fijada justo ahora, la borramos
  if (lastIdx >= 0 && !pieceStoppedFlag) {
    draw_piece(lastCol, lastRow, lastIdx, BG_COLOR);
  }
  // Dibujamos la forma actual
  draw_piece(shapeCol, shapeRow, shapeIndex, shapeColors[shapeIndex]);
  // Guardamos para el próximo ciclo
  lastCol = shapeCol;
  lastRow = shapeRow;
  lastIdx = shapeIndex;
  // Reseteamos la bandera para próximos ciclos
  pieceStoppedFlag = FALSE;
}

// WDT ISR: hace caer la pieza y la apila si colisiona
void wdt_c_handler() {
  static int tick = 0;
  if (++tick < 64) return;  // controla velocidad
  tick = 0;

  // Mover pieza hacia abajo
  shapeRow += BLOCK_SIZE;

  // Detectar colisión con suelo o pieza fija
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
    // Ajustar posición encima de colisión
    shapeRow -= BLOCK_SIZE;
    // Guardar pieza fija y dibujarla
    if (placedCount < MAX_PLACED) {
      placed[placedCount++] = (Placed){ shapeCol, shapeRow, shapeIndex };
      draw_piece(shapeCol, shapeRow, shapeIndex, shapeColors[shapeIndex]);
    }
    // Indicamos que justo fijamos una pieza
    pieceStoppedFlag = TRUE;

    // Generar nueva pieza a la derecha
    shapeIndex = (shapeIndex + 1) % NUM_SHAPES;
    colIndex   = (colIndex + 1) % numColumns;
    shapeCol   = colIndex * BLOCK_SIZE;
    shapeRow   = -BLOCK_SIZE * 4;
  }

  // Señalamos al bucle principal que debe redibujar
  redrawScreen = TRUE;
}

int main() {
  // Inicialización hardware
  P1DIR |= BIT6; P1OUT |= BIT6;  // LED en P1.6
  configureClocks();
  lcd_init();
  clearScreen(BG_COLOR);

  // Columnas disponibles
  numColumns = screenWidth / BLOCK_SIZE;

  // Pieza inicial
  shapeIndex = 0;
  colIndex   = 0;
  shapeCol   = 0;
  shapeRow   = -BLOCK_SIZE * 4;

  // Activar WDT e interrupciones
  enableWDTInterrupts();
  or_sr(0x8);

  // Bucle principal: solo actualiza pieza móvil
  while (TRUE) {
    if (redrawScreen) {
      redrawScreen = FALSE;
      update_moving_shape();
    }
    // CPU OFF entre ISR
    P1OUT &= ~BIT6;
    or_sr(0x10);
    P1OUT |= BIT6;
  }
}

