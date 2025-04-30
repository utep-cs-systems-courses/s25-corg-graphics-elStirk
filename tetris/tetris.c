#include <msp430.h>
#include <libTimer.h>
#include "lcdutils.h"
#include "lcddraw.h"

// Tamaño de cada bloque (en píxeles)
#define BLOCK_SIZE 10

// Definición de formas Tetris (4 bloques cada una)
typedef struct { short x, y; } Offset;
const Offset shapes[][4] = {
  { {0,0}, {1,0}, {0,1}, {1,1} },  // Cuadrado
  { {0,0}, {1,0}, {2,0}, {3,0} },  // Línea
  { {0,0}, {0,1}, {1,1}, {2,1} },  // L invertida
  { {1,0}, {0,1}, {1,1}, {2,1} }   // T
};
#define NUM_SHAPES (sizeof(shapes)/sizeof(shapes[0]))

// Posiciones horizontales predefinidas para que las piezas caigan en distintas columnas
static short colPositions[5];
#define NUM_COLUMNS 5

// Variables de estado compartidas
volatile int redrawScreen = 1;
static short shapeCol, shapeRow;
static char shapeIndex = 0, colIndex = 0;

// Colores para cada forma
unsigned short shapeColors[NUM_SHAPES] = {
  COLOR_RED,
  COLOR_GREEN,
  COLOR_ORANGE,
  COLOR_BLUE
};

#define BG_COLOR COLOR_BLACK

// Borra la forma anterior y dibuja la nueva en (shapeCol, shapeRow)
void update_shape() {
  static short lastCol = -1, lastRow = -1;
  static char  lastShape = -1;

  // Leer variables atómicamente
  and_sr(~8);
  short col = shapeCol;
  short row = shapeRow;
  char  idx = shapeIndex;
  or_sr(8);

  // Si no cambió nada, no repintar
  if (col == lastCol && row == lastRow && idx == lastShape) return;

  // Borrar área de la forma anterior
  if (lastRow >= 0) {
    fillRectangle(lastCol, lastRow,
                  BLOCK_SIZE * 4, BLOCK_SIZE * 4,
                  BG_COLOR);
  }

  // Dibujar nueva forma bloque por bloque
  for (int i = 0; i < 4; i++) {
    int x = col + shapes[idx][i].x * BLOCK_SIZE;
    int y = row + shapes[idx][i].y * BLOCK_SIZE;
    fillRectangle(x, y, BLOCK_SIZE, BLOCK_SIZE,
                  shapeColors[idx]);
  }

  lastCol   = col;
  lastRow   = row;
  lastShape = idx;
}

// Watchdog Timer Handler: mueve la pieza verticalmente (hacia abajo)
void wdt_c_handler() {
  static int tick = 0;
  if (++tick < 64) return;  // control de velocidad (~512 Hz / 64)
  tick = 0;

  // Hacer caer la pieza
  shapeRow += BLOCK_SIZE;

  // Si sobrepasa la pantalla, generar nueva pieza arriba
  if (shapeRow > screenHeight) {
    shapeIndex = (shapeIndex + 1) % NUM_SHAPES;
    colIndex   = (colIndex   + 1) % NUM_COLUMNS;
    shapeCol   = colPositions[colIndex];
    shapeRow   = -BLOCK_SIZE * 4;
  }
  redrawScreen = 1;
}

int main() {
  // Configuración inicial
  P1DIR |= BIT6;  P1OUT |= BIT6;  // LED en P1.6 indica actividad
  configureClocks();
  lcd_init();
  clearScreen(BG_COLOR);

  // Definir columnas de aparición de piezas
  colPositions[0] = 10;
  colPositions[1] = screenWidth / 4;
  colPositions[2] = screenWidth / 2;
  colPositions[3] = 3 * screenWidth / 4;
  colPositions[4] = screenWidth - 10;

  // Estado inicial de la primera pieza
  shapeIndex = 0;
  colIndex   = 0;
  shapeCol   = colPositions[colIndex];
  shapeRow   = -BLOCK_SIZE * 4;

  enableWDTInterrupts();  // activa WDT
  or_sr(0x8);             // interrupciones globales ON

  while (1) {
    if (redrawScreen) {
      redrawScreen = 0;
      update_shape();
    }
    // Sleep corto hasta próxima interrupción
    P1OUT &= ~BIT6;
    or_sr(0x10);
    P1OUT |= BIT6;
  }
}
