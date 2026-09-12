
#include <Arduino_GFX_Library.h>
#include <Adafruit_GFX.h>
#include "boot_screen.h"
#include "rabbit_sprites.h"
#include "app_icons.h"

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
  "WhatsApp", "Zen Browser", "Brave",
  "Apple Music", "VS Code", "Antigravity",
  "File Explorer", "Notepad", "Lock PC"
};
const char* deckLabels[DECK_COUNT] = {
  "WhatsApp", "Zen Browser", "Brave",
  "Apple Music", "VS Code", "Antigravity",
  "Explorer", "Notepad", "Lock PC"
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
float uiPhase = 0.0f;
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
  uint8_t pulse = songPlaying ? (uint8_t)((sinf(uiPhase * 1.5f) + 1.0f) * 40.0f) : 0;
  drawDeskooBackground(pulse);
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
  const uint16_t* icon_data = nullptr;
  switch (idx) {
    case 0: icon_data = ICON_WHATSAPP; break;
    case 1: icon_data = ICON_ZEN; break;
    case 2: icon_data = ICON_BRAVE; break;
    case 3: icon_data = ICON_MUSIC; break;
    case 4: icon_data = ICON_VSCODE; break;
    case 5: icon_data = ICON_ANTIGRAVITY; break;
    case 6: icon_data = ICON_FILE_EXPLORER; break;
    case 7: icon_data = ICON_NOTEPAD; break;
    case 8: icon_data = ICON_LOCK; break;
  }
  if (icon_data) {
    int startX = cx - APP_ICON_SIZE / 2;
    int startY = cy - APP_ICON_SIZE / 2;
    for (int y = 0; y < APP_ICON_SIZE; y++) {
      for (int x = 0; x < APP_ICON_SIZE; x++) {
        uint16_t color = pgm_read_word(&icon_data[y * APP_ICON_SIZE + x]);
        if (color != 0x0000) {
          frame.drawPixel(startX + x, startY + y, color);
        }
      }
    }
  }
}

void drawDeckTile(int idx, int x, int y) {
  bool selected = idx == deckSelection;
  uint16_t bg = selected ? C_TILE_SEL : C_TILE_BG;
  int tw = 98;
  int th = 62;

  if (selected) {
    // Breathing outer glow halo
    uint8_t breath = (uint8_t)((sinf(uiPhase * 2.0f) + 1.0f) * 60.0f); // 0 to 120
    frame.drawRoundRect(x - 2, y - 2, tw + 4, th + 4, 7, lerp565(C_CYAN, C_BLACK, 135 + breath));
    frame.drawRoundRect(x - 1, y - 1, tw + 2, th + 2, 6, lerp565(C_CYAN, C_BLACK, 70 + breath));
    frame.fillRoundRect(x, y, tw, th, 5, bg);
    frame.drawRoundRect(x, y, tw, th, 5, C_CYAN);
    frame.drawRoundRect(x + 1, y + 1, tw - 2, th - 2, 4, lerp565(C_CYAN, C_WHITE, 90));
  } else {
    frame.fillRoundRect(x, y, tw, th, 5, bg);
    frame.drawRoundRect(x, y, tw, th, 5, C_TILE_LINE);
  }

  printText(x + 6, y + 6, String(idx + 1), selected ? C_CYAN : C_DIM, 1);
  drawSimpleIcon(idx, x + tw / 2, y + 24, C_WHITE);

  String name = deckLabels[idx];
  int16_t x1, y1; uint16_t bounds_tw, bounds_th;
  frame.setTextSize(1);
  frame.getTextBounds(name, 0, 0, &x1, &y1, &bounds_tw, &bounds_th);
  printText(x + (tw - bounds_tw) / 2, y + 50, name, selected ? C_WHITE : C_DIM, 1);
}

void drawDeckPage() {
  drawDeskooBackground();
  drawTopBar("STREAM DECK", 1);

  drawDeckTile(0, 6,   35);
  drawDeckTile(1, 111, 35);
  drawDeckTile(2, 216, 35);
  drawDeckTile(3, 6,   103);
  drawDeckTile(4, 111, 103);
  drawDeckTile(5, 216, 103);
  drawDeckTile(6, 6,   171);
  drawDeckTile(7, 111, 171);
  drawDeckTile(8, 216, 171);
}

