// Digital Sand Hourglass (gravity style, neck at 8H) for 2x 8x8 RGB I2C panels + MPU6050
// Bottom display logic is 100% same as your confirmed-correct version.
// Top display: LEDs turn off in diagonal order 1A -> 8H,
// but within each diagonal we turn off "middle" (closest to main diagonal) first,
// then left/right -> feels more gravity sensitive.
//
// This version:
//  - Display color is fixed GREEN (0,255,0)
//  - Brightness pot removed
//  - NEW: On the TOP display a "hole" starts at 8H whenever a grain leaves,
//         then climbs up along 8H→7G→6F→5E→4D→3C→2B→1A so it looks like
//         sand is always falling downward into 8H and the empty space
//         is filled from above.

#include <Wire.h>
#include <MPU6050_tockn.h>

// ---------- I2C addresses ----------
#define TOP_ADDR  0x12
#define BOT_ADDR  0x13

// ---------- Panel rotation (viewed from front) ----------
#define TOP_ROT_DEG  90
#define BOT_ROT_DEG  270

// ---------- Pots ----------
#define POT_TIME_PIN    A0   // time selection only (brightness pot removed)

// ---------- Buzzer ----------
#define BUZZ_PIN 3

// ---------- Neck position (logical 8H) ----------
#define NECK_ROW 7      // row index for 8H
#define NECK_COL 7      // col index for 8H

// ---------- Color: GREEN ----------
const uint8_t SAND_R = 0;
const uint8_t SAND_G = 255;
const uint8_t SAND_B = 0;

// ---------- MPU / gravity ----------
MPU6050 mpu6050(Wire);
#define ACC_THRESHOLD_LOW  -25
#define ACC_THRESHOLD_HIGH  25
const unsigned long FLIP_DEBOUNCE_MS = 800;

// ---------- State ----------
bool topToBottom = true;     // true: top is X(source), bottom is Y(target)
unsigned long lastFlipMs = 0;
int lastGravity = 0;

unsigned long targetMs;       // whole timer duration
unsigned long stepIntervalMs; // ms per animation step
unsigned long lastStepMs = 0;
bool finished = false;

// Logical masks for X (upper) and Y (lower) in 8x8 grid
uint8_t maskX[8];   // 1 = LED ON (sand still in upper)
uint8_t maskY[8];   // built from yStatic + active grain

// Static sand cells on bottom (Y)
bool yStatic[8][8]; // [row][col]

// One active falling grain on bottom
bool grainActive = false;
int  grRow = NECK_ROW, grCol = NECK_COL;

// Toggle for alternating left/right when both sides are free (bottom)
bool sideToggle = false;     // false = left, true = right

// Top-side order of LEDs to remove (64 entries)
uint8_t orderRow[64];
uint8_t orderCol[64];

uint8_t grainIndex = 0;      // which LED in order (0..63)

// ---------- Extra: moving "hole" on top along 1A..8H ----------
bool topHoleActive = false;
int  holeRow = NECK_ROW;     // current hole position
int  holeCol = NECK_COL;

// ---------- Helper: buzzer ----------
void beep(int f, int d) {
  if (BUZZ_PIN >= 0) {
    tone(BUZZ_PIN, f, d);
  }
}

// ---------- Rotation helper (x,y in 0..7) ----------
void rotate8(uint8_t x, uint8_t y, int deg, uint8_t &rx, uint8_t &ry) {
  deg = ((deg % 360) + 360) % 360;
  if (deg == 0)       { rx = x;        ry = y;       }
  else if (deg == 90) { rx = 7 - y;    ry = x;       }
  else if (deg == 180){ rx = 7 - x;    ry = 7 - y;   }
  else if (deg == 270){ rx = y;        ry = 7 - x;   }
  else                { rx = x;        ry = y;       }
}

// Copy logical 8x8 mask into destination with rotation
void applyRotation(uint8_t src[8], int deg, uint8_t dst[8]) {
  for (int i = 0; i < 8; i++) dst[i] = 0;
  for (uint8_t r = 0; r < 8; r++) {
    for (uint8_t c = 0; c < 8; c++) {
      if (src[r] & (1 << c)) {
        uint8_t x = c, y = r;
        uint8_t rx, ry;
        rotate8(x, y, deg, rx, ry);
        dst[ry] |= (1 << rx);
      }
    }
  }
}

// ---------- Send one frame ----------
void sendFrame(uint8_t addr, uint8_t mask[8]) {
  Wire.beginTransmission(addr);
  for (int i = 0; i < 8; i++) Wire.write(mask[i]);

  // Fixed GREEN color
  Wire.write(SAND_R); // R
  Wire.write(SAND_G); // G
  Wire.write(SAND_B); // B

  Wire.endTransmission();
}

