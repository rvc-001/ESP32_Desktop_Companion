
#include <Arduino_GFX_Library.h>
#include <Adafruit_GFX.h>

// ============================================================
// Waveshare ESP32-S3-LCD-2 (NON-TOUCH) Deskoo V2
// 320x240 landscape, full-frame buffered rendering
// ============================================================

// ---- LCD pins verified from your working board/example ----
#define LCD_SCLK 39
#define LCD_MOSI 38
#define LCD_MISO 40
#define LCD_DC   42
#define LCD_RST  -1
#define LCD_CS   45
#define LCD_BL   1

#define NATIVE_W 240
#define NATIVE_H 320
#define W 320
#define H 240

// RGB565 colors
#define C_BLACK   0x0000
#define C_WHITE   0xFFFF
#define C_BG      0x1801
#define C_PANEL   0x3004
#define C_PANEL2  0x7804
#define C_DIM     0xC371
#define C_CYAN    0xFBAE
#define C_PURPLE  0x8805
#define C_PINK    0xF18D
#define C_GREEN   0xFDC0
#define C_RED     0xF800
#define C_YELLOW  0xFFE0
#define C_ORANGE  0xFD20
#define C_BLUE    0x001F
#define C_GRAD_TOP  0x3001
#define C_GRAD_MID  0x8801
#define C_GRAD_EDGE 0x7000
#define C_GRAD_HOT  0xF802
#define C_MAROON    0x3004
#define C_CRIMSON   0xB807
#define C_GLOW      0xFBAE
#define C_CORAL     0xFA88
#define C_RABBIT_EDGE 0xBDF7
#define C_RABBIT_SHADE 0xD69A
#define C_CARROT    0xFC60
#define C_LEAF      0x6EE8

// Brand colors rendered in RGB565.
#define C_WHATSAPP 0x268C
#define C_APPLE_TOP 0xFB56
#define C_APPLE_BOT 0xF804
#define C_VSCODE   0x03D9
#define C_VSCODE_D 0x0255
#define C_ZEN_BLACK 0x0000
#define C_ZEN_MARK  0xFFDF
#define C_DECK_BG   0x1801
#define C_TILE_BG   0x2806
#define C_TILE_SEL  0x4808
#define C_TILE_LINE 0x70CD

Arduino_DataBus *bus = new Arduino_ESP32SPI(
  LCD_DC, LCD_CS, LCD_SCLK, LCD_MOSI, LCD_MISO
);

Arduino_GFX *lcd = new Arduino_ST7789(
  bus, LCD_RST, 1, true, NATIVE_W, NATIVE_H
);

// Full 320x240 RGB565 framebuffer (~150 KB).
// GFXcanvas16 gives us clean off-screen rendering.
GFXcanvas16 frame(W, H);

// -------------------------- STATE -----------------------------
enum Page : uint8_t { PAGE_MUSIC = 0, PAGE_DECK = 1, PAGE_RABBIT = 2 };
Page currentPage = PAGE_MUSIC;

enum RabbitMood : uint8_t { RABBIT_SLEEP = 0, RABBIT_MUSIC = 1, RABBIT_EAT = 2 };
RabbitMood rabbitMood = RABBIT_SLEEP;

String songTitle = "Deskoo";
String songArtist = "Waiting for media...";
int songProgress = 0;
bool songPlaying = false;

int deckSelection = 0;
const uint8_t DECK_COUNT = 9;
const char* deckNames[DECK_COUNT] = {
  "WhatsApp", "Zen Browser", "VS Code",
  "Apple Music", "Previous Track", "Play/Pause",
  "Next Track", "Lock PC", "Shutdown"
};
const char* deckLabels[DECK_COUNT] = {
  "WhatsApp", "Zen Browser", "VS Code",
  "Apple Music", "Prev", "Play/Pause",
  "Next", "Lock PC", "Shutdown"
};

#define COVER_SIZE 64
uint16_t coverPixels[COVER_SIZE * COVER_SIZE];
bool coverReady = false;
bool coverReceiving = false;
uint16_t coverWriteCount = 0;
uint8_t musicControlSelection = 1;

uint32_t lastFrameMs = 0;
uint32_t lastHeartbeatMs = 0;
uint32_t lastMusicTickMs = 0;
float rabbitPhase = 0.0f;
uint8_t rabbitBlink = 0;
uint32_t nextBlinkMs = 0;

String rxLine;

// ------------------------ HELPERS -----------------------------

static inline uint16_t lerp565(uint16_t a, uint16_t b, uint8_t t) {
  // t: 0..255
  uint8_t ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  uint8_t br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  uint8_t rr = ar + ((int16_t)(br - ar) * t >> 8);
  uint8_t rg = ag + ((int16_t)(bg - ag) * t >> 8);
  uint8_t rb = ab + ((int16_t)(bb - ab) * t >> 8);
  return (rr << 11) | (rg << 5) | rb;
}

void clearFrame(uint16_t color) {
  frame.fillScreen(color);
}

void drawDeskooBackground(uint8_t pulse = 0) {
  uint16_t *buf = frame.getBuffer();
  int glowBoost = pulse / 3;

  for (int y = 0; y < H; y++) {
    uint16_t base;
    if (y < 140) {
      base = lerp565(C_GRAD_TOP, C_GRAD_MID, (uint8_t)((y * 255) / 140));
    } else {
      base = lerp565(C_GRAD_MID, C_GRAD_EDGE, (uint8_t)(((y - 140) * 255) / 99));
    }

    for (int x = 0; x < W; x++) {
      int dx = abs(x - 160);
      int dy = abs(y - 245);
      int glow = 255 - (dx * dx) / 88 - (dy * dy) / 20 + glowBoost;
      if (glow < 0) glow = 0;
      if (glow > 255) glow = 255;
      buf[y * W + x] = lerp565(base, C_GRAD_HOT, (uint8_t)glow);
    }
  }

  frame.fillRect(0, 0, W, 22, C_BLACK);
}

void fillRoundGradientRect(int x, int y, int w, int h, int r,
                           uint16_t topColor, uint16_t bottomColor) {
  for (int yy = 0; yy < h; yy++) {
    uint8_t t = (h <= 1) ? 0 : (uint8_t)((yy * 255) / (h - 1));
    uint16_t c = lerp565(topColor, bottomColor, t);
    int inset = 0;

    if (yy < r) {
      float dy = (float)(r - yy);
      float inside = (float)(r * r) - dy * dy;
      if (inside < 0) inside = 0;
      inset = r - (int)sqrtf(inside);
    } else if (yy >= h - r) {
      float dy = (float)(yy - (h - r - 1));
      float inside = (float)(r * r) - dy * dy;
      if (inside < 0) inside = 0;
      inset = r - (int)sqrtf(inside);
    }

    frame.drawFastHLine(x + inset, y + yy, w - inset * 2, c);
  }
}

