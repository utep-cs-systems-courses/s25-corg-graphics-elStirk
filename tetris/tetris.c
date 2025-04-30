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

// Columnas posibles según anchura de pantalla
static int numColumns;

// Registro de piezas colocadas para stacking
typedef struct { short col, row; char idx; } Placed;
#define MAX_PLACED ((160/ BLOCK_SIZE) * (128/ BLOCK_SIZE) / 4)
static Placed placed[MAX_PLACED];
static int placedCount = 0;

// Estado de la pieza en caída
enum { FALSE = 0, TRUE = 1 };
static volatile int redrawScreen = TRUE;
static short shapeCol, shapeRow;
static char shapeIndex, colIndex;

// Colores para cada forma
unsigned short shapeColors[NUM_SHAPES] = {
  COLOR_RED, COLOR_GREEN,
  COLOR_ORANGE, COLOR_BLUE
};
#define BG_COLOR COLOR_BLACK

// Dibuja una pieza (usada para fijas y en movimiento)
static void draw_piece(short col, short row, char idx, unsigned short color) {
  for (int i = 0; i < 4; i++) {
    int x = col + shapes[idx][i].x * BLOCK_SIZE;
    int y = row + shapes[idx][i].y * BLOCK_SIZE;
    fillRectangle(x, y, BLOCK_SIZE, BLOCK_SIZE, color);
  }
}

// Actualiza solo la pieza en movimiento sin limpiar toda la pantalla
static void update_moving_shape(void) {
  static short lastCol = 0, lastRow = 0;
  static char  lastIdx = -1;
  // Borrar bloques de la forma anterior, restaurando bloques fijos si existen
  if (lastIdx >= 0) {
    for (int i = 0; i < 4; i++) {
      int bx = lastCol + shapes[lastIdx][i].x * BLOCK_SIZE;
      int by = lastRow + shapes[lastIdx][i].y * BLOCK_SIZE;
      int blkColor = BG_COLOR;
      // Verificar si aquí hay un bloque fijo
      for (int p = 0; p < placedCount; p++) {
        for (int j = 0; j < 4; j++) {
          int px = placed[p].col + shapes[placed[p].idx][j].x * BLOCK_SIZE;
          int py = placed[p].row + shapes[placed[p].idx][j].y * BLOCK_SIZE;
          if (px == bx && py == by) {
            blkColor = shapeColors[placed[p].idx];
            break;
          }
        }
        if (blkColor != BG_COLOR) break;
      }
      fillRectangle(bx, by, BLOCK_SIZE, BLOCK_SIZE, blkColor);
    }
  }
  // Dibujar la forma actual
  for (int i = 0; i < 4; i++) {
    int bx = shapeCol + shapes[shapeIndex][i].x * BLOCK_SIZE;
    int by = shapeRow + shapes[shapeIndex][i].y * BLOCK_SIZE;
    fillRectangle(bx, by, BLOCK_SIZE, BLOCK_SIZE, shapeColors[shapeIndex]);
  }
  // Guardar para la siguiente iteración
  lastCol = shapeCol;
  lastRow = shapeRow;
  lastIdx = shapeIndex;
}
// Dibujar la forma actual
  draw_piece(shapeCol, shapeRow, shapeIndex, shapeColors[shapeIndex]);
  // Guardar para la siguiente iteración
  lastCol = shapeCol;
  lastRow = shapeRow;
  lastIdx = shapeIndex;
}

// Watchdog Timer Interrupt: mueve pieza hacia abajo y maneja colisiones
void wdt_c_handler() {
  static int tick = 0;
  if (++tick < 64) return; // Ajuste de velocidad (~512Hz/64)
  tick = 0;

  // Avanzar pieza verticalmente
  shapeRow += BLOCK_SIZE;

  // Comprobar colisión con suelo o piezas apiladas
  int collided = FALSE;
  if (shapeRow + BLOCK_SIZE > screenHeight - 1) {
    collided = TRUE;
  } else {
    for (int p = 0; p < placedCount && !collided; p++) {
      for (int i = 0; i < 4; i++) {
        int x = shapeCol + shapes[shapeIndex][i].x * BLOCK_SIZE;
        int y = shapeRow + shapes[shapeIndex][i].y * BLOCK_SIZE;
        if (y + BLOCK_SIZE > placed[p].row &&
            x == placed[p].col + shapes[placed[p].idx][i].x * BLOCK_SIZE) {
          collided = TRUE;
          break;
        }
      }
    }
  }

  if (collided) {
    // Ajustar posición justo encima de la colisión
    shapeRow -= BLOCK_SIZE;
    // Guardar pieza en el arreglo de apiladas y dibujarla fija
    if (placedCount < MAX_PLACED) {
      placed[placedCount] = (Placed){ shapeCol, shapeRow, shapeIndex };
      draw_piece(shapeCol, shapeRow, shapeIndex, shapeColors[shapeIndex]);
      placedCount++;
    }
    // Generar nueva pieza en la siguiente columna
    shapeIndex = (shapeIndex + 1) % NUM_SHAPES;
    colIndex   = (colIndex + 1) % numColumns;
    shapeCol   = colIndex * BLOCK_SIZE;
    shapeRow   = -BLOCK_SIZE * 4;
  }

  redrawScreen = TRUE;
}

int main() {
  // Inicialización rápida del LED
  P1DIR |= BIT6; P1OUT |= BIT6;
  configureClocks();
  lcd_init();
  clearScreen(BG_COLOR);

  // Calcular columnas para iterar de izquierda a derecha
  numColumns = screenWidth / BLOCK_SIZE;

  // Posición y forma inicial
  shapeIndex = 0;
  colIndex   = 0;
  shapeCol   = 0;
  shapeRow   = -BLOCK_SIZE * 4;

  // Activar Watchdog Timer y habilitar interrupciones
  enableWDTInterrupts();
  or_sr(0x8);

  // Bucle principal: solo redibuja la pieza en movimiento
  while (TRUE) {
    if (redrawScreen) {
      redrawScreen = FALSE;
      update_moving_shape();
    }
    // CPU OFF entre interrupciones
    P1OUT &= ~BIT6;
    or_sr(0x10);
    P1OUT |= BIT6;
  }
}