// ---------- TOP ORDER: Diagonal 1A -> 8H, but "center-first" per diagonal ----------
void buildOrderDiagonal() {
  int idx = 0;

  struct Node {
    uint8_t r;
    uint8_t c;
    uint8_t d; // |r - c|
  };

  for (int s = 0; s <= 14; s++) {  // s = row + col
    Node nodes[8];
    int n = 0;

    // collect all cells with row+col == s
    for (int r = 0; r < 8; r++) {
      int c = s - r;
      if (c >= 0 && c < 8) {
        nodes[n].r = (uint8_t)r;
        nodes[n].c = (uint8_t)c;
        int diff = r - c;
        if (diff < 0) diff = -diff;
        nodes[n].d = (uint8_t)diff;
        n++;
      }
    }

    // sort by d ascending (simple bubble sort, n <= 8)
    for (int i = 0; i < n - 1; i++) {
      for (int j = i + 1; j < n; j++) {
        if (nodes[j].d < nodes[i].d) {
          Node tmp = nodes[i];
          nodes[i] = nodes[j];
          nodes[j] = tmp;
        }
      }
    }

    // push into order arrays
    for (int i = 0; i < n; i++) {
      if (idx < 64) {
        orderRow[idx] = nodes[i].r;
        orderCol[idx] = nodes[i].c;
        idx++;
      }
    }
  }
  // first: (0,0)=1A, last: (7,7)=8H, with center-first per diagonal.
}

// ---------- Get duration from time pot ----------
unsigned long getDurationFromPot() {
  int v = analogRead(POT_TIME_PIN);

  if      (v < 170)  return 30000UL;      // 30 sec
  else if (v < 410)  return 60000UL;      // 1 min
  else if (v < 600)  return 120000UL;     // 2 min
  else if (v < 820)  return 300000UL;     // 5 min
  else if (v < 900)  return 600000UL;     // 10 min
  else               return 1800000UL;    // 30 min
}

// ---------- Build maskY from yStatic + active grain ----------
void buildMaskY() {
  for (int r = 0; r < 8; r++) maskY[r] = 0;

  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) {
      if (yStatic[r][c]) {
        maskY[r] |= (1 << c);
      }
    }
  }

  if (grainActive) {
    if (grRow >= 0 && grRow < 8 && grCol >= 0 && grCol < 8) {
      maskY[grRow] |= (1 << grCol);
    }
  }
}

// ---------- Render one frame ----------
void renderFrame() {
  buildMaskY();

  uint8_t tempX[8], tempY[8];
  for (int i = 0; i < 8; i++) {
    tempX[i] = maskX[i];
    tempY[i] = maskY[i];
  }

  // Apply moving "hole" on top: clear that pixel even if maskX has it ON
  if (topHoleActive) {
    if (holeRow >= 0 && holeRow < 8 && holeCol >= 0 && holeCol < 8) {
      tempX[holeRow] &= ~(1 << holeCol);
    }
  }

  uint8_t topMask[8], botMask[8];

  if (topToBottom) {
    applyRotation(tempX, TOP_ROT_DEG, topMask);   // X = top
    applyRotation(tempY, BOT_ROT_DEG, botMask);   // Y = bottom
  } else {
    applyRotation(tempY, TOP_ROT_DEG, topMask);   // Y = top
    applyRotation(tempX, BOT_ROT_DEG, botMask);   // X = bottom
  }

  sendFrame(TOP_ADDR, topMask);
  sendFrame(BOT_ADDR, botMask);
}

// ---------- Start / restart cycle ----------
void startNewCycle(bool dirTopToBottom) {
  topToBottom = dirTopToBottom;
  finished = false;
  grainIndex = 0;
  grainActive = false;
  sideToggle = false;   // reset alternation

  topHoleActive = false; // reset top hole

  // X fully full, Y empty
  for (int i = 0; i < 8; i++) {
    maskX[i] = 0xFF;
    maskY[i] = 0x00;
  }
  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) {
      yStatic[r][c] = false;
    }
  }

  targetMs = getDurationFromPot();
  // approximate frames: ~10 per grain
  unsigned int totalFrames = 64 * 10;
  if (totalFrames == 0) totalFrames = 1;
  stepIntervalMs = targetMs / totalFrames;
  if (stepIntervalMs == 0) stepIntervalMs = 1;

  lastStepMs = millis();

  Serial.print("[EVENT] New cycle started, duration(ms): ");
  Serial.println(targetMs);

  beep(1500, 80);
  delay(80);
  beep(1800, 100);
}

