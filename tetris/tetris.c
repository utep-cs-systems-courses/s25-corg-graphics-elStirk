#include <msp430.h>
#include <libTimer.h>
#include "lcdutils.h"
#include "lcddraw.h"

// Tamaño de cada bloque (en píxeles)
#define BLOCK_SIZE 10

// Definición de formas (4 bloques cada una)
typedef struct { short x, y; } Offset;

const Offset shapes[][4] = {
  // Cuadrado
  { {0,0}, {1,0}, {0,1}, {1,1} },
  // Línea horizontal
  { {0,0}, {1,0}, {2,0}, {3,0} },
  // L invertida
  { {0,0}, {0,1}, {1,1}, {2,1} },
  // T
  { {1,0}, {0,1}, {1,1}, {2,1} }
};
#define NUM_SHAPES (sizeof(shapes)/sizeof(shapes[0]))

// Posiciones verticales predefinidas (reutilizando posiciones del ejemplo)
typedef struct { short col, row; } Pos;
Pos positions[] = {
  {10, 10},
  {10, screenHeight - 10},
  {screenWidth - 10, screenHeight - 10},
  {screenWidth - 10, 10},
  {screenWidth/2, screenHeight/2}
};
#define NUM_POSITIONS (sizeof(positions)/sizeof(positions[0]))

// Variables de estado compartidas
volatile int redrawScreen = 1;
static short shapeCol, shapeRow;
static char shapeIndex = 0, rowIndex = 0;

// Colores para cada forma
unsigned short shapeColors[NUM_SHAPES] = {
  COLOR_RED,
  COLOR_GREEN,
  COLOR_ORANGE,
  COLOR_BLUE
};

#define BG_COLOR COLOR_BLACK

// Actualiza dibujo: borra la forma anterior y dibuja la nueva
void update_shape() {
  static short lastCol = -1, lastRow = -1;
  static char  lastShape = -1;
  // Leer variables de forma atómica
  and_sr(~8);
  short col = shapeCol;
  short row = shapeRow;
  char  idx = shapeIndex;
  or_sr(8);

  // Si no cambió nada, no dibujamos
  if (col == lastCol && row == lastRow && idx == lastShape) return;

  // Borrar forma anterior (área suficientemente grande)
  if (lastCol >= 0) {
    fillRectangle(lastCol,
                  lastRow,
                  BLOCK_SIZE * 4,
                  BLOCK_SIZE * 4,
                  BG_COLOR);
  }

  // Dibujar nueva forma
  for (int i = 0; i < 4; i++) {
    int x = col + shapes[idx][i].x * BLOCK_SIZE;
    int y = row + shapes[idx][i].y * BLOCK_SIZE;
    fillRectangle(x, y, BLOCK_SIZE, BLOCK_SIZE, shapeColors[idx]);
  }

  lastCol   = col;
  lastRow   = row;
  lastShape = idx;
}

// WDT: mueve la pieza horizontalmente de derecha a izquierda
void wdt_c_handler() {
  static int tick = 0;
  if (++tick < 64) return;  // ajuste de velocidad
  tick = 0;

  shapeCol -= BLOCK_SIZE;
  if (shapeCol < -BLOCK_SIZE * 4) {
    // Generar nueva pieza al llegar al borde izquierdo
    shapeIndex = (shapeIndex + 1) % NUM_SHAPES;
    rowIndex   = (rowIndex   + 1) % NUM_POSITIONS;
    shapeCol   = screenWidth;
    shapeRow   = positions[rowIndex].row;
  }
  redrawScreen = 1;
}

int main() {
  // Inicialización básica
  P1DIR |= BIT6;    // LED en P1.6
  P1OUT |= BIT6;    // LED encendido
  configureClocks();
  lcd_init();
  clearScreen(BG_COLOR);

  // Estado inicial de la pieza
  shapeCol = screenWidth;
  shapeRow = positions[0].row;

  enableWDTInterrupts();  // WDT periódico
  or_sr(0x8);             // interrupciones globales ON

  while (1) {
    if (redrawScreen) {
      redrawScreen = 0;
      update_shape();
    }
    // Modo bajo consumo hasta próxima interrupción
    P1OUT &= ~BIT6;
    or_sr(0x10);
    P1OUT |= BIT6;
  }
}