void drawThickLine(int x0, int y0, int x1, int y1, uint8_t radius, uint16_t color) {
  for (int i = 0; i <= 12; i++) {
    int x = x0 + ((x1 - x0) * i) / 12;
    int y = y0 + ((y1 - y0) * i) / 12;
    frame.fillCircle(x, y, radius, color);
  }
}

void drawRing(int cx, int cy, int r, uint8_t thickness, uint16_t color) {
  for (int i = 0; i < thickness; i++) {
    frame.drawCircle(cx, cy, r - i, color);
  }
}

void printText(int16_t x, int16_t y, const String &s,
               uint16_t color, uint8_t size = 1) {
  frame.setTextWrap(false);
  frame.setTextColor(color);
  frame.setTextSize(size);
  frame.setCursor(x, y);
  frame.print(s);
}

void centerText(int16_t y, const String &s, uint16_t color, uint8_t size = 1) {
  int16_t x1, y1;
  uint16_t tw, th;
  frame.setTextSize(size);
  frame.getTextBounds(s, 0, y, &x1, &y1, &tw, &th);
  printText((W - tw) / 2, y, s, color, size);
}

String fitText(String s, uint16_t maxWidth, uint8_t size = 1) {
  int16_t x1, y1;
  uint16_t tw, th;

  frame.setTextSize(size);
  frame.getTextBounds(s, 0, 0, &x1, &y1, &tw, &th);
  while (tw > maxWidth && s.length() > 3) {
    s = s.substring(0, s.length() - 4) + "...";
    frame.getTextBounds(s, 0, 0, &x1, &y1, &tw, &th);
  }
  return s;
}

void pushFrame() {
  // One transfer of the complete finished frame -> avoids visible erase/redraw flicker.
  lcd->draw16bitRGBBitmap(0, 0, frame.getBuffer(), W, H);
}

void drawPageDots(int active) {
  int dotX = 151;
  for (int i = 0; i < 3; i++) {
    int cx2 = dotX + i * 14;
    if (i == active) {
      frame.fillCircle(cx2, 15, 5, lerp565(C_CYAN, C_WHITE, 100)); // glow halo
      frame.fillCircle(cx2, 15, 4, C_WHITE);
    } else {
      frame.fillCircle(cx2, 15, 2, C_PANEL2);
    }
  }
}

void drawTopBar(const char *label, int active) {
  // Gradient panel background for top bar
  for (int y = 0; y < 29; y++) {
    uint8_t t = (uint8_t)((y * 255) / 28);
    frame.drawFastHLine(0, y, W, lerp565(C_PANEL, C_BG, t));
  }
  frame.drawFastHLine(0, 28, W, C_PANEL2);
  // Page label
  printText(10, 8, label, C_WHITE, 2);
  // Brand
  printText(197, 10, "DESKOO", C_DIM, 1);
  // USB pill badge
  frame.fillRoundRect(249, 6, 34, 15, 5, lerp565(C_GREEN, C_BLACK, 120));
  frame.drawRoundRect(249, 6, 34, 15, 5, C_GREEN);
  printText(255, 10, "USB", C_GREEN, 1);
  // Status LED with glow
  frame.fillCircle(302, 14, 5, lerp565(C_GREEN, C_BLACK, 160));
  frame.fillCircle(302, 14, 3, C_GREEN);
  frame.fillCircle(301, 13, 1, lerp565(C_GREEN, C_WHITE, 180));
  drawPageDots(active);
}

uint8_t hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return 10 + c - 'A';
  if (c >= 'a' && c <= 'f') return 10 + c - 'a';
  return 0;
}

uint16_t parseHex565(const String &s, int start) {
  return (hexNibble(s[start]) << 12) |
         (hexNibble(s[start + 1]) << 8) |
         (hexNibble(s[start + 2]) << 4) |
          hexNibble(s[start + 3]);
}

void drawAlbumArtPlaceholder() {
  const int artX = 104;
  const int artY = 35;
  const int artSize = 112;

  fillRoundGradientRect(artX, artY, artSize, artSize, 10, C_MAROON, C_BG);
  frame.drawRoundRect(artX + 15, artY + 18, 82, 76, 6, C_PANEL2);
  frame.fillCircle(artX + 39, artY + 45, 8, C_PANEL2);
  frame.fillTriangle(artX + 27, artY + 88, artX + 55, artY + 57, artX + 78, artY + 88, C_PANEL2);
  frame.fillTriangle(artX + 58, artY + 88, artX + 80, artY + 66, artX + 97, artY + 88, C_PANEL2);
  centerText(78, "ALBUM", C_DIM, 1);
  centerText(91, "ART", C_DIM, 1);
}

void drawAlbumArt() {
  const int artX = 104;
  const int artY = 35;
  const int artSize = 112;

  frame.fillRoundRect(artX - 4, artY - 4, artSize + 8, artSize + 8, 12, C_PANEL);
  frame.drawRoundRect(artX - 4, artY - 4, artSize + 8, artSize + 8, 12, C_PANEL2);

  if (!coverReady) {
    drawAlbumArtPlaceholder();
    return;
  }

  for (int y = 0; y < artSize; y++) {
    int sy = (y * COVER_SIZE) / artSize;
    for (int x = 0; x < artSize; x++) {
      int sx = (x * COVER_SIZE) / artSize;
      frame.drawPixel(artX + x, artY + y, coverPixels[sy * COVER_SIZE + sx]);
    }
  }

  frame.drawRoundRect(artX - 1, artY - 1, artSize + 2, artSize + 2, 8, C_WHITE);
  frame.drawRoundRect(artX - 2, artY - 2, artSize + 4, artSize + 4, 9, C_PANEL2);
}

// ----------------------- MUSIC PAGE ---------------------------