// ----------------------- RABBIT PAGE --------------------------

const char* rabbitMoodLabel() {
  if (rabbitMood == RABBIT_SLEEP) return "SLEEPING";
  if (rabbitMood == RABBIT_MUSIC) return "HEADPHONE MUSIC";
  return "CARROT SNACK";
}

// Mood overlays drawn on top of the sprite
void drawShamserMoodOverlay() {
  if (rabbitMood == RABBIT_SLEEP) {
    // Crescent moon - geometrically drawn to prevent background mismatch
    int cx = 36, cy = 70, cr = 18, inner_r = 15;
    int bx = 47, by = 64, br = 17; // Bite coordinates and size
    for (int y = cy - cr; y <= cy + cr; y++) {
      for (int x = cx - cr; x <= cx + cr; x++) {
        int distSq = (x - cx) * (x - cx) + (y - cy) * (y - cy);
        if (distSq <= cr * cr) {
          // Only draw if outside the 'bite' circle
          if ((x - bx) * (x - bx) + (y - by) * (y - by) > br * br) {
             if (distSq <= inner_r * inner_r) {
                frame.drawPixel(x, y, C_YELLOW);
             } else {
                frame.drawPixel(x, y, lerp565(C_YELLOW, C_WHITE, 50));
             }
          }
        }
      }
    }
    // Stars
    frame.fillCircle(68, 50, 2, C_WHITE);
    frame.fillCircle(20, 52, 2, C_DIM);
    frame.fillCircle(76, 74, 1, C_WHITE);
    frame.fillCircle(10, 80, 1, C_DIM);
    
    // Drifting ZZZ
    int zOff = (int)(uiPhase * 4.0f) % 30; // Increased distance
    uint8_t fade1 = (zOff < 24) ? 255 - (zOff * 10) : 0;
    uint8_t fade2 = (zOff < 16) ? 255 - (zOff * 15) : 0;
    uint8_t fade3 = (zOff < 10) ? 255 - (zOff * 25) : 0;
    
    if (fade1 > 0) printText(248, 130 - zOff,      "Z", lerp565(C_BG, C_WHITE, fade1), 2);
    if (fade2 > 0) printText(264, 114 - zOff - 8,  "Z", lerp565(C_BG, C_WHITE, fade2), 2);
    if (fade3 > 0) printText(276, 102 - zOff - 14, "z", lerp565(C_BG, C_WHITE, fade3), 1);

  } else if (rabbitMood == RABBIT_EAT) {
    // Carrot crumbs falling with pseudo-gravity
    const int crumbX[6] = { 178, 190, 200, 172, 210, 184 };
    const int crumbCol[6] = { 0, 1, 2, 0, 2, 1 }; 
    for (int i = 0; i < 6; i++) {
      float t = fmodf(uiPhase * 2.5f + i * 0.9f, 4.0f);
      if (t < 2.0f) { // Active fall duration
        float fallDist = t * t * 15.0f; // Gravity acceleration
        int cy2 = 120 + (int)fallDist;
        int cx2 = crumbX[i] + (int)(sinf(t * 5.0f) * 3.0f); // slight tumble
        
        int size = 4;
        if (t > 1.2f) size = 3;
        if (t > 1.5f) size = 2;
        
        if (size > 0 && cy2 >= 0 && cy2 < H) {
          uint16_t cc = (crumbCol[i] == 0) ? C_CARROT : (crumbCol[i] == 1) ? C_YELLOW : C_ORANGE;
          frame.fillRect(cx2, cy2, size, size, cc);
        }
      }
    }

  } else if (rabbitMood == RABBIT_MUSIC) {
    const int noteX[3] = { 260, 40, 220 };
    for (int i = 0; i < 3; i++) {
      float phase = fmodf(uiPhase * 1.5f + i * 2.1f, 3.14159f * 2.0f);
      int ny = 160 - (int)(phase * 18.0f); 
      if (ny > 40 && ny < 200) {
        int nx = noteX[i] + (int)(sinf(phase * 4.0f) * 12.0f); 
        
        uint16_t baseColor = (i == 0) ? C_CYAN : (i == 1) ? C_PINK : C_YELLOW;
        uint8_t alpha = (ny < 80) ? (ny - 40) * 6 : 255; // Fade out near top
        uint16_t color = lerp565(C_BG, baseColor, alpha);
        
        frame.fillCircle(nx, ny, 4, color);
        frame.fillCircle(nx + 8, ny - 2, 4, color);
        frame.fillRect(nx + 2, ny - 12, 2, 12, color);
        frame.fillRect(nx + 10, ny - 14, 2, 12, color);
        frame.fillRect(nx + 2, ny - 14, 10, 4, color);
      }
    }
  }
}

