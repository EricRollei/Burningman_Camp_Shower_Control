/*
 * Echo-Reactive  --  AtomS3 + Atomic Echo Base port
 * Board : M5Stack AtomS3 (ESP32-S3).  In Arduino IDE: "M5AtomS3" (or "ESP32S3 Dev Module"),
 *         USB CDC On Boot: Enabled.
 * Libs  : M5Unified (pulls M5GFX), M5EchoBase (M5Atomic-EchoBase), Adafruit NeoPixel, arduinoFFT
 *
 * Ported from the original M5Atom + raw-I2S-mic version:
 *   - Mic now comes from the Atomic Echo Base (ES8311 codec) via M5EchoBase.
 *   - The 120-LED WS2812B strip is still the visualizer, same 4 modes/logic,
 *     but its DATA pin moves to G2 (Grove) because the Echo Base uses 5/6/7/8/38/39.
 *   - The AtomS3 128x128 LCD shows the current display mode + a small VU bar.
 *   - Button cycles modes: on the original Atom it was M5.Btn; here it's M5.BtnA.
 *
 * Strip power unchanged from the original build: external 5V (120 LEDs can pull
 * ~7A), common ground with the AtomS3, and a 3.3V->5V level shifter on the data
 * line is recommended.
 */

#include <M5Unified.h>
#include <M5EchoBase.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <arduinoFFT.h>

// ---------- LED strip (same logic as original; new data pin) ----------
#define LED_PIN     2      // WS2812 data via Grove G2 (avoid Echo Base's 5/6/7/8/38/39)
#define NUM_LEDS    120
#define BRIGHTNESS  150
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// ---------- Audio (Atomic Echo Base, ES8311) ----------
#define SAMPLE_RATE 22050  // kept from the original so the band tuning is identical
#define SAMPLES     512    // power of 2 for FFT
#define MIC_GAIN    3.0
M5EchoBase echobase;   // no ctor arg: on ESP-IDF5 / core 3.x the lib uses the new I2S API. Pinmap set in init(): SDA38 SCL39 DIN7 WS6 DOUT5 BCK8

ArduinoFFT<double> FFT = ArduinoFFT<double>();
double vReal[SAMPLES];
double vImag[SAMPLES];

// ---------- Analysis state (unchanged) ----------
float smoothedBass = 0, smoothedMid = 0, smoothedTreble = 0, smoothedVolume = 0;
float peakVolume = 0;
const float SMOOTHING_FACTOR = 0.3;

// --- Music Match mode: timbre (centroid) + rhythm (onsets) ---
float smoothedCentroid = 0;   // 0..1, tracks the brightness/timbre of the sound
float kickEnv = 0;            // decays after a low-band (kick) hit
float hatEnv  = 0;            // decays after a high-band (hat/snare) hit
float musicHue = 0;           // eased hue driven by the centroid
float freqHue  = 0;           // hue from the LOG-frequency centroid (Dance mode)

// --- Dance mode: a self-correcting beat clock from kick timing ---
uint32_t lastKickMs   = 0;    // time of last counted kick
float    beatPeriod   = 500;  // ms between kicks (smoothed), ~120 BPM to start
float    danceBeat    = 0;    // continuous beat-clock: free-runs at tempo, resyncs on kicks
uint32_t lastFrameMs2 = 0;    // dt source for the beat-clock
bool     newBeat      = false;// true on each counted beat (Dance mode steps a foot)

// ---------- Modes ----------
int displayMode = 5;   // start on mode 6 (Dance)
const int NUM_MODES = 6;
unsigned long lastModeChange = 0;
const unsigned long MODE_CHANGE_INTERVAL = 10000; // auto-cycle every 10 s (as original)
const bool AUTO_CYCLE = false;                     // modes change ONLY on a button press

const char* MODE_NAMES[NUM_MODES] = { "Spectrum", "Color Wash", "Bass Pulse", "Freq Waves", "Music Match", "Dance" };
uint16_t MODE_COLORS[NUM_MODES];  // filled in setup()