void drawTransportButton(int cx, int cy, int kind, bool primary, bool selected) {
  uint16_t fill = primary ? C_WHITE : C_PANEL;
  uint16_t icon = primary ? C_BLACK : C_WHITE;

  if (selected) {
    frame.drawCircle(cx, cy, primary ? 23 : 20, C_GLOW);
    frame.drawCircle(cx, cy, primary ? 22 : 19, C_CYAN);
  }

  frame.fillCircle(cx, cy, primary ? 18 : 15, fill);
  frame.drawCircle(cx, cy, primary ? 18 : 15, primary ? C_CYAN : C_PANEL2);

  if (kind == 0) {
    frame.fillTriangle(cx + 7, cy - 9, cx + 7, cy + 9, cx - 1, cy, icon);
    frame.fillTriangle(cx - 1, cy - 9, cx - 1, cy + 9, cx - 9, cy, icon);
    frame.fillRoundRect(cx - 12, cy - 9, 3, 18, 1, icon);
  } else if (kind == 1) {
    if (songPlaying) {
      frame.fillRoundRect(cx - 6, cy - 9, 5, 18, 2, icon);
      frame.fillRoundRect(cx + 3, cy - 9, 5, 18, 2, icon);
    } else {
      frame.fillTriangle(cx - 5, cy - 10, cx - 5, cy + 10, cx + 9, cy, icon);
    }
  } else {
    frame.fillTriangle(cx - 7, cy - 9, cx - 7, cy + 9, cx + 1, cy, icon);
    frame.fillTriangle(cx + 1, cy - 9, cx + 1, cy + 9, cx + 9, cy, icon);
    frame.fillRoundRect(cx + 10, cy - 9, 3, 18, 1, icon);
  }
}

void drawMusicPage() {
  drawDeskooBackground();
  drawTopBar("MUSIC", 0);

  drawAlbumArt();

  String title = fitText(songTitle, 286, 2);
  String artist = fitText(songArtist, 246, 1);

  centerText(153, title, C_WHITE, 2);
  centerText(176, artist, C_DIM, 1);

  // Progress track
  frame.fillRoundRect(48, 191, 224, 8, 4, C_PANEL);
  frame.drawRoundRect(48, 191, 224, 8, 4, C_PANEL2);
  int pw = map(constrain(songProgress, 0, 100), 0, 100, 0, 224);
  if (pw > 0) {
    frame.fillRoundRect(48, 191, pw, 8, 4, C_CYAN);
    if (pw > 10) frame.drawFastHLine(52, 193, pw - 6, lerp565(C_CYAN, C_WHITE, 80));
  }
  // Knob with glow
  int kx = constrain(48 + pw, 55, 266);
  frame.drawCircle(kx, 195, 7, lerp565(C_CYAN, C_BG, 100));
  frame.fillCircle(kx, 195, 5, C_WHITE);
  frame.fillCircle(kx, 195, 2, C_CYAN);

  printText(48, 205, songPlaying ? "PLAYING" : "PAUSED", songPlaying ? C_CYAN : C_YELLOW, 1);
  String pct = String(songProgress) + "%";
  int16_t x1,y1; uint16_t tw,th;
  frame.setTextSize(1);
  frame.getTextBounds(pct, 0, 0, &x1, &y1, &tw, &th);
  printText(272 - tw, 205, pct, C_DIM, 1);

  drawTransportButton(116, 220, 0, false, musicControlSelection == 0);
  drawTransportButton(160, 220, 1, true, musicControlSelection == 1);
  drawTransportButton(204, 220, 2, false, musicControlSelection == 2);
}

// ----------------------- DECK PAGE ----------------------------

void drawSimpleIcon(int idx, int cx, int cy, uint16_t fg) {
  switch (idx) {
    case 0: // WhatsApp
      frame.fillCircle(cx, cy, 16, C_WHATSAPP);
      frame.fillTriangle(cx - 7, cy + 11, cx - 13, cy + 18, cx - 2, cy + 14, C_WHATSAPP);
      drawRing(cx, cy, 11, 2, C_WHITE);
      frame.fillTriangle(cx - 4, cy + 8, cx - 9, cy + 14, cx + 1, cy + 10, C_WHITE);
      drawThickLine(cx - 5, cy - 5, cx + 2, cy + 5, 2, C_WHITE);
      drawThickLine(cx + 2, cy + 5, cx + 8, cy + 1, 2, C_WHITE);
      frame.fillCircle(cx - 5, cy - 5, 2, C_WHATSAPP);
      frame.fillCircle(cx + 8, cy + 1, 2, C_WHATSAPP);
      break;
    case 1: // Zen Browser
      frame.fillRoundRect(cx - 17, cy - 17, 34, 34, 9, C_ZEN_BLACK);
      frame.drawRoundRect(cx - 17, cy - 17, 34, 34, 9, C_TILE_LINE);
      frame.fillRoundRect(cx - 10, cy - 9, 20, 6, 3, C_ZEN_MARK);
      frame.fillCircle(cx + 10, cy - 6, 3, C_ZEN_MARK);
      drawThickLine(cx + 8, cy - 5, cx - 8, cy + 9, 4, C_ZEN_MARK);
      frame.fillRoundRect(cx - 7, cy + 7, 18, 6, 3, C_ZEN_MARK);
      break;
    case 2: // Visual Studio Code
      frame.fillRoundRect(cx - 17, cy - 17, 34, 34, 8, C_VSCODE_D);
      frame.fillTriangle(cx - 14, cy, cx - 4, cy - 10, cx - 4, cy + 10, C_WHITE);
      frame.fillTriangle(cx - 4, cy - 10, cx + 14, cy - 16, cx + 14, cy + 16, C_VSCODE);
      frame.fillTriangle(cx - 4, cy + 10, cx + 14, cy + 16, cx + 14, cy - 16, C_BLUE);
      frame.drawLine(cx - 12, cy, cx - 5, cy - 7, C_VSCODE);
      frame.drawLine(cx - 12, cy, cx - 5, cy + 7, C_VSCODE);
      frame.drawLine(cx + 5, cy - 7, cx + 12, cy - 12, C_WHITE);
      frame.drawLine(cx + 5, cy + 7, cx + 12, cy + 12, C_WHITE);
      break;
    case 3: // Apple Music
      fillRoundGradientRect(cx - 17, cy - 17, 34, 34, 9, C_APPLE_TOP, C_APPLE_BOT);
      frame.fillCircle(cx - 8, cy + 9, 6, C_WHITE);
      frame.fillCircle(cx + 8, cy + 6, 6, C_WHITE);
      frame.fillRoundRect(cx - 3, cy - 6, 5, 16, 2, C_WHITE);
      frame.fillRoundRect(cx + 13, cy - 10, 5, 16, 2, C_WHITE);
      frame.fillTriangle(cx - 3, cy - 10, cx + 18, cy - 14, cx + 18, cy - 8, C_WHITE);
      frame.fillTriangle(cx - 3, cy - 10, cx - 3, cy - 4, cx + 18, cy - 8, C_WHITE);
      break;
    case 4: // previous track
      frame.fillCircle(cx, cy, 17, C_PANEL2);
      frame.fillTriangle(cx + 9, cy - 11, cx + 9, cy + 11, cx - 2, cy, C_WHITE);
      frame.fillTriangle(cx - 2, cy - 11, cx - 2, cy + 11, cx - 13, cy, C_WHITE);
      frame.fillRoundRect(cx - 16, cy - 11, 4, 22, 2, C_CYAN);
      break;
    case 5: // play/pause
      frame.fillCircle(cx, cy, 17, C_GREEN);
      frame.fillTriangle(cx - 7, cy - 10, cx - 7, cy + 10, cx + 6, cy, C_BLACK);
      frame.fillRoundRect(cx + 8, cy - 10, 4, 20, 2, C_BLACK);
      frame.fillRoundRect(cx + 15, cy - 10, 4, 20, 2, C_BLACK);
      break;
    case 6: // next track
      frame.fillCircle(cx, cy, 17, C_PANEL2);
      frame.fillTriangle(cx - 9, cy - 11, cx - 9, cy + 11, cx + 2, cy, C_WHITE);
      frame.fillTriangle(cx + 2, cy - 11, cx + 2, cy + 11, cx + 13, cy, C_WHITE);
      frame.fillRoundRect(cx + 13, cy - 11, 4, 22, 2, C_CYAN);
      break;
    case 7: // lock
      frame.fillRoundRect(cx - 15, cy - 1, 30, 18, 4, C_YELLOW);
      drawRing(cx, cy - 2, 10, 3, C_YELLOW);
      frame.fillRect(cx - 9, cy - 2, 18, 7, C_TILE_BG);
      frame.fillCircle(cx, cy + 7, 3, C_BLACK);
      frame.fillRoundRect(cx - 1, cy + 8, 3, 5, 1, C_BLACK);
      break;
    case 8: // power
      frame.fillCircle(cx, cy, 17, 0x3000);
      drawRing(cx, cy + 2, 13, 3, C_RED);
      frame.fillRect(cx - 5, cy - 16, 10, 13, 0x3000);
      drawThickLine(cx, cy - 14, cx, cy + 1, 2, C_RED);
      break;
  }
}

