/* tetris.c - Simple Tetris-style mini-game for MSP430 using lcdLib */

#include <msp430.h>
#include <libTimer.h>
#include "lcdutils.h"
#include "lcddraw.h"
#include <stdint.h>
// Grid dimensions (LCD 96x64, using 6x6 pixel blocks)
#define GRID_COLS 16  // 32 / 6 = 16 columns
#define GRID_ROWS 30  // 20 / 6 = 10 rows
#define BLOCK_SIZE 6

// Switches on P2.0 = left, P2.1 = rotate
#define SWITCHES (BIT0 | BIT1)

// Game grid: 0 = empty, 1 = filled
uint8_t grid[GRID_ROWS][GRID_COLS];

// Current piece state
int currentPiece;
int rotation;
int posX, posY;
int tickCount;
int redrawScreen = 0;  // flag for WDT handler

// Block position within a tetromino
typedef struct { int8_t x, y; } BlockPos;
// Tetromino definition: 4 rotations, each with 4 blocks
typedef struct {
  BlockPos blocks[4][4];
  uint16_t color;
} Tetromino;

// Two simple pieces: O (square) and I (line)
const Tetromino pieces[2] = {
  // O piece (square)
  { { {{0,0},{1,0},{0,1},{1,1}},
      {{0,0},{1,0},{0,1},{1,1}},
      {{0,0},{1,0},{0,1},{1,1}},
      {{0,0},{1,0},{0,1},{1,1}} },
    COLOR_YELLOW },
  // I piece (vertical/horizontal)
  { { {{0,-1},{0,0},{0,1},{0,2}},
      {{-1,0},{0,0},{1,0},{2,0}},
      {{0,-1},{0,0},{0,1},{0,2}},
      {{-1,0},{0,0},{1,0},{2,0}} },
    COLOR_CYAN }
};

// Draw or clear a single block cell
void drawBlock(int x, int y, uint16_t color) {
  fillRectangle(x * BLOCK_SIZE, y * BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE, color);
}
void clearBlock(int x, int y) {
  fillRectangle(x * BLOCK_SIZE, y * BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE, COLOR_BLACK);
}
// Draw/clear current tetromino at (x,y)
void drawPiece(int p, int rot, int x, int y) {
  for (int i = 0; i < 4; i++) {
    int px = x + pieces[p].blocks[rot][i].x;
    int py = y + pieces[p].blocks[rot][i].y;
    if (py >= 0 && py < GRID_ROWS && px >= 0 && px < GRID_COLS)
      drawBlock(px, py, pieces[p].color);
  }
}
void clearPiece(int p, int rot, int x, int y) {
  for (int i = 0; i < 4; i++) {
    int px = x + pieces[p].blocks[rot][i].x;
    int py = y + pieces[p].blocks[rot][i].y;
    if (py >= 0 && py < GRID_ROWS && px >= 0 && px < GRID_COLS)
      clearBlock(px, py);
  }
}

// Check collision: returns 1 if colliding
int checkCollision(int p, int rot, int x, int y) {
  for (int i = 0; i < 4; i++) {
    int px = x + pieces[p].blocks[rot][i].x;
    int py = y + pieces[p].blocks[rot][i].y;
    if (px < 0 || px >= GRID_COLS || py >= GRID_ROWS) return 1;
    if (py >= 0 && grid[py][px]) return 1;
  }
  return 0;
}
// Fix piece into the grid
void fixPiece() {
  for (int i = 0; i < 4; i++) {
    int px = posX + pieces[currentPiece].blocks[rotation][i].x;
    int py = posY + pieces[currentPiece].blocks[rotation][i].y;
    if (py >= 0 && py < GRID_ROWS && px >= 0 && px < GRID_COLS)
      grid[py][px] = 1;
  }
}

// Spawn a new piece
void newPiece() {
  currentPiece = (tickCount / 20) % 2;  // alternate between pieces
  rotation = 0;
  posX = GRID_COLS / 2;
  posY = 0;
  if (checkCollision(currentPiece, rotation, posX, posY)) {
    // Game over: clear grid
    for (int r = 0; r < GRID_ROWS; r++)
      for (int c = 0; c < GRID_COLS; c++)
	grid[r][c] = 0;
  }
}

// WDT interrupt: drop and redraw
void wdt_c_handler() {
  tickCount++;
  redrawScreen = 1;
  if (tickCount % 20 == 0) {
    clearPiece(currentPiece, rotation, posX, posY);
    if (!checkCollision(currentPiece, rotation, posX, posY + 1)) {
      posY++;
    } else {
      fixPiece();
      newPiece();
    }
    drawPiece(currentPiece, rotation, posX, posY);
  }
}
// Switch (button) handling
static char switch_update_interrupt_sense() {
  char p2val = P2IN;
  P2IES |= (p2val & SWITCHES);
  P2IES &= (p2val | ~SWITCHES);
  return p2val;
}
void switch_init() {
  P2REN |= SWITCHES;
  P2OUT |= SWITCHES;
  P2DIR &= ~SWITCHES;
  P2IE  |= SWITCHES;
}
void switch_interrupt_handler() {
  char p2val = switch_update_interrupt_sense();
  if (!(p2val & BIT0)) {  // Left button
    clearPiece(currentPiece, rotation, posX, posY);
    if (!checkCollision(currentPiece, rotation, posX - 1, posY)) posX--;
    drawPiece(currentPiece, rotation, posX, posY);
  }
  if (!(p2val & BIT1)) {  // Rotate button
    clearPiece(currentPiece, rotation, posX, posY);
    int nr = (rotation + 1) & 3;
    if (!checkCollision(currentPiece, nr, posX, posY)) rotation = nr;
    drawPiece(currentPiece, rotation, posX, posY);
  }
  P2IFG &= ~SWITCHES;
}
// Main entry point
int main() {
  configureClocks();      // from libTimer
  lcd_init();
  clearScreen(COLOR_BLACK);
  switch_init();
  enableWDTInterrupts();
  or_sr(0x8);

  newPiece();
  drawPiece(currentPiece, rotation, posX, posY);

  while (1) {
    if (redrawScreen) {
      redrawScreen = 0;
      // You can implement row clearing here
    }
    or_sr(0x10);  // CPU off, enable interrupts
  }
  return 0;
}
