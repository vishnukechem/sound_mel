// mel_spectrogram.cpp
#include "mel_spectrogram.h"
#include "arduinoFFT.h"
#include <math.h>
#include "esp_heap_caps.h"  // Needed for PSRAM allocation

double vReal[FFT_SIZE];
double vImag[FFT_SIZE];
ArduinoFFT<double> FFT(vReal, vImag, FFT_SIZE, SAMPLE_RATE);

float hammingWindow(int n, int N) {
  return 0.54 - 0.46 * cos(2 * M_PI * n / (N - 1));
}

// ✅ Updated to use flat float* buffer
void generateMelFilterBank(float* melFilter, int melBands, int fftSize, int sampleRate) {
  float melLow = 0;
  float melHigh = 2595 * log10(1 + (sampleRate / 2) / 700.0);
  float melStep = (melHigh - melLow) / (melBands + 1);

  float hzPoints[MEL_BANDS + 2];
  for (int i = 0; i < melBands + 2; i++) {
    float mel = melLow + i * melStep;
    hzPoints[i] = 700 * (pow(10, mel / 2595.0) - 1);
  }

  int binPoints[MEL_BANDS + 2];
  for (int i = 0; i < melBands + 2; i++) {
    binPoints[i] = floor((fftSize + 1) * hzPoints[i] / sampleRate);
  }

  for (int i = 0; i < melBands; i++) {
    for (int k = 0; k < fftSize / 2 + 1; k++) {
      int idx = i * (fftSize / 2 + 1) + k;
      if (k < binPoints[i]) {
        melFilter[idx] = 0;
      } else if (k < binPoints[i + 1]) {
        melFilter[idx] = (float)(k - binPoints[i]) / (binPoints[i + 1] - binPoints[i]);
      } else if (k < binPoints[i + 2]) {
        melFilter[idx] = (float)(binPoints[i + 2] - k) / (binPoints[i + 2] - binPoints[i + 1]);
      } else {
        melFilter[idx] = 0;
      }
    }
  }
}

void computeMelSpectrogram(float* pcmBuffer, int totalSamples, float mel[MEL_BANDS][NUM_FRAMES]) {
  Serial.println("🧠 Starting Mel Spectrogram computation");

  // 🔥 Allocate melFilter in PSRAM instead of stack
  float* melFilter = (float*) heap_caps_malloc(sizeof(float) * MEL_BANDS * (FFT_SIZE / 2 + 1), MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT);
  if (!melFilter) {
    Serial.println("❌ Failed to allocate melFilter");
    return;
  }

  generateMelFilterBank(melFilter, MEL_BANDS, FFT_SIZE, SAMPLE_RATE);
  Serial.println("✅ Mel filter bank generated");

  for (int frame = 0; frame < NUM_FRAMES; frame++) {
    if (frame % 4 == 0) vTaskDelay(1);
    int start = frame * HOP_SIZE;

    for (int i = 0; i < FFT_SIZE; i++) {
      vReal[i] = (start + i < totalSamples) ? pcmBuffer[start + i] * hammingWindow(i, FFT_SIZE) : 0;
      vImag[i] = 0;
    }

    FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward);
    FFT.compute(FFTDirection::Forward);
    FFT.complexToMagnitude();

    for (int melBand = 0; melBand < MEL_BANDS; melBand++) {
      if (melBand % 32 == 0) vTaskDelay(1);
      float sum = 0;
      for (int k = 0; k < FFT_SIZE / 2 + 1; k++) {
        int idx = melBand * (FFT_SIZE / 2 + 1) + k;
        sum += vReal[k] * melFilter[idx];
      }
      mel[melBand][frame] = log10f(sum + 1e-6);
    }
  }

  heap_caps_free(melFilter);
  Serial.println("✅ Mel spectrogram computation complete");
  // Inside computeMelSpectrogram

}