void drawDeckTile(int idx, int x, int y) {
  bool selected = idx == deckSelection;
  uint16_t bg = selected ? C_TILE_SEL : C_TILE_BG;

  if (selected) {
    // Multi-layer outer glow halo
    frame.drawRoundRect(x - 2, y - 2, 98, 54, 7, lerp565(C_CYAN, C_BLACK, 200));
    frame.drawRoundRect(x - 1, y - 1, 96, 52, 6, lerp565(C_CYAN, C_BLACK, 130));
    frame.fillRoundRect(x, y, 94, 50, 5, bg);
    frame.drawRoundRect(x, y, 94, 50, 5, C_CYAN);
    frame.drawRoundRect(x + 1, y + 1, 92, 48, 4, lerp565(C_CYAN, C_WHITE, 90));
  } else {
    frame.fillRoundRect(x, y, 94, 50, 5, bg);
    frame.drawRoundRect(x, y, 94, 50, 5, C_TILE_LINE);
  }

  printText(x + 6, y + 6, String(idx + 1), selected ? C_CYAN : C_DIM, 1);
  drawSimpleIcon(idx, x + 47, y + 18, C_WHITE);

  String name = deckLabels[idx];
  int16_t x1, y1; uint16_t tw, th;
  frame.setTextSize(1);
  frame.getTextBounds(name, 0, 0, &x1, &y1, &tw, &th);
  printText(x + (94 - tw) / 2, y + 38, name, selected ? C_WHITE : C_DIM, 1);
}

void drawDeckPage() {
  drawDeskooBackground();
  drawTopBar("STREAM DECK", 1);

  drawDeckTile(0, 8,   39);
  drawDeckTile(1, 113, 39);
  drawDeckTile(2, 218, 39);
  drawDeckTile(3, 8,   95);
  drawDeckTile(4, 113, 95);
  drawDeckTile(5, 218, 95);
  drawDeckTile(6, 8,   151);
  drawDeckTile(7, 113, 151);
  drawDeckTile(8, 218, 151);

  printText(9, 211, "Ctrl+Shift+1-9 / Enter", C_DIM, 1);
}

// ----------------------- RABBIT PAGE --------------------------

const char* rabbitMoodLabel() {
  if (rabbitMood == RABBIT_SLEEP) return "SLEEPING";
  if (rabbitMood == RABBIT_MUSIC) return "HEADPHONE MUSIC";
  return "CARROT SNACK";
}

void drawShamsherBackdrop() {
  int activePulse = (rabbitMood == RABBIT_MUSIC)
    ? (int)((sinf(rabbitPhase * 2.2f) + 1.0f) * 9.0f)
    : (int)((sinf(rabbitPhase * 0.8f) + 1.0f) * 3.0f);

  frame.fillCircle(160, 132, 82 + activePulse, C_MAROON);
  frame.fillCircle(160, 132, 67 + activePulse / 2, C_CORAL);
  frame.drawCircle(160, 132, 91 + activePulse, C_PANEL2);
  frame.drawCircle(160, 132, 105 + activePulse / 2, C_CRIMSON);

  for (int i = 0; i < 18; i++) {
    float a = rabbitPhase * ((rabbitMood == RABBIT_MUSIC) ? 1.35f : 0.32f) + i * 0.58f;
    int x = 160 + (int)(cosf(a) * (70 + (i % 4) * 18));
    int y = 134 + (int)(sinf(a) * (40 + (i % 3) * 12));
    uint16_t c = (i % 3 == 0) ? C_GLOW : ((i % 3 == 1) ? C_CYAN : C_RABBIT_SHADE);
    frame.fillCircle(x, y, (rabbitMood == RABBIT_MUSIC) ? 2 : 1, c);
  }

  if (rabbitMood == RABBIT_SLEEP) {
    // Crescent moon - bigger with soft glow halo
    frame.fillCircle(42, 62, 20, lerp565(C_YELLOW, C_WHITE, 40));
    frame.fillCircle(42, 62, 17, C_YELLOW);
    frame.fillCircle(54, 56, 17, lerp565(C_GRAD_TOP, C_BG, 30)); // bite = crescent
    // Stars near moon
    frame.fillCircle(72, 44, 2, C_WHITE);
    frame.fillCircle(26, 44, 2, C_DIM);
    frame.fillCircle(80, 66, 1, C_DIM);
    // Animated ZZZ
    printText(244, 54, "Z", C_WHITE, 2);
    printText(263, 38, "Z", C_DIM, 2);
    printText(279, 28, "z", C_DIM, 1);
  } else if (rabbitMood == RABBIT_EAT) {
    frame.fillCircle(42, 61, 8, C_CARROT);
    frame.fillTriangle(37, 52, 48, 52, 42, 43, C_LEAF);
    frame.fillCircle(286, 61, 7, C_LEAF);
    frame.fillRoundRect(278, 66, 22, 5, 3, C_CARROT);
  } else {
    for (int i = 0; i < 16; i++) {
      float s = sinf(rabbitPhase * 2.3f + i * 0.52f);
      int amp = 7 + (int)((s + 1.0f) * 12.0f);
      int x = 8 + i * 20;
      frame.fillRoundRect(x, 219 - amp, 8, amp, 4, (i % 2) ? C_CYAN : C_GLOW);
    }
  }
}

