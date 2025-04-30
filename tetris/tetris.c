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

// Columnas posibles de aparición
#define NUM_COLUMNS 5
static short colPositions[NUM_COLUMNS];

// Registro de piezas colocadas para stacking
// Cálculo de capacidad de stacking:
// Pantalla rotada: altura vertical = 128px, BLOCK_SIZE = 10px → 128/10 = 12.8 filas → 12 filas
// Altura máxima de pieza = 2 bloques → 12/2 = 6 piezas por columna
// NUM_COLUMNS = 5 → 6*5 = 30 piezas máximo en pantalla
#define MAX_PLACED 30

typedef struct { short col, row; char idx; } Placed;
static Placed placed[MAX_PLACED];
static int placedCount = 0;

// Estado de la pieza en caída
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

// Dibuja todo: piezas apiladas y pieza en movimiento
void draw_all() {
  clearScreen(BG_COLOR);
  // Dibujar piezas colocadas
  for (int p = 0; p < placedCount; p++) {
    Placed *pc = &placed[p];
    for (int i = 0; i < 4; i++) {
      int x = pc->col + shapes[pc->idx][i].x * BLOCK_SIZE;
      int y = pc->row + shapes[pc->idx][i].y * BLOCK_SIZE;
      fillRectangle(x, y, BLOCK_SIZE, BLOCK_SIZE,
                    shapeColors[pc->idx]);
    }
  }
  // Dibujar pieza en caída
  for (int i = 0; i < 4; i++) {
    int x = shapeCol + shapes[shapeIndex][i].x * BLOCK_SIZE;
    int y = shapeRow + shapes[shapeIndex][i].y * BLOCK_SIZE;
    fillRectangle(x, y, BLOCK_SIZE, BLOCK_SIZE,
                  shapeColors[shapeIndex]);
  }
}

// Watchdog Timer Handler: mueve la pieza hacia abajo y detecta colisión
void wdt_c_handler() {
  static int tick = 0;
  if (++tick < 64) return;
  tick = 0;

  // Mover pieza
  shapeRow += BLOCK_SIZE;

  // Detectar colisión: suelo o tope de otra pieza
  int collided = 0;
  if (shapeRow + BLOCK_SIZE >= screenHeight) {
    collided = 1;
  } else {
    // Verificar contra piezas colocadas
    for (int p = 0; p < placedCount; p++) {
      for (int i = 0; i < 4; i++) {
        int x = shapeCol + shapes[shapeIndex][i].x * BLOCK_SIZE;
        int y = shapeRow + shapes[shapeIndex][i].y * BLOCK_SIZE;
        if (y + BLOCK_SIZE > placed[p].row &&
            x == placed[p].col + shapes[placed[p].idx][i].x * BLOCK_SIZE) {
          collided = 1;
          break;
        }
      }
      if (collided) break;
    }
  }

  if (collided) {
    // Ajustar posición final justo arriba
    shapeRow -= BLOCK_SIZE;
    // Guardar pieza
    if (placedCount < MAX_PLACED) {
      placed[placedCount++] = (Placed){ shapeCol, shapeRow, shapeIndex };
    }
    // Nueva pieza
    shapeIndex = (shapeIndex + 1) % NUM_SHAPES;
    colIndex   = (colIndex   + 1) % NUM_COLUMNS;
    shapeCol   = colPositions[colIndex];
    shapeRow   = -BLOCK_SIZE * 4;
  }
  redrawScreen = 1;
}

int main() {
  P1DIR |= BIT6;  P1OUT |= BIT6;
  configureClocks();
  lcd_init();
  clearScreen(BG_COLOR);

  // Columnas de aparición
  colPositions[0] = 10;
  colPositions[1] = screenWidth / 4;
  colPositions[2] = screenWidth / 2;
  colPositions[3] = 3 * screenWidth / 4;
  colPositions[4] = screenWidth - 10;

  shapeIndex = 0; colIndex = 0;
  shapeCol   = colPositions[colIndex];
  shapeRow   = -BLOCK_SIZE * 4;

  enableWDTInterrupts();
  or_sr(0x8);

  while (1) {
    if (redrawScreen) {
      redrawScreen = 0;
      draw_all();
    }
    P1OUT &= ~BIT6; or_sr(0x10); P1OUT |= BIT6;
  }
}
