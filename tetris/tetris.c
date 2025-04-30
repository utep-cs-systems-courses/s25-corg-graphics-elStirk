#include <msp430.h>
#include <libTimer.h>
#include "lcdutils.h"
#include "lcddraw.h"

// Tamaño de bloque (px)
#define BLOCK_SIZE 10

// Dimensiones de la grilla
#define COLS (screenWidth / BLOCK_SIZE)   // 16
#define ROWS (screenHeight / BLOCK_SIZE)  // 12

// Grid que almacena color de cada celda (0 = BG)
static unsigned short grid[COLS][ROWS];

// Offsets de las piezas (4 bloques)
typedef struct { int x,y; } Offset;
const Offset shapes[][4] = {
  {{0,0},{1,0},{0,1},{1,1}},  // O
  {{0,0},{1,0},{2,0},{3,0}},  // I
  {{0,0},{0,1},{1,1},{2,1}},  // L
  {{1,0},{0,1},{1,1},{2,1}}   // T
};
#define NUM_SHAPES (sizeof(shapes)/sizeof(shapes[0]))

// Índice y posición de la pieza en caída
enum { FALSE=0, TRUE=1 };
static volatile int redrawFlag = TRUE;
static int shapeI, posX, posY; // posX,posY en celdas

// Colores por forma
unsigned short colors[NUM_SHAPES] = {
  COLOR_RED, COLOR_GREEN, COLOR_ORANGE, COLOR_BLUE
};
#define BG COLOR_BLACK

// Dibuja/reemplaza una celda de la grilla (x,y) con color c
static void draw_cell(int cx, int cy, unsigned short c) {
  fillRectangle(cx*BLOCK_SIZE, cy*BLOCK_SIZE,
                BLOCK_SIZE, BLOCK_SIZE, c);
  grid[cx][cy] = c;
}

// Dibuja la pieza móvil sin limpiar todo el LCD
static void update_piece(void) {
  static int last[4][2] = {{-1,-1}};
  int idx = shapeI;
  int cur[4][2];
  // calcular celdas actuales
  for(int i=0;i<4;i++) {
    cur[i][0] = posX + shapes[idx][i].x;
    cur[i][1] = posY + shapes[idx][i].y;
  }
  // borrar antiguas: restaurar color de grid
  for(int i=0;i<4;i++) {
    int lx = last[i][0], ly = last[i][1];
    if(lx>=0 && lx<COLS && ly>=0 && ly<ROWS)
      draw_cell(lx, ly, grid[lx][ly]);
  }
  // dibujar nuevas sobre grid
  for(int i=0;i<4;i++) {
    int cx = cur[i][0], cy = cur[i][1];
    if(cx>=0 && cx<COLS && cy>=0 && cy<ROWS)
      draw_cell(cx, cy, colors[idx]);
  }
  // guardar
  for(int i=0;i<4;i++) {
    last[i][0] = cur[i][0];
    last[i][1] = cur[i][1];
  }
}

// ISR WDT: hace caer la pieza y la fija al colisionar
void wdt_c_handler() {
  static int tick;
  if(++tick < 64) return;
  tick = 0;
  // mover hacia abajo en celdas
  posY++;
    // detectar colisión a nivel de píxeles:
  // línea de hit en pixel Y = 159, y X entre 0 y 120
  int hit = FALSE;
  for(int i=0;i<4;i++){
    int cx = posX + shapes[shapeI][i].x;
    int cy = posY + shapes[shapeI][i].y;
    int pixelY = cy * BLOCK_SIZE;
    int pixelX = cx * BLOCK_SIZE;
    // colisión si supera línea inferior o fuera de X permitido
    if(pixelY >= 159 || pixelX < 0 || pixelX > 120 || grid[cx][cy] != BG) {
      hit = TRUE; break;
    }
  }
  if(hit) {
    // retroceder 1 celda
    posY--;
    // fijar en grid y dibujar bloque fijo
    for(int i=0;i<4;i++){
      int cx = posX + shapes[shapeI][i].x;
      int cy = posY + shapes[shapeI][i].y;
      if(cx>=0 && cx<COLS && cy>=0 && cy<ROWS) {
        grid[cx][cy] = colors[shapeI];
        fillRectangle(cx*BLOCK_SIZE, cy*BLOCK_SIZE,
                      BLOCK_SIZE, BLOCK_SIZE,
                      colors[shapeI]);
      }
    }
    // generar próxima pieza con spawn limitado a columnas 0..12
    static int spawn = 0;
    shapeI = (shapeI+1)%NUM_SHAPES;
    posX = spawn++ % ((120 / BLOCK_SIZE) + 1);
    posY = -2;
  }
  redrawFlag = TRUE;=0;=0;
    posX = (spawn++ % COLS);
    posY = -2; // arriba
  }
  redrawFlag = TRUE;
}

int main(){
  // inicializar grid
  for(int x=0;x<COLS;x++)
    for(int y=0;y<ROWS;y++)
      grid[x][y]=BG;
  // hardware
  P1DIR |= BIT6; P1OUT |= BIT6;
  configureClocks(); lcd_init(); clearScreen(BG);
  // estado inicial
  shapeI = 0; posX = 0; posY = -2;
  // activar WDT y global
  enableWDTInterrupts(); or_sr(0x8);
  // loop principal
  while(1){
    if(redrawFlag){
      redrawFlag = FALSE;
      update_piece();
    }
    // bajo consumo
    P1OUT &= ~BIT6; or_sr(0x10); P1OUT |= BIT6;
  }
}