// Draw full-bleed sprite with per-pixel brightness animation
void drawRabbitSprite() {
  const int SPRITE_Y = 29;

  if (rabbitMood == RABBIT_SLEEP) {
    float breath = (sinf(uiPhase * 0.55f) + 1.0f) * 0.5f; 
    uint8_t brightAmt = (breath > 0.5f) ? (uint8_t)((breath - 0.5f) * 2.0f * 30.0f) : 0;
    uint8_t darkAmt   = (breath < 0.5f) ? (uint8_t)((0.5f - breath) * 2.0f * 20.0f) : 0;
    for (int dy = 0; dy < RABBIT_SPRITE_H; dy++) {
      int canvasY = SPRITE_Y + dy;
      if (canvasY < 0 || canvasY >= H) continue;
      for (int dx = 0; dx < RABBIT_SPRITE_W; dx++) {
        uint16_t px = pgm_read_word(&RABBIT_SLEEP_BMP[dy * RABBIT_SPRITE_W + dx]);
        if (brightAmt > 0) px = lerp565(px, C_WHITE, brightAmt);
        if (darkAmt   > 0) px = lerp565(px, C_BLACK, darkAmt);
        frame.drawPixel(dx, canvasY, px);
      }
    }
  } else if (rabbitMood == RABBIT_MUSIC) {
    for (int dy = 0; dy < RABBIT_SPRITE_H; dy++) {
      int canvasY = SPRITE_Y + dy;
      if (canvasY < 0 || canvasY >= H) continue;
      for (int dx = 0; dx < RABBIT_SPRITE_W; dx++) {
        frame.drawPixel(dx, canvasY, pgm_read_word(&RABBIT_MUSIC_BMP[dy * RABBIT_SPRITE_W + dx]));
      }
    }
  } else { // RABBIT_EAT
    for (int dy = 0; dy < RABBIT_SPRITE_H; dy++) {
      int canvasY = SPRITE_Y + dy;
      if (canvasY < 0 || canvasY >= H) continue;
      for (int dx = 0; dx < RABBIT_SPRITE_W; dx++) {
        frame.drawPixel(dx, canvasY, pgm_read_word(&RABBIT_EAT_BMP[dy * RABBIT_SPRITE_W + dx]));
      }
    }
  }

}

void drawRabbitPage() {
  drawTopBar("SHAMSHER", 2);
  drawRabbitSprite();
  // Semi-transparent dark pill behind mood label
  frame.fillRoundRect(90, 31, 140, 14, 4, lerp565(C_BLACK, C_BG, 60));
  centerText(33, rabbitMoodLabel(),
             (rabbitMood == RABBIT_MUSIC) ? C_CYAN :
             (rabbitMood == RABBIT_SLEEP) ? C_YELLOW : C_WHITE, 1);
  drawShamserMoodOverlay();
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

  // 3-frame rapid fade out for snappy feel
  uint16_t *buf = frame.getBuffer();
  for (int step = 0; step < 3; step++) {
    uint8_t t = 80; // darken aggressively
    for (int i = 0; i < W*H; i++) buf[i] = lerp565(buf[i], C_BLACK, t);
    pushFrame();
    // No extra delay needed, pushFrame itself acts as a small delay
  }

  currentPage = p;
  renderCurrentPage();
}