// Cheek blush + freckle dots, positioned relative to head center (hx, hy)
void drawRabbitSpeckles(int hx, int hy) {
  // Soft blush ovals on cheeks
  frame.fillEllipse(hx - 22, hy + 11, 10, 6, C_PINK);
  frame.fillEllipse(hx + 22, hy + 11, 10, 6, C_PINK);
  // Tiny freckle dots
  for (int i = 0; i < 4; i++) {
    frame.fillCircle(hx - 26 + i * 3, hy + 7 - (i % 2), 1, C_RABBIT_SHADE);
    frame.fillCircle(hx + 14 + i * 3, hy + 7 - (i % 2), 1, C_RABBIT_SHADE);
  }
}

void drawRabbitFace(int hx, int hy) {
  bool eyesClosed = rabbitMood == RABBIT_SLEEP || rabbitBlink > 0;
  bool eating     = rabbitMood == RABBIT_EAT;
  bool music      = rabbitMood == RABBIT_MUSIC;

  // --- Eyes ---
  if (eyesClosed) {
    // Soft closed-eye arcs
    for (int d = 0; d <= 1; d++) {
      frame.drawFastHLine(hx - 20, hy - 9 + d, 10, C_BLACK);
      frame.drawFastHLine(hx + 10, hy - 9 + d, 10, C_BLACK);
    }
    // Eyelashes
    frame.drawLine(hx - 20, hy - 9, hx - 23, hy - 13, C_BLACK);
    frame.drawLine(hx - 15, hy - 9, hx - 15, hy - 13, C_BLACK);
    frame.drawLine(hx + 10, hy - 9, hx + 7,  hy - 13, C_BLACK);
    frame.drawLine(hx + 19, hy - 9, hx + 22, hy - 13, C_BLACK);
  } else {
    // Big round eyes with shine and colored iris
    frame.fillCircle(hx - 13, hy - 8, 7, C_BLACK);
    frame.fillCircle(hx + 13, hy - 8, 7, C_BLACK);
    // Shine highlight
    frame.fillCircle(hx - 10, hy - 11, 2, C_WHITE);
    frame.fillCircle(hx + 16, hy - 11, 2, C_WHITE);
    // Colored iris
    uint16_t irisCol = music ? C_CYAN : (eating ? C_CARROT : 0x39C7);
    frame.fillCircle(hx - 13, hy - 8, 3, irisCol);
    frame.fillCircle(hx + 13, hy - 8, 3, irisCol);
    // Music sparkle cross above eyes
    if (music) {
      frame.drawLine(hx - 13, hy - 16, hx - 13, hy - 19, C_CYAN);
      frame.drawLine(hx + 13, hy - 16, hx + 13, hy - 19, C_CYAN);
      frame.drawLine(hx - 16, hy - 15, hx - 10, hy - 15, C_CYAN);
      frame.drawLine(hx + 10, hy - 15, hx + 16, hy - 15, C_CYAN);
    }
  }

  // --- Nose ---
  frame.fillEllipse(hx, hy + 5, 5, 4, C_PINK);
  frame.fillEllipse(hx, hy + 4, 3, 2, C_CORAL);

  // --- Mouth ---
  if (eating) {
    int chew = (int)(sinf(rabbitPhase * 3.5f) * 2.0f);
    frame.fillEllipse(hx, hy + 13 + chew, 7, 5, 0x8000);
    frame.fillEllipse(hx + 1, hy + 13 + chew, 5, 3, C_CARROT);
    frame.drawLine(hx - 7, hy + 9, hx - 1, hy + 12 + chew, C_BLACK);
    frame.drawLine(hx + 7, hy + 9, hx + 1, hy + 12 + chew, C_BLACK);
  } else {
    // Cute W-shaped mouth
    frame.drawLine(hx - 7, hy + 9, hx - 3, hy + 14, C_BLACK);
    frame.drawLine(hx - 3, hy + 14, hx + 3, hy + 11, C_BLACK);
    frame.drawLine(hx + 3, hy + 11, hx + 7, hy + 14, C_BLACK);
  }

  // --- Whiskers ---
  frame.drawLine(hx - 5, hy + 7, hx - 27, hy + 4, C_RABBIT_SHADE);
  frame.drawLine(hx - 5, hy + 9, hx - 27, hy + 11, C_RABBIT_SHADE);
  frame.drawLine(hx + 5, hy + 7, hx + 27, hy + 4, C_RABBIT_SHADE);
  frame.drawLine(hx + 5, hy + 9, hx + 27, hy + 11, C_RABBIT_SHADE);
}