// prototypes
bool readAudio();  void analyzeAudio();
void drawSpectrumAnalyzer(); void drawFrequencyColorWash(); void drawBassPulse(); void drawFrequencyWaves(); void drawMusicMatch(); void drawDanceMotion();
uint32_t HSVtoRGB(int hue, int sat, int val);
void showModeScreen(); void drawVU();

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);                 // brings up the AtomS3 LCD (M5.Display) + button
  Serial.begin(115200);

  MODE_COLORS[0] = TFT_CYAN;
  MODE_COLORS[1] = TFT_GREEN;
  MODE_COLORS[2] = TFT_ORANGE;
  MODE_COLORS[3] = TFT_MAGENTA;
  MODE_COLORS[4] = TFT_WHITE;
  MODE_COLORS[5] = TFT_RED;

  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  strip.show();

  // Atomic Echo Base -- AtomS3 pinmap (SDA=38 SCL=39 DIN=7 WS=6 DOUT=5 BCK=8).
  // On a COLD power-up the ES8311 codec/expander may not be ready the instant
  // setup() runs, so init() can fail -- which is why a warm RESET "fixed" it.
  // Let the rails settle, then retry until init() actually succeeds.
  delay(250);
  bool micReady = false;
  for (int attempt = 0; attempt < 20 && !micReady; attempt++) {
    micReady = echobase.init(SAMPLE_RATE, 38, 39, 7, 6, 5, 8, Wire);
    if (!micReady) { Serial.println("EchoBase init failed; retrying..."); delay(150); }
  }
  if (micReady) {
    echobase.setMicGain(ES8311_MIC_GAIN_6DB);
    echobase.setMute(true);      // keep the speaker/amp quiet; we only listen
    Serial.println("EchoBase ready");
  } else {
    Serial.println("EchoBase init FAILED after retries");
  }

  showModeScreen();
  Serial.println("Echo-Reactive (AtomS3 + Echo Base) ready");
}

void loop() {
  M5.update();

  if (M5.BtnA.wasPressed()) {
    displayMode = (displayMode + 1) % NUM_MODES;
    lastModeChange = millis();
    showModeScreen();
  }
  if (AUTO_CYCLE && millis() - lastModeChange > MODE_CHANGE_INTERVAL) {
    displayMode = (displayMode + 1) % NUM_MODES;
    lastModeChange = millis();
    showModeScreen();
  }

  if (readAudio()) {
    analyzeAudio();
    switch (displayMode) {
      case 0: drawSpectrumAnalyzer();  break;
      case 1: drawFrequencyColorWash(); break;
      case 2: drawBassPulse();          break;
      case 3: drawFrequencyWaves();     break;
      case 4: drawMusicMatch();         break;
      case 5: drawDanceMotion();        break;
    }
    drawVU();   // update the LCD volume bar
  }

  delay(6);     // faster loop -> snappier button + smoother animation
}

// ---------- Audio capture (Echo Base replaces the old initI2S/i2s_read) ----------
bool readAudio() {
  static int16_t samples[SAMPLES];
  if (!echobase.record((uint8_t*)samples, sizeof(samples))) return false;  // skip bad frames
  for (int i = 0; i < SAMPLES; i++) {
    vReal[i] = (double)samples[i] * MIC_GAIN;
    vImag[i] = 0.0;
  }
  return true;
}

