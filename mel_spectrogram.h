// mel_spectrogram.h
#pragma once

#define MEL_BANDS 128
#define FFT_SIZE 512
#define HOP_SIZE 256
#define NUM_FRAMES 1721  // For 10 seconds of audio at 44.1kHz with 256 hop
#define SAMPLE_RATE 44100

void computeMelSpectrogram(float* pcmBuffer, int totalSamples, float mel[MEL_BANDS][NUM_FRAMES]);
/*
NUM_FRAMES = floor((totalSamples - FFT_SIZE) / HOP_SIZE) + 1
NUM_FRAMES = floor((441000 - 512) / 256) + 1
           ≈ floor(440488 / 256) + 1
           ≈ 1720 + 1
           = 1721

           when i have to change the sample rate i have to chanfe the num of frames too 
*/