// Vertical carrot held upright — x,y is top of carrot body
void drawCarrot(int x, int y) {
  // Body: tapered orange column
  for (int i = 0; i < 26; i++) {
    int w = 11 - (i * 10) / 26;
    if (w < 1) w = 1;
    frame.drawFastHLine(x - w / 2, y + i, w, C_CARROT);
  }
  // Highlight stripe
  frame.drawLine(x - 2, y + 1, x - 3, y + 20, C_YELLOW);
  frame.drawLine(x - 1, y + 1, x - 2, y + 21, lerp565(C_CARROT, C_YELLOW, 100));
  // Tip
  frame.fillCircle(x, y + 26, 2, lerp565(C_CARROT, C_RED, 100));
  // Root
  frame.drawLine(x, y + 27, x + 1, y + 31, C_RABBIT_SHADE);
  // Leafy tops — 3 leaves fanning upward
  frame.fillTriangle(x - 3, y,     x - 13, y - 20, x - 1, y - 8,  C_LEAF);
  frame.fillTriangle(x,     y,     x - 3,  y - 23, x + 8, y - 10, C_LEAF);
  frame.fillTriangle(x + 3, y,     x + 13, y - 18, x + 6, y - 7,  C_LEAF);
  // Leaf veins
  frame.drawLine(x - 9, y - 11, x - 2, y - 4, lerp565(C_LEAF, C_BLACK, 70));
  frame.drawLine(x,     y - 12, x + 3, y - 4,  lerp565(C_LEAF, C_BLACK, 70));
}

void drawRabbitCharacter(int cx, int cy) {
  int bob      = (rabbitMood == RABBIT_SLEEP)
                   ? (int)(sinf(rabbitPhase * 0.55f) * 1.5f)
                   : (int)(sinf(rabbitPhase) * 3.5f);
  int earShift = (int)(sinf(rabbitPhase * 0.75f) * 4.0f);
  cy += bob;

  // Centered upright rabbit: head directly above body, facing forward
  int hx = cx;        // head center x
  int hy = cy - 22;   // head center y
  int bx = cx;        // body center x
  int by = cy + 32;   // body center y

  // ---- Ground shadow ----
  frame.fillEllipse(bx, by + 44, 52, 8, lerp565(C_PANEL, C_BG, 80));

  // ---- Ears drawn BEFORE head so head overlaps the bases ----
  if (rabbitMood == RABBIT_SLEEP) {
    // Both ears drooping outward — sleepy
    drawThickLine(hx - 14, hy - 26, hx - 62, hy - 12, 11, C_WHITE);
    drawThickLine(hx - 13, hy - 25, hx - 60, hy - 10, 5,  C_PINK);
    drawThickLine(hx + 14, hy - 26, hx + 62, hy - 12, 11, C_WHITE);
    drawThickLine(hx + 13, hy - 25, hx + 60, hy - 10, 5,  C_PINK);
  } else {
    // Both ears upright and gently swaying
    drawThickLine(hx - 14, hy - 26, hx - 16 + earShift, hy - 90, 11, C_WHITE);
    drawThickLine(hx - 13, hy - 25, hx - 14 + earShift, hy - 85, 5,  C_PINK);
    drawThickLine(hx + 14, hy - 26, hx + 16 - earShift, hy - 88, 11, C_WHITE);
    drawThickLine(hx + 13, hy - 25, hx + 14 - earShift, hy - 83, 5,  C_PINK);
  }

  // ---- Body ----
  frame.fillEllipse(bx, by, 44, 36, C_WHITE);
  frame.drawEllipse(bx, by, 44, 36, C_RABBIT_EDGE);
  // Soft belly tuft
  frame.fillEllipse(bx, by + 6, 24, 19, lerp565(C_WHITE, 0xFFFF, 160));

  // ---- Left arm + paw ----
  frame.fillEllipse(bx - 42, by - 2, 16, 11, C_WHITE);
  frame.drawEllipse(bx - 42, by - 2, 16, 11, C_RABBIT_EDGE);
  frame.fillCircle(bx - 54, by + 2, 8, C_WHITE);
  frame.drawCircle(bx - 54, by + 2, 8, C_RABBIT_EDGE);
  frame.fillCircle(bx - 51, by + 7, 3, C_RABBIT_SHADE);
  frame.fillCircle(bx - 57, by + 7, 3, C_RABBIT_SHADE);

  // ---- Right arm + paw (raised when eating) ----
  if (rabbitMood == RABBIT_EAT) {
    int paw = (int)(sinf(rabbitPhase * 2.0f) * 3.0f);
    // Draw carrot first (behind raised arm)
    drawCarrot(bx + 52, by - 68 + paw);
    frame.fillEllipse(bx + 44, by - 16 + paw, 16, 11, C_WHITE);
    frame.drawEllipse(bx + 44, by - 16 + paw, 16, 11, C_RABBIT_EDGE);
    frame.fillCircle(bx + 56, by - 26 + paw, 8, C_WHITE);
    frame.drawCircle(bx + 56, by - 26 + paw, 8, C_RABBIT_EDGE);
  } else {
    frame.fillEllipse(bx + 42, by - 2, 16, 11, C_WHITE);
    frame.drawEllipse(bx + 42, by - 2, 16, 11, C_RABBIT_EDGE);
    frame.fillCircle(bx + 54, by + 2, 8, C_WHITE);
    frame.drawCircle(bx + 54, by + 2, 8, C_RABBIT_EDGE);
    frame.fillCircle(bx + 51, by + 7, 3, C_RABBIT_SHADE);
    frame.fillCircle(bx + 57, by + 7, 3, C_RABBIT_SHADE);
  }

  // ---- Feet ----
  frame.fillEllipse(bx - 24, by + 40, 22, 13, C_WHITE);
  frame.drawEllipse(bx - 24, by + 40, 22, 13, C_RABBIT_EDGE);
  frame.fillCircle(bx - 30, by + 47, 3, C_RABBIT_SHADE);
  frame.fillCircle(bx - 22, by + 49, 3, C_RABBIT_SHADE);
  frame.fillCircle(bx - 14, by + 47, 3, C_RABBIT_SHADE);

  frame.fillEllipse(bx + 24, by + 40, 22, 13, C_WHITE);
  frame.drawEllipse(bx + 24, by + 40, 22, 13, C_RABBIT_EDGE);
  frame.fillCircle(bx + 18, by + 47, 3, C_RABBIT_SHADE);
  frame.fillCircle(bx + 26, by + 49, 3, C_RABBIT_SHADE);
  frame.fillCircle(bx + 34, by + 47, 3, C_RABBIT_SHADE);

  // ---- Head ----
  frame.fillEllipse(hx, hy, 40, 34, C_WHITE);
  frame.drawEllipse(hx, hy, 40, 34, C_RABBIT_EDGE);
  // Subtle top-left highlight on head
  frame.drawEllipse(hx - 5, hy - 8, 20, 12, lerp565(C_WHITE, 0xFFFF, 160));

  drawRabbitSpeckles(hx, hy);
  drawRabbitFace(hx, hy);

  // ---- Headphones (RABBIT_MUSIC only) ----
  if (rabbitMood == RABBIT_MUSIC) {
    uint8_t gPhase = (uint8_t)((sinf(rabbitPhase * 2.0f) + 1.0f) * 127.5f);
    uint16_t glowCol = lerp565(C_CYAN, C_GLOW, gPhase);

    // Thick headband arc over the head (elliptical: rx=46, ry=40)
    for (int a = 20; a <= 160; a += 5) {
      float rad = (float)a * 3.14159f / 180.0f;
      int px = hx + (int)(cosf(rad) * 46);
      int py = hy - (int)(sinf(rad) * 40);
      frame.fillCircle(px, py, 6, C_DIM);
    }
    // Inner band highlight
    for (int a = 35; a <= 145; a += 8) {
      float rad = (float)a * 3.14159f / 180.0f;
      int px = hx + (int)(cosf(rad) * 46);
      int py = hy - (int)(sinf(rad) * 40);
      frame.fillCircle(px, py, 2, C_PANEL2);
    }

    // Left ear cup — shell, foam, grille, glow ring
    int lcx = hx - 47, lcy = hy + 2;
    frame.fillCircle(lcx, lcy, 16, C_DIM);
    frame.fillCircle(lcx, lcy, 12, 0x2104);
    frame.fillCircle(lcx, lcy, 8,  C_PANEL);
    frame.drawCircle(lcx, lcy, 16, glowCol);
    frame.drawCircle(lcx, lcy, 17, lerp565(glowCol, C_BLACK, 180));
    for (int gx = -1; gx <= 1; gx++)
      for (int gy = -1; gy <= 1; gy++)
        frame.fillCircle(lcx + gx * 4, lcy + gy * 4, 1, lerp565(C_DIM, glowCol, 60));

    // Right ear cup
    int rcx = hx + 47, rcy = hy + 2;
    frame.fillCircle(rcx, rcy, 16, C_DIM);
    frame.fillCircle(rcx, rcy, 12, 0x2104);
    frame.fillCircle(rcx, rcy, 8,  C_PANEL);
    frame.drawCircle(rcx, rcy, 16, glowCol);
    frame.drawCircle(rcx, rcy, 17, lerp565(glowCol, C_BLACK, 180));
    for (int gx = -1; gx <= 1; gx++)
      for (int gy = -1; gy <= 1; gy++)
        frame.fillCircle(rcx + gx * 4, rcy + gy * 4, 1, lerp565(C_DIM, glowCol, 60));
  }
}