// ---------- Everything below is unchanged from the original sketch ----------
void analyzeAudio() {
  FFT.windowing(vReal, SAMPLES, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  FFT.compute(vReal, vImag, SAMPLES, FFT_FORWARD);
  FFT.complexToMagnitude(vReal, vImag, SAMPLES);

  float newBass = 0, newMid = 0, newTreble = 0, newVolume = 0;

  // Bass: 40-250 Hz (bins 1-6) at 22050 Hz / 512
  for (int i = 1; i <= 6; i++)   newBass += vReal[i];
  newBass /= 6.0;
  // Mid: 250-4000 Hz (bins 6-93)
  for (int i = 6; i <= 93; i++)  newMid += vReal[i];
  newMid /= 88.0;
  // Treble: 4000-11000 Hz (bins 93-256)
  for (int i = 93; i <= 256; i++) newTreble += vReal[i];
  newTreble /= 164.0;
  // Overall RMS
  for (int i = 1; i < SAMPLES / 2; i++) newVolume += vReal[i] * vReal[i];
  newVolume = sqrt(newVolume / (SAMPLES / 2));

  // ---- Volume reference: fast up to peaks, slow decay (original behaviour) ----
  if (newVolume > peakVolume) peakVolume = newVolume * 0.9 + peakVolume * 0.1;
  else                        peakVolume = peakVolume * 0.997;   // was 0.999; a bit quicker recovery after loud bursts

  smoothedBass   = smoothedBass   * (1.0 - SMOOTHING_FACTOR) + newBass   * SMOOTHING_FACTOR;
  smoothedMid    = smoothedMid    * (1.0 - SMOOTHING_FACTOR) + newMid    * SMOOTHING_FACTOR;
  smoothedTreble = smoothedTreble * (1.0 - SMOOTHING_FACTOR) + newTreble * SMOOTHING_FACTOR;
  smoothedVolume = smoothedVolume * (1.0 - SMOOTHING_FACTOR) + newVolume * SMOOTHING_FACTOR;

  if (peakVolume > 0) {
    smoothedBass   = constrain(smoothedBass   / peakVolume, 0.0, 1.0);
    smoothedMid    = constrain(smoothedMid    / peakVolume, 0.0, 1.0);
    smoothedTreble = constrain(smoothedTreble / peakVolume, 0.0, 1.0);
    smoothedVolume = constrain(smoothedVolume / peakVolume, 0.0, 1.0);
  }

  // ---- Absolute noise-floor gate: silent room / very quiet music -> dark (measured floor) ----
  const float GATE_LO = 20000.0f, GATE_HI = 30000.0f;   // set-and-forget: safely above office ambient (~17.5k peaks); playa is louder
  float noiseGate = constrain((newVolume - GATE_LO) / (GATE_HI - GATE_LO), 0.0f, 1.0f);
  smoothedVolume *= noiseGate;

  // ---- Timbre: spectral centroid (amplitude-weighted mean bin), 0..1 ----
  double cNum = 0, cDen = 0;
  for (int i = 1; i < SAMPLES / 2; i++) { cNum += (double)i * vReal[i]; cDen += vReal[i]; }
  float centroid = (cDen > 0) ? (float)(cNum / cDen) / (SAMPLES / 2) : 0;
  smoothedCentroid = smoothedCentroid * 0.8 + centroid * 0.2;

  // ---- DIAGNOSTIC CSV over Serial (logging only, no visual change). Set false to disable. ----
  static const bool DIAG_CSV = true;
  static uint32_t lastLog = 0;
  if (DIAG_CSV && millis() - lastLog >= 30) {           // ~33 rows/sec
    lastLog = millis();
    int pkBin = 1; double pkMag = 0;
    for (int i = 1; i < SAMPLES / 2; i++) if (vReal[i] > pkMag) { pkMag = vReal[i]; pkBin = i; }
    // D, ms, rawBass, rawMid, rawTreble, centroid(0..1), peakBin, peakHz, rawVolume
    Serial.printf("D,%lu,%.1f,%.1f,%.1f,%.4f,%d,%.1f,%.1f\n",
                  millis(), newBass, newMid, newTreble, centroid,
                  pkBin, pkBin * (float)SAMPLE_RATE / SAMPLES, newVolume);
  }

  // ---- Log-frequency centroid -> hue: spreads bass/vocals/highs across the rainbow ----
  const float LOGHUE_LO = 2.2f, LOGHUE_HI = 6.5f;   // shift/widen where the rainbow sits
  double lnum = 0, lden = 0;
  for (int i = 2; i < SAMPLES / 2; i++) { double w = vReal[i]; lnum += w * log2f((float)i); lden += w; }
  float logC = (lden > 0) ? (float)(lnum / lden) : LOGHUE_LO;      // ~1(bin2)..8(bin255)
  float hueT = constrain((logC - LOGHUE_LO) / (LOGHUE_HI - LOGHUE_LO), 0.0f, 1.0f);
  freqHue = freqHue * 0.7f + (hueT * 300.0f) * 0.3f;              // 0=red .. 300=violet, tracks fast

  // ---- Rhythm: per-band spectral flux -> adaptive onset (kick / hat) ----
  static float prevBass = 0, prevTreble = 0;
  static float avgBassFlux = 0, avgTrebleFlux = 0;
  float bandBass = 0, bandTreble = 0;
  for (int i = 1;  i <= 6;   i++) bandBass   += vReal[i];
  for (int i = 93; i <= 256; i++) bandTreble += vReal[i];
  float bassFlux   = max(0.0f, bandBass   - prevBass);
  float trebleFlux = max(0.0f, bandTreble - prevTreble);
  prevBass = bandBass; prevTreble = bandTreble;
  avgBassFlux   = avgBassFlux   * 0.95f + bassFlux   * 0.05f;   // self-calibrating baseline
  avgTrebleFlux = avgTrebleFlux * 0.95f + trebleFlux * 0.05f;
  // advance the dance beat-clock continuously at the estimated tempo
  uint32_t nowf = millis();
  if (lastFrameMs2 == 0) lastFrameMs2 = nowf;
  danceBeat += ((nowf - lastFrameMs2) / 1000.0f) / (beatPeriod / 1000.0f);
  lastFrameMs2 = nowf;

  const float ONSET_SENS = 1.6f;    // higher = fewer, stronger beats
  if (smoothedVolume > 0.08f) {     // only look for beats when there's real sound
    if (bassFlux > avgBassFlux * ONSET_SENS) {
      kickEnv = 1.0f;
      if (nowf - lastKickMs > 150) {                 // refractory: count one beat per kick
        uint32_t interval = nowf - lastKickMs;
        if (interval > 200 && interval < 1500)       // plausible 40-300 BPM
          beatPeriod = beatPeriod * 0.8f + interval * 0.2f;
        lastKickMs = nowf;
        newBeat = true;                                        // trigger a Dance foot-step
        danceBeat += 0.20f * (roundf(danceBeat) - danceBeat);  // soft resync to the kick
      }
    }
    if (trebleFlux > avgTrebleFlux * ONSET_SENS) hatEnv = 1.0f;
  }
  kickEnv *= 0.85f;   // decay the hit envelopes each frame
  hatEnv  *= 0.80f;
}

void drawSpectrumAnalyzer() {
  for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, 0);
  int sectionSize = NUM_LEDS / 3;

  int bassLEDs = smoothedBass * sectionSize;
  for (int i = 0; i < bassLEDs && i < sectionSize; i++) {
    int intensity = 255 * ((float)(i + 1) / sectionSize) * smoothedVolume;
    strip.setPixelColor(i, HSVtoRGB(240, 255, intensity));
  }
  int midLEDs = smoothedMid * sectionSize;
  for (int i = 0; i < midLEDs && i < sectionSize; i++) {
    int intensity = 255 * ((float)(i + 1) / sectionSize) * smoothedVolume;
    strip.setPixelColor(sectionSize + i, HSVtoRGB(120, 255, intensity));
  }
  int trebleLEDs = smoothedTreble * sectionSize;
  for (int i = 0; i < trebleLEDs && i < sectionSize; i++) {
    int intensity = 255 * ((float)(i + 1) / sectionSize) * smoothedVolume;
    strip.setPixelColor(sectionSize * 2 + i, HSVtoRGB(0, 255, intensity));
  }
  strip.show();
}