// ---------- Gravity / orientation ----------
int getGravity() {
  int x = mpu6050.getAngleX();
  int y = mpu6050.getAngleY();

  if (y < ACC_THRESHOLD_LOW)  return 90;
  if (x > ACC_THRESHOLD_HIGH) return 0;
  if (y > ACC_THRESHOLD_HIGH) return 270;
  if (x < ACC_THRESHOLD_LOW)  return 180;
  return lastGravity;
}

// Check flip & restart if needed
void checkFlip() {
  int g = getGravity();
  unsigned long now = millis();

  bool newDir = (g == 90);  // same convention as earlier

  if (newDir != topToBottom && (now - lastFlipMs) > FLIP_DEBOUNCE_MS) {
    lastFlipMs = now;
    Serial.print("[EVENT] Flip detected, gravity = ");
    Serial.println(g);
    startNewCycle(newDir);
  }
  lastGravity = g;
}

// ---------- One animation step ----------
void stepAnimation() {
  if (finished) {
    renderFrame();
    return;
  }

  // TOP: choose which LED to turn off, and start a "hole" at 8H
  if (!grainActive && grainIndex < 64) {
    uint8_t r = orderRow[grainIndex];
    uint8_t c = orderCol[grainIndex];
    maskX[r] &= ~(1 << c);   // remove that upper sand pixel

    // spawn new grain at logical 8H (neck) on bottom
    grRow = NECK_ROW;   // 7
    grCol = NECK_COL;   // 7
    grainActive = true;

    // spawn a moving "hole" on top starting at 8H
    topHoleActive = true;
    holeRow = NECK_ROW;
    holeCol = NECK_COL;
  }
  else if (grainActive) {
    bool moved = false;

    // 1) main "down" direction: 8H->7G->6F->5E->4D->3C->2B->1A   (row-1,col-1)
    if (grRow > 0 && grCol > 0 && !yStatic[grRow - 1][grCol - 1]) {
      grRow--;
      grCol--;
      moved = true;
    } else {
      // 2) spread around that line:
      //    option A: move left (same row, col-1)
      //    option B: move "down-right" (row-1, same col)
      bool canLeft  = (grCol > 0   && !yStatic[grRow][grCol - 1]);
      bool canRight = (grRow > 0   && !yStatic[grRow - 1][grCol]);

      if (canLeft || canRight) {
        int choice = 0;
        if (canLeft && canRight) {
          // Alternate sides: left, right, left, right...
          choice = sideToggle ? 1 : -1;
          sideToggle = !sideToggle;
        } else if (canLeft) {
          choice = -1;
        } else { // canRight only
          choice = 1;
        }

        if (choice == -1) {
          grCol--;       // slide left
          moved = true;
        } else if (choice == 1) {
          grRow--;       // slide slightly "down-right"
          moved = true;
        }
      }
    }

    if (!moved) {
      // settle grain on bottom
      if (grRow >= 0 && grRow < 8 && grCol >= 0 && grCol < 8) {
        yStatic[grRow][grCol] = true;
      }

      grainActive = false;
      grainIndex++;

      if (grainIndex >= 64) {
        finished = true;
        Serial.println("[EVENT] Timer finished");
        beep(900, 200);
        delay(80);
        beep(700, 220);
      }
    }
  }

  // Move top "hole" UP along 8H->7G->6F->...->1A
  if (topHoleActive) {
    if (holeRow > 0 && holeCol > 0) {
      holeRow--;
      holeCol--;
    } else {
      topHoleActive = false;  // reached 1A, stop
    }
  }

  renderFrame();
}

// ---------- Setup ----------
void setup() {
  Serial.begin(9600);
  pinMode(BUZZ_PIN, OUTPUT);

  Wire.begin();
  mpu6050.begin();
  mpu6050.calcGyroOffsets(true);

  buildOrderDiagonal();       // diagonal 1A->8H, center-first per diagonal
  randomSeed(analogRead(A2)); // for bottom side alternation

  lastGravity = getGravity();
  bool dir = (lastGravity == 90);
  startNewCycle(dir);

  Serial.println("[BOOT] Gravity Sand Hourglass (top hole flow, neck 8H, GREEN) Ready");
}

// ---------- Main loop ----------
void loop() {
  mpu6050.update();
  checkFlip();

  unsigned long now = millis();
  if (now - lastStepMs >= stepIntervalMs) {
    lastStepMs = now;
    stepAnimation();
  }
}
