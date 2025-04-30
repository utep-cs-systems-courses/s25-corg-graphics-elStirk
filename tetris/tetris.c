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

// Cálculos de grilla dinámica según pantalla
static int numColumns;            // columnas posibles en la anchura
static int numRows;               // filas posibles en la altura

// Registro de piezas colocadas para stacking
#define MAX_PLACED 192          // límite máximo seguro (16 cols * 12 filas)
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

  // Mover pieza hacia abajo
  shapeRow += BLOCK_SIZE;

  // Detectar colisión: suelo o tope de otra pieza
  int collided = 0;
  if (shapeRow + BLOCK_SIZE > screenHeight - 1) {
    collided = 1;
  } else {
    // Verificar contra piezas colocadas
    for (int p = 0; p < placedCount; p++) {
      for (int i = 0; i < 4; i++) {
        int x = shapeCol + shapes[shapeIndex][i].x * BLOCK_SIZE;
        int y = shapeRow + shapes[shapeIndex][i].y * BLOCK_SIZE;
        // colisión si la nueva Y alcanza la fila de pieza colocada
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
    // Guardar pieza en el array si hay espacio
    if (placedCount < MAX_PLACED) {
      placed[placedCount++] = (Placed){ shapeCol, shapeRow, shapeIndex };
    }
    // Generar nueva pieza en próxima columna
    shapeIndex = (shapeIndex + 1) % NUM_SHAPES;
    colIndex   = (colIndex   + 1) % numColumns;
    shapeCol   = colIndex * BLOCK_SIZE;
    shapeRow   = -BLOCK_SIZE * 4;
  }
  redrawScreen = 1;
}

int main() {
  P1DIR |= BIT6; P1OUT |= BIT6;          // LED en P1.6 ON
  configureClocks();
  lcd_init();
  clearScreen(BG_COLOR);

  // Calcular grilla según pantalla rotada
  numColumns = screenWidth / BLOCK_SIZE;
  numRows    = screenHeight / BLOCK_SIZE;

  // Coordenadas iniciales de la pieza
  shapeIndex = 0;
  colIndex   = 0;
  shapeCol   = colIndex * BLOCK_SIZE;
  shapeRow   = -BLOCK_SIZE * 4;

  enableWDTInterrupts();                  // activar WDT
  or_sr(0x8);                             // interrupciones globales

  while (1) {
    if (redrawScreen) {
      redrawScreen = 0;
      draw_all();
    }
    P1OUT &= ~BIT6; or_sr(0x10); P1OUT |= BIT6; // CPU OFF entre ISR
  }
}