void drawFrequencyColorWash() {
  float maxLevel = max(smoothedBass, max(smoothedMid, smoothedTreble));
  int dominantHue;
  if (smoothedBass == maxLevel)      dominantHue = 240;
  else if (smoothedMid == maxLevel)  dominantHue = 120;
  else                               dominantHue = 0;

  for (int i = 0; i < NUM_LEDS; i++) {
    float positionVariation = sin((float)i / NUM_LEDS * 2.0 * PI) * 30;
    int pixelHue = dominantHue + positionVariation;
    if (pixelHue < 0) pixelHue += 360;
    if (pixelHue >= 360) pixelHue -= 360;
    float positionIntensity = 0.7 + 0.3 * sin((float)i / NUM_LEDS * PI);
    int intensity = 255 * smoothedVolume * positionIntensity;
    strip.setPixelColor(i, HSVtoRGB(pixelHue, 255, intensity));
  }
  strip.show();
}

void drawBassPulse() {
  static float pulsePhase = 0;
  pulsePhase += smoothedBass * 0.5;
  if (pulsePhase > 2.0 * PI) pulsePhase -= 2.0 * PI;
  float pulseIntensity = (sin(pulsePhase) + 1.0) / 2.0;

  int baseHue = 0;
  if (smoothedMid > smoothedBass && smoothedMid > smoothedTreble)          baseHue = 120;
  else if (smoothedTreble > smoothedBass && smoothedTreble > smoothedMid)  baseHue = 60;

  for (int i = 0; i < NUM_LEDS; i++) {
    float distance = abs(i - NUM_LEDS / 2) / (float)(NUM_LEDS / 2);
    float radialPulse = 1.0 - distance;
    float totalIntensity = pulseIntensity * radialPulse * smoothedVolume * 255;
    int pixelHue = baseHue + (i * 2) % 60;
    strip.setPixelColor(i, HSVtoRGB(pixelHue, 255, totalIntensity));
  }
  strip.show();
}

