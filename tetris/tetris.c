#include <msp430.h>
#include <libTimer.h>
#include "lcdutils.h"
#include "lcddraw.h"

// Tamaño de bloque (px)
#define BLOCK_SIZE 10

// Grilla de células
#define NUM_COLS (screenWidth / BLOCK_SIZE)  // 16
#define NUM_ROWS (screenHeight / BLOCK_SIZE) // 12
static unsigned short grid[NUM_COLS][NUM_ROWS]; // color de cada celda

// Definición de formas (4 offsets cada una)
typedef struct { short x,y; } Offset;
const Offset shapes[][4] = {
  {{0,0},{1,0},{0,1},{1,1}}, // cuadrado
  {{0,0},{1,0},{2,0},{3,0}}, // línea
  {{0,0},{0,1},{1,1},{2,1}}, // L
  {{1,0},{0,1},{1,1},{2,1}}  // T
};
#define NUM_SHAPES (sizeof(shapes)/sizeof(shapes[0]))

// Estado de pieza en caída
enum { FALSE=0, TRUE=1 };
int redraw = TRUE;
static short shapeX, shapeY;
static char shapeI;

// Paleta de colores por forma
unsigned short colors[NUM_SHAPES] = {COLOR_RED,COLOR_GREEN,COLOR_ORANGE,COLOR_BLUE};
#define BG COLOR_BLACK

// Dibuja un bloque en celda específica
static void draw_cell(int cx,int cy,unsigned short c) {
  fillRectangle(cx*BLOCK_SIZE, cy*BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE, c);
}

// Actualiza sólo la pieza móvil
static void update() {
  static int lastCells[4][2] = {{-1,-1}};
  int curCells[4][2];
  // calcular celdas actuales
  int baseCx = shapeX / BLOCK_SIZE;
  int baseCy = shapeY / BLOCK_SIZE;
  for(int i=0;i<4;i++){
    curCells[i][0] = baseCx + shapes[shapeI][i].x;
    curCells[i][1] = baseCy + shapes[shapeI][i].y;
  }
  // borrar viejas: restaurar color en grid o fondo
  for(int i=0;i<4;i++){
    int lx=lastCells[i][0], ly=lastCells[i][1];
    if(lx>=0 && lx<NUM_COLS && ly>=0 && ly<NUM_ROWS) {
      draw_cell(lx, ly, grid[lx][ly]);
    }
  }
  // dibujar nuevas
  for(int i=0;i<4;i++){
    int cx=curCells[i][0], cy=curCells[i][1];
    if(cx>=0 && cx<NUM_COLS && cy>=0 && cy<NUM_ROWS)
      draw_cell(cx, cy, colors[shapeI]);
  }
  // guardar
  for(int i=0;i<4;i++){ lastCells[i][0]=curCells[i][0]; lastCells[i][1]=curCells[i][1]; }
}

// Watchdog: mover, colisionar y apilar
void wdt_c_handler() {
  static int t=0;
  if(++t<64) return; t=0;
  shapeY += BLOCK_SIZE;
  // colisión suelo o bloques
  int collided=FALSE;
  for(int i=0;i<4;i++){
    int cx=(shapeX/BLOCK_SIZE)+shapes[shapeI][i].x;
    int cy=(shapeY/BLOCK_SIZE)+shapes[shapeI][i].y;
    if(cy>=NUM_ROWS || grid[cx][cy]!=BG) { collided=TRUE; break; }
  }
  if(collided) {
    shapeY -= BLOCK_SIZE;
    // fijar en grid
    for(int i=0;i<4;i++){
      int cx=(shapeX/BLOCK_SIZE)+shapes[shapeI][i].x;
      int cy=(shapeY/BLOCK_SIZE)+shapes[shapeI][i].y;
      if(cx>=0&&cx<NUM_COLS&&cy>=0&&cy<NUM_ROWS)
        grid[cx][cy]=colors[shapeI];
    }
    // nueva pieza
    shapeI = (shapeI+1)%NUM_SHAPES;
    static int spawnCol=0;
    shapeX = (spawnCol%NUM_COLS)*BLOCK_SIZE; spawnCol++;
    shapeY = -BLOCK_SIZE*4;
  }
  redraw=TRUE;
}

int main(){
  // init grid
  for(int x=0;x<NUM_COLS;x++)for(int y=0;y<NUM_ROWS;y++)grid[x][y]=BG;
  // init hardware
  P1DIR|=BIT6;P1OUT|=BIT6;
  configureClocks();lcd_init();clearScreen(BG);
  // estado inicial
  shapeI=0; shapeX=0; shapeY=-BLOCK_SIZE*4;
  enableWDTInterrupts(); or_sr(0x8);
  while(TRUE){
    if(redraw){ redraw=FALSE; update(); }
    P1OUT&=~BIT6; or_sr(0x10); P1OUT|=BIT6;
  }
}