uint32_t lastPageChangeMs = 0;

void nextPage() {
  if (millis() - lastPageChangeMs < 300) return;
  transitionTo((Page)(((int)currentPage + 1) % 3));
  lastPageChangeMs = millis(); // Reset debounce AFTER transition completes
}

void prevPage() {
  if (millis() - lastPageChangeMs < 300) return;
  transitionTo((Page)(((int)currentPage + 2) % 3));
  lastPageChangeMs = millis(); // Reset debounce AFTER transition completes
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
  Serial.begin(1000000);
  delay(500);

  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);

  if (!lcd->begin()) {
    while (true) delay(1000);
  }

  // --- Boot screen: full-screen image ---
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      frame.drawPixel(x, y, pgm_read_word(&BOOT_SCREEN_BMP[y * W + x]));
    }
  }
  // Phase 1 & 2: Smooth carrot loader
  for (int i = 0; i <= 100; i++) {
    // Redraw the bottom portion to clear previous frame
    for (int y = 190; y < H; y++) {
      for (int x = 0; x < W; x++) {
        frame.drawPixel(x, y, pgm_read_word(&BOOT_SCREEN_BMP[y * W + x]));
      }
    }
    
    // Sleek progress bar (orange track and fill)
    int pw = (i * 200) / 100;
    int cy = 208; 
    
    // Track background
    frame.drawRoundRect(60, cy - 2, 200, 4, 2, lerp565(C_BLACK, C_CARROT, 70));
    // Filled loader
    if (pw > 0) frame.fillRoundRect(60, cy - 2, pw, 4, 2, C_CARROT);
    
    // Draw carrot attached to the moving end
    int cx = 60 + pw;
    // Carrot body pointing DOWN
    frame.fillCircle(cx, cy - 4, 4, C_CARROT); // Thicker top
    frame.fillTriangle(cx - 4, cy - 4, cx + 4, cy - 4, cx, cy + 7, C_CARROT); // Longer pointy bottom
    // Carrot leaves sticking UP
    frame.drawLine(cx, cy - 4, cx - 5, cy - 11, C_LEAF);
    frame.drawLine(cx, cy - 4, cx + 5, cy - 11, C_LEAF);
    frame.drawLine(cx, cy - 5, cx, cy - 13, C_LEAF);
    
    // Fade text smoothly
    if (i < 50) {
      uint8_t alpha = (i * 255) / 50;
      centerText(224, "SYSTEM INITIALIZING...", lerp565(C_DIM, C_YELLOW, alpha), 1);
    } else {
      uint8_t alpha = ((i - 50) * 255) / 50;
      centerText(224, "USB CONNECTION READY", lerp565(C_DIM, C_GREEN, alpha), 1);
    }
    pushFrame();
    delay(25); // ~2.5 seconds total
  }
  delay(100);

  renderCurrentPage();

  Serial.println("READY:ESP32-DESKTOP-COMPANION");
  nextBlinkMs = millis() + 1800;
}

// --------------------------- LOOP -----------------------------

void loop() {
  readSerial();

  const uint32_t now = millis();

  // 20 FPS animation on all pages for fluid UI.
  if (now - lastFrameMs >= 50) {
    lastFrameMs = now;

    if (currentPage == PAGE_RABBIT) {
      if (rabbitMood == RABBIT_MUSIC) uiPhase += 0.13f;
      else if (rabbitMood == RABBIT_EAT) uiPhase += 0.08f;
      else uiPhase += 0.04f;
    } else {
      uiPhase += 0.05f; // Global animation phase
    }

    if (rabbitBlink > 0) rabbitBlink--;
    if (now >= nextBlinkMs && rabbitBlink == 0) {
      rabbitBlink = 3; // ~150 ms at 20 FPS
      nextBlinkMs = now + 1600 + random(0, 2400);
    }

    renderCurrentPage(); // Render all pages to keep animations flowing
  }

  if (now - lastHeartbeatMs >= 5000) {
    lastHeartbeatMs = now;
    Serial.println("ALIVE");
  }
}