void drawFrequencyWaves() {
  static float wavePhase = 0;
  wavePhase += 0.1;
  if (wavePhase > 2.0 * PI) wavePhase -= 2.0 * PI;

  for (int i = 0; i < NUM_LEDS; i++) {
    float position = (float)i / NUM_LEDS;
    float bassWave   = sin(position * 2.0 * PI + wavePhase)        * smoothedBass;
    float midWave    = sin(position * 4.0 * PI + wavePhase * 1.5)  * smoothedMid;
    float trebleWave = sin(position * 8.0 * PI + wavePhase * 2.0)  * smoothedTreble;
    float r = (bassWave   + 1.0) / 2.0 * 255 * smoothedVolume;
    float g = (midWave    + 1.0) / 2.0 * 255 * smoothedVolume;
    float b = (trebleWave + 1.0) / 2.0 * 255 * smoothedVolume;
    r = constrain(r, 0, 255); g = constrain(g, 0, 255); b = constrain(b, 0, 255);
    strip.setPixelColor(i, strip.Color((int)r, (int)g, (int)b));
  }
  strip.show();
}

// ---- Music Match: hue follows timbre; kicks thump warm, hats sparkle cool ----
void drawMusicMatch() {
  const float CENTROID_LO = 0.03, CENTROID_HI = 0.30;  // stretch usable timbre -> full hue
  float c = constrain((smoothedCentroid - CENTROID_LO) / (CENTROID_HI - CENTROID_LO), 0.0f, 1.0f);
  float targetHue = c * 260.0;                 // bassy -> red, bright/airy -> blue-violet
  musicHue = musicHue * 0.85 + targetHue * 0.15;

  int baseVal = (int)(smoothedVolume * 200);   // volume -> base brightness
  for (int i = 0; i < NUM_LEDS; i++) {
    float center = 1.0 - (abs(i - NUM_LEDS / 2) / (float)(NUM_LEDS / 2)); // 1 at center
    int hue = (int)musicHue;
    if (kickEnv > 0.15) hue = (int)(hue * (1.0 - kickEnv) + 10 * kickEnv); // kick pulls warm
    int val = constrain(baseVal + (int)(kickEnv * center * 255), 0, 255);  // kick thumps center
    strip.setPixelColor(i, HSVtoRGB(((hue % 360) + 360) % 360, 255, val));
  }
  if (hatEnv > 0.2) {                           // hats/snare -> icy sparkles
    int sparkles = (int)(hatEnv * 8);
    for (int s = 0; s < sparkles; s++)
      strip.setPixelColor(random(NUM_LEDS), HSVtoRGB(190, 120, (int)(hatEnv * 255)));
  }
  strip.show();
}

