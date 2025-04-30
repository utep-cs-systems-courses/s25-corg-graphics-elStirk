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

// Pines para botones en P2.0 (izq) y P2.1 (der)
#define SWITCHES    (BIT0 | BIT1)
#define LEFT_BTN    BIT0
#define RIGHT_BTN   BIT1

// Grilla: número de columnas según anchura de pantalla
typedef struct { short col, row; char idx; } Placed;
#define MAX_PLACED 48  // (160/10)*(128/10)/4
static Placed placed[MAX_PLACED];
static int placedCount = 0;
static int numColumns;

// Estado pieza en caída
enum { FALSE=0, TRUE=1 };
volatile int redrawScreen = TRUE;
volatile char moveLeft=FALSE, moveRight=FALSE;
static short shapeCol, shapeRow;
static char shapeIndex, colIndex;

// Colores para cada forma
unsigned short shapeColors[NUM_SHAPES] = { COLOR_RED, COLOR_GREEN,
                                           COLOR_ORANGE, COLOR_BLUE };
#define BG_COLOR COLOR_BLACK

// Prototipos
static void switch_init(void);
static char switch_update_interrupt_sense(void);
static void draw_piece(short col, short row, char idx, unsigned short color);
static void update_moving_shape(void);

// Inicializa botones con interrupciones en P2.0 y P2.1
static void switch_init(void) {
  P2REN |= SWITCHES;     // habilita resistencias
  P2OUT |= SWITCHES;     // pull-up
  P2IE  |= SWITCHES;     // habilita interrupciones
  P2IES |= SWITCHES;     // flanco alto->bajo
  P2IFG &= ~SWITCHES;    // limpia flags
}

// Manejador de puerto 2
void __interrupt_vec(PORT2_VECTOR) Port_2(void) {
  char p2val = P2IN;
  char changed = p2val ^ (P2IES & SWITCHES);
  // Detectar pulsación
  if ((changed & LEFT_BTN) && !(p2val & LEFT_BTN)) moveLeft = TRUE;
  if ((changed & RIGHT_BTN) && !(p2val & RIGHT_BTN)) moveRight = TRUE;
  // Resetear flags
  P2IES ^= changed;
  P2IFG &= ~SWITCHES;
  redrawScreen = TRUE;
}

// Dibuja una pieza en pantalla\static void draw_piece(short col, short row, char idx, unsigned short color) {
  for (int i = 0; i < 4; i++) {
    int x = col + shapes[idx][i].x * BLOCK_SIZE;
    int y = row + shapes[idx][i].y * BLOCK_SIZE;
    fillRectangle(x, y, BLOCK_SIZE, BLOCK_SIZE, color);
  }
}

// Actualiza solo la pieza en movimiento\static void update_moving_shape(void) {
  static short lastCol=0, lastRow=0;
  static char lastIdx=-1;
  // Borrar anterior
  if (lastIdx>=0) draw_piece(lastCol, lastRow, lastIdx, BG_COLOR);
  // Ajuste horizontal por botones
  if (moveLeft && shapeCol >= BLOCK_SIZE) {
    shapeCol -= BLOCK_SIZE;
  } else if (moveRight && shapeCol + 4*BLOCK_SIZE < screenWidth) {
    shapeCol += BLOCK_SIZE;
  }
  moveLeft = moveRight = FALSE;
  // Dibujar actual
  draw_piece(shapeCol, shapeRow, shapeIndex, shapeColors[shapeIndex]);
  lastCol = shapeCol; lastRow = shapeRow; lastIdx = shapeIndex;
}

// WDT: mueve pieza hacia abajo, detecta colisión y posición horizontal
void wdt_c_handler() {
  static int tick=0;
  if (++tick<64) return;
  tick=0;
  // Caída
  shapeRow += BLOCK_SIZE;
  // Colisión vertical
  int collided=FALSE;
  if (shapeRow + BLOCK_SIZE > screenHeight-1) collided=TRUE;
  else {
    for (int p=0;p<placedCount && !collided;p++){
      for(int i=0;i<4;i++){
        int x=shapeCol + shapes[shapeIndex][i].x*BLOCK_SIZE;
        int y=shapeRow + shapes[shapeIndex][i].y*BLOCK_SIZE;
        if(y+BLOCK_SIZE>placed[p].row && x==placed[p].col+shapes[placed[p].idx][i].x*BLOCK_SIZE){ collided=TRUE; break; }
      }
    }
  }
  if(collided){
    shapeRow -= BLOCK_SIZE;
    if(placedCount<MAX_PLACED){
      placed[placedCount++] = (Placed){shapeCol,shapeRow,shapeIndex};
      draw_piece(shapeCol,shapeRow,shapeIndex,shapeColors[shapeIndex]);
    }
    // Nueva pieza
    shapeIndex = (shapeIndex+1)%NUM_SHAPES;
    colIndex   = (colIndex+1)%numColumns;
    shapeCol   = colIndex*BLOCK_SIZE;
    shapeRow   = -BLOCK_SIZE*4;
  }
  redrawScreen=TRUE;
}

int main(){
  P1DIR |= BIT6; P1OUT |= BIT6;
  configureClocks();
  lcd_init(); clearScreen(BG_COLOR);
  switch_init();        // configura botones
  numColumns = screenWidth/BLOCK_SIZE;
  shapeIndex=0;colIndex=0;
  shapeCol=0;shapeRow=-BLOCK_SIZE*4;
  enableWDTInterrupts(); or_sr(0x8);
  while(1){
    if(redrawScreen){ redrawScreen=FALSE; update_moving_shape(); }
    P1OUT&=~BIT6; or_sr(0x10); P1OUT|=BIT6;
  }
}