void drawRabbitPage() {
  drawDeskooBackground(rabbitMood == RABBIT_MUSIC ? 24 : 10);
  drawTopBar("SHAMSHER", 2);

  drawShamsherBackdrop();

  centerText(38, rabbitMoodLabel(), rabbitMood == RABBIT_MUSIC ? C_GLOW : C_DIM, 1);

  drawRabbitCharacter(158, 126);

  // Smooth deterministic visualizer at bottom (shifted down to clear rabbit feet)
  if (rabbitMood == RABBIT_MUSIC) {
    for (int i = 0; i < 12; i++) {
      float s = sinf(rabbitPhase * 1.7f + i * 0.63f);
      float t = sinf(rabbitPhase * 0.9f + i * 1.13f);
      int amp = 14 + (int)((s + t + 2.0f) * 6.0f);
      amp = constrain(amp, 4, 36);
      int x = 11 + i * 26;
      uint16_t c = (i % 2) ? C_CRIMSON : C_CYAN;
      frame.fillRoundRect(x, 224 - amp, 9, amp, 4, c);
    }
  } else {
    for (int i = 0; i < 11; i++) {
      int x = 20 + i * 27;
      int y = 222 - (i % 4) * 2;
      frame.fillRoundRect(x, y, 10, 3, 2, C_PANEL2);
    }
  }
}

// ---------------------- RENDER -------------------------------

void renderCurrentPage() {
  if (currentPage == PAGE_MUSIC) drawMusicPage();
  else if (currentPage == PAGE_DECK) drawDeckPage();
  else drawRabbitPage();
  pushFrame();
}

// Fade-to-black -> switch -> fade-in effect.
// We keep it short to avoid SPI-heavy multi-page sliding.
void transitionTo(Page p) {
  if (p == currentPage) return;

  // 4-frame fade out from current rendered frame
  uint16_t *buf = frame.getBuffer();
  for (int step = 0; step < 4; step++) {
    uint8_t t = 70; // darken in-place
    for (int i = 0; i < W*H; i++) buf[i] = lerp565(buf[i], C_BLACK, t);
    pushFrame();
    delay(12);
  }

  currentPage = p;
  renderCurrentPage();
}

void nextPage() {
  transitionTo((Page)(((int)currentPage + 1) % 3));
}

void prevPage() {
  transitionTo((Page)(((int)currentPage + 2) % 3));
}

// --------------------- SERIAL PROTOCOL ------------------------

void sendAction(const char *name) {
  Serial.print("ACTION:");
  Serial.println(name);
}

void runDeckItem(int idx) {
  if (idx < 0 || idx >= DECK_COUNT) return;

  deckSelection = idx;
  if (currentPage != PAGE_DECK) {
    transitionTo(PAGE_DECK);
  } else {
    renderCurrentPage();
  }
  sendAction(deckNames[deckSelection]);
}

void runMusicControl(int idx) {
  if (idx < 0 || idx > 2) return;

  musicControlSelection = idx;
  if (currentPage != PAGE_MUSIC) {
    transitionTo(PAGE_MUSIC);
  } else {
    renderCurrentPage();
  }

  if (idx == 0) sendAction("Previous Track");
  else if (idx == 1) sendAction("Play/Pause");
  else sendAction("Next Track");
}

void setRabbitMood(RabbitMood mood) {
  rabbitMood = mood;
  if (currentPage != PAGE_RABBIT) {
    transitionTo(PAGE_RABBIT);
  } else {
    renderCurrentPage();
  }
}