// ---- Dance: hump sways to the beat; per-LED shimmer + hue speckle break up the uniformity ----
void drawDanceMotion() {
  const float SWAY_BEATS     = 2.0;   // beats per full left->right->left cycle (try 1, 2, 4)
  const float SWAY_AMPLITUDE = 0.9;   // 0..1, how far across the strip it travels
  const float CENTROID_LO = 0.14, CENTROID_HI = 0.205;  // tuned smoothed-centroid colour range
  const float TEX_DEPTH = 0.55;   // per-LED brightness variance (0 = uniform, 1 = strong)
  const int   HUE_VAR   = 16;     // per-LED hue speckle in degrees (0 = single hue)

  static bool  texInit = false;
  static float texPhase[NUM_LEDS], texRate[NUM_LEDS];
  static int   hueOff[NUM_LEDS];
  if (!texInit) {                                   // give each LED its own rate/phase/hue offset
    for (int i = 0; i < NUM_LEDS; i++) {
      texPhase[i] = random(0, 6283) / 1000.0f;      // random start phase 0..2pi
      texRate[i]  = 0.02f + random(0, 60) / 1000.0f;// own slow oscillation rate (~2-8s period)
      hueOff[i]   = random(-HUE_VAR, HUE_VAR + 1);  // fixed per-LED hue speckle (not position-mapped)
    }
    texInit = true;
  }

  float sway = sinf((danceBeat / SWAY_BEATS) * 2.0f * PI);        // -1..1
  float centerPos = (NUM_LEDS - 1) * 0.5f * (1.0f + sway * SWAY_AMPLITUDE);
  float cc = constrain((smoothedCentroid - CENTROID_LO) / (CENTROID_HI - CENTROID_LO), 0.0f, 1.0f);
  int   baseHue  = (int)(cc * 260);
  float baseFill = smoothedVolume * 55.0f;                        // dim wash so color always shows
  float humpH    = smoothedVolume * 255.0f + kickEnv * 120.0f;    // bright peak, punched by kicks
  float humpW    = 10.0f + kickEnv * 22.0f;                       // kicks widen the hump

  // at loud/kick peaks the per-LED variance washes out -> strip fills toward uniform full
  float peakness = constrain(smoothedVolume * 1.1f + kickEnv, 0.0f, 1.0f);
  float depth = TEX_DEPTH * (1.0f - peakness);

  for (int i = 0; i < NUM_LEDS; i++) {
    float dist = i - centerPos;
    float hump = expf(-(dist * dist) / (2.0f * humpW * humpW));   // gaussian slosh
    float val  = baseFill + humpH * hump;
    texPhase[i] += texRate[i];
    float tex = 1.0f - depth * (0.5f + 0.5f * sinf(texPhase[i])); // per-LED (1-depth)..1
    val *= tex;
    int hue = baseHue + hueOff[i];
    if (hue < 0) hue += 360; else if (hue >= 360) hue -= 360;
    strip.setPixelColor(i, HSVtoRGB(hue, 255, constrain((int)val, 0, 255)));
  }
  if (hatEnv > 0.2) {                                             // random sparkles (unchanged)
    int n = (int)(hatEnv * 6);
    for (int s = 0; s < n; s++)
      strip.setPixelColor(random(NUM_LEDS), HSVtoRGB(190, 120, (int)(hatEnv * 255)));
  }
  strip.show();
}

uint32_t HSVtoRGB(int hue, int sat, int val) {
  hue = hue % 360;
  int r, g, b;
  int i = hue / 60;
  int f = hue % 60;
  int p = (val * (255 - sat)) / 255;
  int q = (val * (255 - (sat * f) / 60)) / 255;
  int t = (val * (255 - (sat * (60 - f)) / 60)) / 255;
  switch (i) {
    case 0: r = val; g = t; b = p; break;
    case 1: r = q; g = val; b = p; break;
    case 2: r = p; g = val; b = t; break;
    case 3: r = p; g = q; b = val; break;
    case 4: r = t; g = p; b = val; break;
    case 5: r = val; g = p; b = q; break;
    default: r = 0; g = 0; b = 0; break;
  }
  return strip.Color(r, g, b);
}

// ---------- AtomS3 LCD: mode readout + live volume bar ----------
void showModeScreen() {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(MODE_COLORS[displayMode], TFT_BLACK);
  M5.Display.setTextSize(4);
  M5.Display.drawString(String(displayMode + 1), 64, 40);
  M5.Display.setTextSize(2);
  M5.Display.drawString(MODE_NAMES[displayMode], 64, 82);
}

void drawVU() {
  int w = (int)(smoothedVolume * 120);
  if (w < 0) w = 0; if (w > 120) w = 120;
  M5.Display.fillRect(4, 116, 120, 8, TFT_BLACK);
  M5.Display.fillRect(4, 116, w, 8, MODE_COLORS[displayMode]);
}