void processRabbitMoodCommand(String value) {
  value.trim();
  value.toUpperCase();

  if (value == "NEXT" || value == "CYCLE" || value == "TOGGLE") {
    setRabbitMood((RabbitMood)(((uint8_t)rabbitMood + 1) % 3));
  } else if (value == "SLEEP" || value == "SLEEPING" || value == "0") {
    setRabbitMood(RABBIT_SLEEP);
  } else if (value == "MUSIC" || value == "HEADPHONES" || value == "HEADPHONE" || value == "1") {
    setRabbitMood(RABBIT_MUSIC);
  } else if (value == "EAT" || value == "EATING" || value == "CARROT" || value == "2") {
    setRabbitMood(RABBIT_EAT);
  }
}

void processCommand(String cmd) {
  cmd.trim();
  if (!cmd.length()) return;

  if (cmd == "PAGE:1") transitionTo(PAGE_MUSIC);
  else if (cmd == "PAGE:2") transitionTo(PAGE_DECK);
  else if (cmd == "PAGE:3") transitionTo(PAGE_RABBIT);
  else if (cmd == "ROUTE:MUSIC") transitionTo(PAGE_MUSIC);
  else if (cmd == "ROUTE:DECK") transitionTo(PAGE_DECK);
  else if (cmd == "ROUTE:RABBIT") transitionTo(PAGE_RABBIT);
  else if (cmd == "NEXT") nextPage();
  else if (cmd == "PREV") prevPage();

  else if (cmd == "SELECT:NEXT") {
    deckSelection = (deckSelection + 1) % DECK_COUNT;
    if (currentPage == PAGE_DECK) renderCurrentPage();
  }
  else if (cmd == "SELECT:PREV") {
    deckSelection = (deckSelection + DECK_COUNT - 1) % DECK_COUNT;
    if (currentPage == PAGE_DECK) renderCurrentPage();
  }
  else if (cmd.startsWith("SELECT:")) {
    int n = cmd.substring(7).toInt();
    if (n >= 0 && n < DECK_COUNT) {
      deckSelection = n;
      if (currentPage == PAGE_DECK) renderCurrentPage();
    }
  }

  else if (cmd == "RUN") {
    if (currentPage == PAGE_MUSIC) runMusicControl(musicControlSelection);
    else sendAction(deckNames[deckSelection]);
  }
  else if (cmd.startsWith("RUN:")) {
    runDeckItem(cmd.substring(4).toInt());
  }
  else if (cmd.startsWith("MUSICRUN:")) {
    runMusicControl(cmd.substring(9).toInt());
  }
  else if (cmd.startsWith("RABBITMOOD:")) {
    processRabbitMoodCommand(cmd.substring(11));
  }
  else if (cmd.startsWith("SHAMSHER:")) {
    processRabbitMoodCommand(cmd.substring(9));
  }

  // MUSIC:title|artist|progress|playing
  // Example: MUSIC:Starboy|The Weeknd|73|1
  else if (cmd.startsWith("MUSIC:")) {
    String d = cmd.substring(6);
    int p1 = d.indexOf('|');
    int p2 = d.indexOf('|', p1 + 1);
    int p3 = d.indexOf('|', p2 + 1);

    if (p1 > 0 && p2 > p1 && p3 > p2) {
      songTitle = d.substring(0, p1);
      songArtist = d.substring(p1 + 1, p2);
      songProgress = constrain(d.substring(p2 + 1, p3).toInt(), 0, 100);
      songPlaying = d.substring(p3 + 1).toInt() != 0;
      if (currentPage == PAGE_MUSIC || currentPage == PAGE_RABBIT) renderCurrentPage();
    }
  }

  // Album art protocol:
  // COVER:RESET
  // COVER:CHUNK:<pixel-offset>:<RGB565-hex-pixels>
  // COVER:DONE
  else if (cmd == "COVER:RESET") {
    coverReady = false;
    coverReceiving = true;
    coverWriteCount = 0;
  }
  else if (cmd.startsWith("COVER:CHUNK:")) {
    int first = cmd.indexOf(':', 12);
    if (first > 12) {
      int offset = cmd.substring(12, first).toInt();
      String data = cmd.substring(first + 1);
      int count = data.length() / 4;

      if (offset >= 0 && offset + count <= COVER_SIZE * COVER_SIZE) {
        for (int i = 0; i < count; i++) {
          coverPixels[offset + i] = parseHex565(data, i * 4);
        }
        if (offset + count > coverWriteCount) coverWriteCount = offset + count;
      }
    }
  }
  else if (cmd == "COVER:DONE") {
    coverReady = coverWriteCount >= COVER_SIZE * COVER_SIZE;
    coverReceiving = false;
    if (currentPage == PAGE_MUSIC) renderCurrentPage();
  }

  else if (cmd == "PING") {
    Serial.println("PONG:ESP32-DESKTOP-COMPANION");
  }
}

void readSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      processCommand(rxLine);
      rxLine = "";
    } else if (c != '\r') {
      if (rxLine.length() < 512) rxLine += c;
    }
  }
}

// -------------------------- SETUP -----------------------------

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);

  if (!lcd->begin()) {
    while (true) delay(1000);
  }

  drawDeskooBackground();
  frame.fillCircle(160, 113, 58, C_MAROON);
  frame.drawCircle(160, 113, 70, C_PANEL2);
  centerText(96, "DESKOO", C_WHITE, 3);
  centerText(135, "USB READY", C_GREEN, 1);
  pushFrame();
  delay(700);

  renderCurrentPage();

  Serial.println("READY:ESP32-DESKTOP-COMPANION");
  nextBlinkMs = millis() + 1800;
}

// --------------------------- LOOP -----------------------------

void loop() {
  readSerial();

  const uint32_t now = millis();

  // 20 FPS animation only on animated pages.
  if (now - lastFrameMs >= 50) {
    lastFrameMs = now;

    if (currentPage == PAGE_RABBIT) {
      if (rabbitMood == RABBIT_MUSIC) rabbitPhase += 0.13f;
      else if (rabbitMood == RABBIT_EAT) rabbitPhase += 0.08f;
      else rabbitPhase += 0.04f;

      if (rabbitBlink > 0) rabbitBlink--;
      if (now >= nextBlinkMs && rabbitBlink == 0) {
        rabbitBlink = 3; // ~150 ms at 20 FPS
        nextBlinkMs = now + 1600 + random(0, 2400);
      }

      drawRabbitPage();
      pushFrame();
    }
    // Deck is static, so no pointless full-screen refresh.
  }

  if (now - lastHeartbeatMs >= 5000) {
    lastHeartbeatMs = now;
    Serial.println("ALIVE");
  }
}
