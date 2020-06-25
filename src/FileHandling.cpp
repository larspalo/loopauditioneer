/*
 * FileHandling.cpp is a part of LoopAuditioneer software
 * Copyright (C) 2011-2020 Lars Palo
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * You can contact the author on larspalo(at)yahoo.se
 */

#include "FileHandling.h"
#include "FFT.h"
#include <cfloat>

FileHandling::FileHandling(wxString fileName, wxString path) : m_loops(NULL), m_cues(NULL), shortAudioData(NULL), intAudioData(NULL), doubleAudioData(NULL), fileOpenWasSuccessful(false), m_fftPitch(0), m_fftHPS(0), m_timeDomainPitch(0) {
  m_loops = new LoopMarkers();
  m_cues = new CueMarkers();
  wxString filePath;
  filePath = path;
  filePath += wxFILE_SEP_PATH;
  filePath += fileName;

  // Here we get all the info about the file to be able to later open new file in write mode if changes should be saved
  SndfileHandle sfHandle;

  sfHandle = SndfileHandle(std::string(filePath.mb_str())); // Open file only for read first to get all info

  if (sfHandle) { // checking if opening file was succesful or not

    m_format = sfHandle.format();
    m_samplerate = sfHandle.samplerate();
    m_channels = sfHandle.channels();
    m_minorFormat = sfHandle.format() & SF_FORMAT_SUBMASK;

    // Try to get loop info from the file
    if (sfHandle.command(4304, &instr, sizeof(instr)) == SF_TRUE) {
      // There are loops!

      m_loops->SetMIDIUnityNote(instr.basenote);
      m_loops->SetMIDIPitchFraction(instr.dwMIDIPitchFraction);

      // Stort all the loops into m_loops loopsIn vector
      for (int i = 0; i < instr.loop_count; i++) {
        LOOPDATA temp;
        temp.dwType = instr.loops[i].mode;
        temp.dwStart = instr.loops[i].start;
        temp.dwEnd = instr.loops[i].end - 1; // -1 to compensate for libsndfile behaviour
        temp.dwPlayCount = instr.loops[i].count;
        temp.shouldBeSaved = true;
        m_loops->AddLoop(temp);
      }
    }

    // Try to get cue info from file
    if (sfHandle.command(4302, &cues, sizeof(cues)) == SF_TRUE) {
      // There are cues!

      // Check if the cue is a real cue or a label, only keep real cues!
      for (int i = 0; i < cues.cue_count; i++) {
        bool toAdd = true;

        for (int j = 0; j < instr.loop_count; j++) {
          if (cues.cue_points[i].sample_offset == instr.loops[j].start)
            toAdd = false;
        }

        if (toAdd) {
          CUEPOINT tmp;
          tmp.dwName = cues.cue_points[i].indx;
          tmp.dwPosition = cues.cue_points[i].position;
          tmp.fccChunk = cues.cue_points[i].fcc_chunk;
          tmp.dwChunkStart = cues.cue_points[i].chunk_start;
          tmp.dwBlockStart = cues.cue_points[i].block_start;
          tmp.dwSampleOffset = cues.cue_points[i].sample_offset;
          tmp.keepThisCue = true;

          m_cues->AddCue(tmp);
        }
      }
    }

    // Decide what format to store audio data as and copy it
    if ((m_minorFormat == SF_FORMAT_DOUBLE) || (m_minorFormat == SF_FORMAT_FLOAT)) {
      ArrayLength = sfHandle.frames() * sfHandle.channels();
      doubleAudioData = new double[ArrayLength];
      sfHandle.read(doubleAudioData, ArrayLength);

      fileOpenWasSuccessful = true;
    } else if ((m_minorFormat == SF_FORMAT_PCM_16) || (m_minorFormat == SF_FORMAT_PCM_S8) || (m_minorFormat == SF_FORMAT_PCM_U8)) {
      ArrayLength = sfHandle.frames() * sfHandle.channels();
      shortAudioData = new short[ArrayLength];
      sfHandle.read(shortAudioData, ArrayLength);

      fileOpenWasSuccessful = true;
    } else if ((m_minorFormat == SF_FORMAT_PCM_24) || (m_minorFormat == SF_FORMAT_PCM_32)) {
      ArrayLength = sfHandle.frames() * sfHandle.channels();
      intAudioData = new int[ArrayLength];
      sfHandle.read(intAudioData, ArrayLength);

      fileOpenWasSuccessful = true;
    } else {
      // file didn't contain any audio data
      fileOpenWasSuccessful = false;
    }

    // also store audio data as doubles de-interleaved if successful
    if (fileOpenWasSuccessful) {
      // remember to "rewind" file before read
      sfHandle.seek(0, SEEK_SET);

      for (int i = 0; i < m_channels; i++) {
        WAVETRACK track;
        waveTracks.push_back(track);
      }
      double *buffer = new double[ArrayLength];
      sfHandle.read(buffer, ArrayLength);

      int index = 0;
      for (unsigned int i = 0; i < ArrayLength; i++) {
        // de-interleaving
        waveTracks[index].waveData.push_back(buffer[i]);
        index++;

        if (index == m_channels)
          index = 0;
      }
      delete[] buffer;
    }

  } else { // if file open didn't succeed we make a note of that
    fileOpenWasSuccessful = false;
  }
}

FileHandling::~FileHandling() {
  delete m_loops;

  delete m_cues;

  delete[] shortAudioData;

  delete[] intAudioData;

  delete[] doubleAudioData;
}

void FileHandling::SaveAudioFile(wxString fileName, wxString path) {
  wxString filePath;
  filePath = path;
  filePath += wxFILE_SEP_PATH;
  filePath += fileName;

  // This we open the file write
  sfh = SndfileHandle(std::string(filePath.mb_str()), SFM_WRITE, m_format, m_channels, m_samplerate);

  // Deal with the loops first
  m_loops->ExportLoops();
  instr.basenote = m_loops->GetMIDIUnityNote();
  instr.dwMIDIPitchFraction = m_loops->GetMIDIPitchFraction();
  int loopCount = m_loops->loopsOut.size();
  if (loopCount > 16)
    instr.loop_count = 16;
  else
    instr.loop_count = loopCount;
  for (int i = 0; i < instr.loop_count; i++) {
    instr.loops[i].mode = m_loops->loopsOut[i].dwType;
    instr.loops[i].start = m_loops->loopsOut[i].dwStart;
    instr.loops[i].end = m_loops->loopsOut[i].dwEnd;
    instr.loops[i].count = m_loops->loopsOut[i].dwPlayCount;
  }
  sfh.command(4305, &instr, sizeof(instr)); // this writes the loops metadata

  // Then take care of the cues
  m_cues->ExportCues();
  int cueCount = m_cues->exportedCues.size();
  if (cueCount > 99)
    cues.cue_count = 99;
  else
    cues.cue_count = cueCount;
  for (int i = 0; i < cues.cue_count; i++) {
    cues.cue_points[i].indx =  m_cues->exportedCues[i].dwName;
    cues.cue_points[i].position =  m_cues->exportedCues[i].dwPosition;
    cues.cue_points[i].fcc_chunk =  m_cues->exportedCues[i].fccChunk;
    cues.cue_points[i].chunk_start =  m_cues->exportedCues[i].dwChunkStart;
    cues.cue_points[i].block_start =  m_cues->exportedCues[i].dwBlockStart;
    cues.cue_points[i].sample_offset =  m_cues->exportedCues[i].dwSampleOffset;
  }
  sfh.command(4303, &cues, sizeof(cues));

  // Finally write the data back
  if ((m_minorFormat == SF_FORMAT_DOUBLE) || (m_minorFormat == SF_FORMAT_FLOAT)) {
    sfh.write(doubleAudioData, ArrayLength);
  } else if ((m_minorFormat == SF_FORMAT_PCM_16) || (m_minorFormat == SF_FORMAT_PCM_S8) || (m_minorFormat == SF_FORMAT_PCM_U8)) {
    sfh.write(shortAudioData, ArrayLength);
  } else {
    sfh.write(intAudioData, ArrayLength);
  }
  // File will be finally closed when the instance is deleted!
}

int FileHandling::GetSampleRate() {
  return m_samplerate;
}

void FileHandling::SetSampleRate(unsigned s_rate) {
  m_samplerate = s_rate;
}

int FileHandling::GetAudioFormat() {
  return m_minorFormat;
}

int FileHandling::GetWholeFormat() {
  return m_format;
}


bool FileHandling::FileCouldBeOpened() {
  if (fileOpenWasSuccessful)
    return true;
  else
    return false;
}

bool FileHandling::GetFFTPitch(double data[], double pitches[]) {
  bool gotPitch = DetectPitchByFFT(data);
  if (gotPitch) {
    pitches[0] = m_fftPitch;
    pitches[1] = m_fftHPS;

    return true;
  } else
    return false;
}

bool FileHandling::DetectPitchByFFT(double data[]) {
  std::vector<double> detectedPitches;
  std::vector<double> harmonicProductPitches;
  unsigned numberOfSamples = ArrayLength / m_channels;
  std::pair <unsigned, unsigned> sustainStartAndEnd;
  sustainStartAndEnd.first = 0;
  sustainStartAndEnd.second = 0;
  double *channel_data = new double[numberOfSamples];

  // Get channel data if necessary
  if (m_channels > 1)
    SeparateStrongestChannel(data, channel_data);
  else {
    for (unsigned i = 0; i < numberOfSamples; i++)
      channel_data[i] = data[i];
  }

  // Get sustainsection start and end
  sustainStartAndEnd = GetSustainStartAndEnd(channel_data);

  // Check if sustainsection is not valid and abort if so
  if (sustainStartAndEnd.first == 0 && sustainStartAndEnd.second == 0) {
    delete[] channel_data;
    return false;
  }

  // find out how large the analyze window can be
  unsigned analyzeWindowSize = 2;
  bool keepIncreasing = true;
  while (keepIncreasing) {
    if (analyzeWindowSize * 2 < sustainStartAndEnd.second - sustainStartAndEnd.first) {
      if (analyzeWindowSize < m_samplerate) {
        if (analyzeWindowSize < 65536)
          analyzeWindowSize *= 2;
        else
          keepIncreasing = false;
      } else {
        keepIncreasing = false;
      }
    } else {
      keepIncreasing = false;
    }
  }

  // if not more than one window will be analyzed put it in the middle
  if ((sustainStartAndEnd.second - sustainStartAndEnd.first) / analyzeWindowSize < 2) {
    sustainStartAndEnd.first += (sustainStartAndEnd.second - sustainStartAndEnd.first - analyzeWindowSize) / 2;
  }

  double *in = new double[analyzeWindowSize];

  int originalSize = analyzeWindowSize / 2;

  double *out = new double[originalSize];
  double *hps = new double[originalSize];
  double *outInDb = new double[originalSize];
   
  unsigned current_idx = sustainStartAndEnd.first;

  while (current_idx < sustainStartAndEnd.second - analyzeWindowSize) {
    // fill in array
    unsigned index = 0;
    for (unsigned x = current_idx; x < current_idx + analyzeWindowSize; x++) {
      in[index] = channel_data[x];
      index++;
    }

    // Apply a Gaussian window to the in data
    WindowFunc(9, analyzeWindowSize, in);

    // Perform the FFT
    PowerSpectrum(analyzeWindowSize, in, out);

    // copy to hps
    for (int i = 0; i < originalSize; i++)
      hps[i] = out[i];

    // Normalize output magnitude between 0 and 1
    double maxValuePreNorm = 0;
    for (int i = 0; i < originalSize; i++) {
      double temp = out[i];
      if (temp > maxValuePreNorm)
        maxValuePreNorm = temp;
    }

    // Convert to decibels (normalized)
    for (int i = 0; i < originalSize; i++) {
      double temp = out[i] / maxValuePreNorm;
      outInDb[i] = 10 * log10(temp);
    }

    // Find greatest peak
    double maxPeakValue = -DBL_MAX;
    int peakIndex = 0;

    for (int x = 0; x < originalSize; x++) {
      double currentValue = outInDb[x];
      if (currentValue > maxPeakValue) {
        maxPeakValue = currentValue;
        peakIndex = x;
      }
    }

    // search if there are earlier peaks that could be the fundamental
    std::vector<unsigned> allPeaksToConsider;
    double middleValue = outInDb[1];
    double lastValue = outInDb[0];
    for (int x = 2; x < peakIndex; x++) {
      double currentValue = outInDb[x];
      if (middleValue > currentValue && middleValue > lastValue) {
        // it's a peak but should it be considered?
        if (middleValue > (maxPeakValue - 36)) {
          // yes, add it to the vector
          allPeaksToConsider.push_back(x - 1);
          lastValue = middleValue;
          middleValue = currentValue;
        } else {
          // no, just continue
          lastValue = middleValue;
          middleValue = currentValue;
        }
      } else {
        lastValue = middleValue;
        middleValue = currentValue;
      }
    }
    if ((allPeaksToConsider.empty() == false) && (allPeaksToConsider.size() > 1)) {
      // sort the vector so that strongest peak is first
      for (unsigned i = 0; i < allPeaksToConsider.size() - 1; i++) {
        unsigned best = i;
        // find highest peak among i to allPeaksToConsider.size() - 1
        for (unsigned j = i + 1; j < allPeaksToConsider.size(); j++) {
          if (outInDb[ allPeaksToConsider[j] ] > outInDb[ allPeaksToConsider[best] ])
            best = j;
        }

        // now we switch content of index i and best
        unsigned tempIndex = allPeaksToConsider[i];
        allPeaksToConsider[i] = allPeaksToConsider[best];
        allPeaksToConsider[best] = tempIndex;
      }
    }

    // this is the max peak that we add last and compare to
    allPeaksToConsider.push_back(peakIndex);

    if (allPeaksToConsider.size() > 1) {
      // now see if the maxpeak can be a harmonic of any other peak
      for (int x = 0; x < allPeaksToConsider.size() - 1; x++) {
        if (labs((long)(allPeaksToConsider[x] * 2) - (long)peakIndex) < 3) {
          // peakIndex could be the first harmonic
          peakIndex = allPeaksToConsider[x];
          break;
        } else if (labs((long)(allPeaksToConsider[x] * 3) - (long)peakIndex) < 5) {
          // peakIndex could be the second harmonic of this one
          peakIndex = allPeaksToConsider[x];
          break;
        } else if (labs((long)(allPeaksToConsider[x] * 3) / 2 - (long)peakIndex) < 4) {
          // fundamental could just be a fifth lower 
          peakIndex = allPeaksToConsider[x];
          break;
        }
      }
    }

    // translate peakIndex to a frequency and store it
    double finalFrequency = TranslateIndexToPitch(
      peakIndex,
      out[peakIndex - 1],
      out[peakIndex],
      out[peakIndex + 1],
      analyzeWindowSize
    );
    detectedPitches.push_back(finalFrequency);

    allPeaksToConsider.clear();

    // now try detecting pitch with HPS using 4 harmonics
    int maxSearchIndex = originalSize / 4;
    int maxBin = 0;

    for (int i = 0; i < maxSearchIndex; i++) {
      for (int j = 2; j <= 4; j++)
        hps[i] *= hps[i * j];

      if (hps[i] > hps[maxBin])
        maxBin = i;
    }

    // try fixing possible lower fundamental errors
    // get strongest peak below up to ca a fourth below
    int correctMaxBin = 0;
    int maxsearch = (maxBin * 3) / 4;
    for (int i = 1; i < maxsearch; i++) {
      if ((hps[i] > hps[correctMaxBin]) && (abs(i * 2 - maxBin) < 4)) {
        // this is likely an octave
        correctMaxBin = i;
      } else if ((hps[i] > hps[correctMaxBin]) && (abs((i * 3) / 2 - maxBin) < 4)) {
        // this is likely a fifth
        correctMaxBin = i;
      }
    }

    if ((hps[correctMaxBin] / hps[maxBin]) > 0.001)
      maxBin = correctMaxBin;

    // translate maxBin to a frequency and store it
    double hpsFrequency = TranslateIndexToPitch(
      maxBin,
      hps[maxBin - 1],
      hps[maxBin],
      hps[maxBin + 1],
      analyzeWindowSize
    );
    harmonicProductPitches.push_back(hpsFrequency);

    // proceed to next window
    current_idx += analyzeWindowSize / 2;
  }

  delete[] in;
  delete[] out;
  delete[] hps;
  delete[] outInDb;
  delete[] channel_data;

  // try removing obvious pitch error values if still present for hps
  if (harmonicProductPitches.empty() == false) {
    for (unsigned i = 0; i < harmonicProductPitches.size() - 1; i++) {
      for (unsigned j = i + 1; j < harmonicProductPitches.size(); j++) {
        if (harmonicProductPitches[j] < harmonicProductPitches[i]) {
          double tempValue = harmonicProductPitches[i];
          harmonicProductPitches[i] = harmonicProductPitches[j];
          harmonicProductPitches[j] = tempValue;
        }
      }
    }
    for (int i = harmonicProductPitches.size() - 1; i > 0; i--) {
      if (harmonicProductPitches[i] / harmonicProductPitches[0] > 1.05)
        harmonicProductPitches.pop_back();
    }
  }

  if (detectedPitches.empty() == false) {
    double pitchSum = 0;
    for (unsigned i = 0; i < detectedPitches.size(); i++)
      pitchSum += detectedPitches[i];

    m_fftPitch = pitchSum / (double) detectedPitches.size();

    pitchSum = 0;
    for (unsigned i = 0; i < harmonicProductPitches.size(); i++)
      pitchSum += harmonicProductPitches[i];

    m_fftHPS = pitchSum / (double) harmonicProductPitches.size();

    return true;
  } else {
    return false;
  }
}

double FileHandling::TranslateIndexToPitch(
  int idxAtPeak,
  double valueBeforePeak,
  double valueAtPeak,
  double valueAfterPeak,
  unsigned wSize) {
  double pitchToReturn;

  double centerPeakBin;

  centerPeakBin = (valueAfterPeak - valueBeforePeak) / (2 * ( 2 * valueAtPeak - valueBeforePeak - valueAfterPeak));
  pitchToReturn = (idxAtPeak + centerPeakBin) * m_samplerate / (double) wSize;

  return pitchToReturn;
}

bool FileHandling::DetectPitchInTimeDomain(double audio[]) {
  unsigned numberOfSamples = ArrayLength / m_channels;
  std::pair <unsigned, unsigned> sustainStartAndEnd;
  sustainStartAndEnd.first = 0;
  sustainStartAndEnd.second = 0;
  double *channel_data = new double[numberOfSamples];

  // Get channel data if necessary
  if (m_channels > 1)
    SeparateStrongestChannel(audio, channel_data);
  else {
    for (unsigned i = 0; i < numberOfSamples; i++)
      channel_data[i] = audio[i];
  }

  // Get sustainsection start and end
  sustainStartAndEnd = GetSustainStartAndEnd(channel_data);

  // Check if sustainsection is not valid and abort if so
  if (sustainStartAndEnd.first == 0 && sustainStartAndEnd.second == 0) {
    delete[] channel_data;
    return false;
  }

  if (sustainStartAndEnd.second - sustainStartAndEnd.first < 2)
    return 0; // cannot calculate pitch

  if (sustainStartAndEnd.second - sustainStartAndEnd.first > m_samplerate * 2)
    sustainStartAndEnd.second = sustainStartAndEnd.first + m_samplerate * 2;

  std::vector<double> allDetectedPitches;
  double prev = channel_data[sustainStartAndEnd.second]; // Last sample
  unsigned end_point = 0; // Preliminary value of the last sample of period

  for (unsigned i = sustainStartAndEnd.second - 2; i > sustainStartAndEnd.first; i--) {
    double v = channel_data[i];

    /* We are interested in positive zero crossings */
    if ((v > 0.0) && (prev <= 0.0)) {
      if (!end_point) {
        end_point = i;
      } else {
        /* Found the next zero crossing - is this a good loop? */
        unsigned len = end_point - i; /* no +1 as we don't want to look at the second crossover point */
        if (i > len) {
          unsigned prev_start_point = i - len;

          /* Find the RMS of the first signal and compute the error of the second signal. */
          double rms = 0.0;
          double error_rms = 0.0;

          for (unsigned j = 0; j < len; j++) {
            double error = channel_data[j + prev_start_point] - channel_data[j + i];
            double d     = channel_data[j + prev_start_point];

            error *= error;
            d *= d;
            error_rms = error_rms * j / ((double)(j + 1)) + error / ((double)(j + 1));
            rms = rms * j / ((double)(j + 1)) + d / ((double)(j + 1));
          }

          if ((error_rms > 0.0) && (rms > 0.0)) {
            error_rms = sqrt(error_rms);
            rms = sqrt(rms);

            if ((error_rms / rms) < 0.55) {
              // store pitch
              allDetectedPitches.push_back( ((double) m_samplerate) / (double) len );
              // set endpoint for next period
              end_point = i;
            }
          }
        }
      }
    }
    prev = v;
  }

  delete[] channel_data;

  if (allDetectedPitches.empty() == false) {
    double pitchSum = 0.0;
    for (unsigned i = 0; i < allDetectedPitches.size(); i++)
      pitchSum += allDetectedPitches[i];

    m_timeDomainPitch = pitchSum / allDetectedPitches.size();
    return true;
  } else {
    m_timeDomainPitch = 0; /* Couldn't find out the pitch */
    return false;
  }
}

double FileHandling::GetTDPitch(double data[]) {
  bool gotPitch = DetectPitchInTimeDomain(data);
  if (gotPitch)
    return m_timeDomainPitch;
  else
    return 0;
}

void FileHandling::PerformCrossfade(double audioData[], int loopNumber, double fadeLength, int fadeType) {
  LOOPDATA loopToCrossfade;
  m_loops->GetLoopData(loopNumber, loopToCrossfade);
  unsigned samplesToFade = m_samplerate * fadeLength;
  unsigned samplesToFadeOut = m_samplerate * fadeLength;

  // check to not read outside the audiodata array
  if (samplesToFade > loopToCrossfade.dwStart)
    samplesToFade = loopToCrossfade.dwStart;

  // check to not write outside the audiodata array
  if ((samplesToFadeOut + loopToCrossfade.dwEnd + 1) * m_channels > ArrayLength)
    samplesToFadeOut = (ArrayLength / m_channels) - (loopToCrossfade.dwEnd + 1);

  unsigned firstTargetIdx = (loopToCrossfade.dwEnd - (samplesToFade - 1)) * m_channels;
  unsigned firstSourceIdx = (loopToCrossfade.dwStart - samplesToFade) * m_channels;
  unsigned secondTargetIdx = (loopToCrossfade.dwEnd + 1) * m_channels;
  unsigned secondSourceIdx = loopToCrossfade.dwStart * m_channels;

  // prepare arrays for the crossfade curve data
  double fadeData[samplesToFade];
  double fadeOutData[samplesToFadeOut];

  switch(fadeType) {
    case 0:
      // linear data from 0 to 1
      for (int i = 0; i < samplesToFade; i++)
        fadeData[i] = i * 1.0 / (samplesToFade - 1);
      for (int i = 0; i < samplesToFadeOut; i++)
        fadeOutData[i] = i * 1.0 / (samplesToFadeOut - 1);
      break;

    case 1:
      // create a S curve table from 0 to 1
      for (int i = 0; i < samplesToFade; i++) {
        double linear = i * 1.0 / (samplesToFade - 1);
        fadeData[i] = 0.5 * (1.0 + cos((1.0 - linear) * M_PI));
      }
      for (int i = 0; i < samplesToFadeOut; i++) {
        double linear = i * 1.0 / (samplesToFadeOut - 1);
        fadeOutData[i] = 0.5 * (1.0 + cos((1.0 - linear) * M_PI));
      }
      break;

    case 2:
      // create a curve from 0 to 1 with equal power/gain
      for (int i = 0; i < samplesToFade; i++) {
        double linear = i * 1.0 / (samplesToFade - 1);
        fadeData[i] = linear / sqrt( pow(linear, 2) + pow((1 - linear), 2) );
      }
      for (int i = 0; i < samplesToFadeOut; i++) {
        double linear = i * 1.0 / (samplesToFadeOut - 1);
        fadeOutData[i] = linear / sqrt( pow(linear, 2) + pow((1 - linear), 2) );
      }
      break;

    case 3:
       // create a sine curve table from 0 to 1
      for (int i = 0; i < samplesToFade; i++) {
        double linear = i * 1.0 / (samplesToFade - 1);
        fadeData[i] = sin(M_PI / 2 * linear);
      }
      for (int i = 0; i < samplesToFadeOut; i++) {
        double linear = i * 1.0 / (samplesToFadeOut - 1);
        fadeOutData[i] = sin(M_PI / 2 * linear);
      }
      break;

    default:
      // linear data from 0 to 1
      for (int i = 0; i < samplesToFade; i++)
        fadeData[i] = i * 1.0 / (samplesToFade - 1);
      for (int i = 0; i < samplesToFadeOut; i++)
        fadeOutData[i] = i * 1.0 / (samplesToFadeOut - 1);
  }

  // crossfade around the endpoint so that the audio won't click
  // the new loopEnd will be identical to the sample before loopStart
  // and the new sample after loopEnd will be identical to loopStart
  // with crossfades to either side of loopEnd with source from loopStart

  for (unsigned i = 0; i < samplesToFade; i++) {
    for (int j = 0; j < m_channels; j++) {
      audioData[firstTargetIdx + j] = 
        audioData[firstTargetIdx + j] * fadeData[samplesToFade - 1 - i] +
        audioData[firstSourceIdx + j] * fadeData[i];
    }
    firstTargetIdx += m_channels;
    firstSourceIdx += m_channels;
  }

  for (unsigned i = 0; i < samplesToFadeOut; i++) {
    for (int j = 0; j < m_channels; j++) {
      audioData[secondTargetIdx + j] = 
        audioData[secondTargetIdx + j] * fadeOutData[i] +
        audioData[secondSourceIdx + j] * fadeOutData[samplesToFadeOut - 1 - i];
    }
    secondTargetIdx += m_channels;
    secondSourceIdx += m_channels;
  }
}

void FileHandling::SeparateStrongestChannel(double inData[], double outData[]) {
  unsigned channel_idx = 0;

  // detect strongest channel
  double strongestValue = 0;
  for (int i = 0; i < m_channels; i++) {
    for (unsigned j = i; j < ArrayLength; j += m_channels) {
      if (fabs(inData[j]) > strongestValue) {
        strongestValue = fabs(inData[j]);
        channel_idx = i;
      }
    }
  }
 
  // fill channel_data array with values from data[]
  unsigned ch_idx = 0;
  for (unsigned data_idx = channel_idx; data_idx < ArrayLength; data_idx += m_channels) {
    outData[ch_idx] = inData[data_idx];
    ch_idx++;
  }
}

std::pair<unsigned, unsigned> FileHandling::GetSustainStartAndEnd(double ch_data[]) {
  unsigned numberOfSamples = ArrayLength / m_channels;
  std::pair<unsigned, unsigned> sustainsection;
  sustainsection.first = 0;
  sustainsection.second = 0;

  // Find strongest value
  double maxValue = 0;
  unsigned indexWithMaxValue = 0;
  for (unsigned idx = 0; idx < numberOfSamples; idx++) {
    double currentValue = fabs(ch_data[idx]);

    if (currentValue > maxValue) {
      maxValue = currentValue;
      indexWithMaxValue = idx;
    }
  }

  // now detect sustain section
  // set a windowsize for a 20 Hz frequency in current file (mono now!)
  unsigned windowSize = m_samplerate / 20;

  // Find sustainstart by scanning from the beginning
  double maxAmplitudeValue = 0;
  
  for (unsigned idx = 0; idx < numberOfSamples - windowSize; idx += windowSize) {
    double maxValueInThisWindow = 0;
    for (unsigned j = idx; j < idx + windowSize; j++) {
      double currentValue = fabs(ch_data[j]);

      if (currentValue > maxValueInThisWindow)
        maxValueInThisWindow = currentValue;
    }

    if (maxValueInThisWindow > maxAmplitudeValue)
      maxAmplitudeValue = maxValueInThisWindow;
    else {
      // the max value in the window is not increasing anymore so 
      // sustainsection is reached
      sustainsection.first = idx + windowSize;
      break;
    }
  }

  // then we add an offset of 0.25 seconds to allow the tone to stabilize
  sustainsection.first += m_samplerate / 4;

  // now find sustainend by scanning from the end of audio data
  maxAmplitudeValue = 0;
  
  for (unsigned idx = numberOfSamples - 1; idx > windowSize; idx -= windowSize) {
    double maxValueInThisWindow = 0;
    for (unsigned j = idx; j > idx - windowSize; j--) {
      double currentValue = fabs(ch_data[j]);

      if (currentValue > maxValueInThisWindow)
        maxValueInThisWindow = currentValue;
    }

    // if current max is less than one fourth of max value, or 12 dB lower
    // we just continue searching backwards
    if (maxValueInThisWindow < maxValue / 4) {
      maxAmplitudeValue = maxValueInThisWindow;
      continue;
    }

    if (maxValueInThisWindow > maxAmplitudeValue) {
      maxAmplitudeValue = maxValueInThisWindow;
    } else {
      // the max value in the window is not increasing anymore so 
      // sustainsectionend should be reached
      sustainsection.second = idx;
      break;
    }
  }
  if (sustainsection.second > sustainsection.first) {
    // then we remove an offset of 0.25 seconds to be sure
    sustainsection.second -= m_samplerate / 4;
  }

  if (sustainsection.second < sustainsection.first) {
    // this is an error situation where no sustainsection could be found
    // so we try another approach to detecting the part of sound that we can
    // analyze we know where the max value is located so we calculate where the
    // max values get no larger than one half of maxvalue in both directions
    // we start searching backwards from max index
    double lastValue = maxValue;
    double middleValue = fabs(ch_data[indexWithMaxValue + 1]);
    std::vector<std::pair<unsigned, double> > absolutePeaks;
    for (unsigned i = indexWithMaxValue + 2; i < numberOfSamples; i++) {
      double currentValue = fabs(ch_data[i]);
      if (middleValue > currentValue && middleValue > lastValue) {
        // we have a peak
        absolutePeaks.push_back(std::make_pair(i - 1, middleValue));

        lastValue = middleValue;
        middleValue = currentValue;
      } else {
        lastValue = middleValue;
        middleValue = currentValue;
      }
    }

    // now we search backwards in the vector for the first peak that is larger
    // than maxValue / 2
    if (absolutePeaks.empty() == false) {
      for (unsigned i = absolutePeaks.size() - 1; i > 0; i--) {
        if (absolutePeaks[i].second > (maxValue / 2)) {
          sustainsection.second = absolutePeaks[i].first;
          break;
        }
      }
    } else {
      // we've really failed
      return std::make_pair(0, 0);
    }

    // now we search forwards from max index
    lastValue = maxValue;
    middleValue = fabs(ch_data[indexWithMaxValue - 1]);
    absolutePeaks.clear();
    for (unsigned i = indexWithMaxValue - 2; i > 0; i--) {
      double currentValue = fabs(ch_data[i]);
      if (middleValue > currentValue && middleValue > lastValue) {
        // we have a peak
        absolutePeaks.push_back(std::make_pair(i + 1, middleValue));

        lastValue = middleValue;
        middleValue = currentValue;
      } else {
        lastValue = middleValue;
        middleValue = currentValue;
      }
    }

    // now we search backwards in the vector for the first peak that is larger
    // than maxValue / 2
    if (absolutePeaks.empty() == false) {
      for (unsigned i = absolutePeaks.size() - 1; i > 0; i--) {
        if (absolutePeaks[i].second > (maxValue / 2)) {
          sustainsection.first = absolutePeaks[i].first;
          break;
        }
      }
    } else {
      // we've really failed
      return std::make_pair(0, 0);
    }

    if (sustainsection.first < sustainsection.second)
      return sustainsection;
    else
      return std::make_pair(0, 0);
  }

  if (sustainsection.first < sustainsection.second)
    return sustainsection;
  else
    return std::make_pair(0, 0);
}

void FileHandling::TrimExcessData() {
  // Remove data between last loop and first cue larger than last loop
  // First get last loop sample
  unsigned lastEndSample = 0;
  for (int i = 0; i < m_loops->GetNumberOfLoops(); i++) {
    LOOPDATA currentLoop;
    m_loops->GetLoopData(i, currentLoop);
    if (currentLoop.dwEnd > lastEndSample)
      lastEndSample = currentLoop.dwEnd;
  }
  // only continue if there were any loops
  if (lastEndSample) {
    // now get cues greater than loopend (if there are any)
    unsigned firstCuePosAfter = 0;
    for (unsigned i = 0; i < m_cues->GetNumberOfCues(); i++) {
      CUEPOINT currentCue;
      m_cues->GetCuePoint(i, currentCue);
      if (currentCue.dwSampleOffset > lastEndSample) {
        if (!firstCuePosAfter)
          firstCuePosAfter = currentCue.dwSampleOffset;
        else if (currentCue.dwSampleOffset < firstCuePosAfter)
          firstCuePosAfter = currentCue.dwSampleOffset;
      }
    }

    long unsigned int newArrayLength = (lastEndSample + 3) * m_channels;

    if (firstCuePosAfter) {
      newArrayLength += (ArrayLength - ((firstCuePosAfter - 1) * m_channels));

      // we must also move one or more cues to new positions
      unsigned samplesToRemove = (firstCuePosAfter - 1) - (lastEndSample + 3);
      for (unsigned i = 0; i < m_cues->GetNumberOfCues(); i++) {
        CUEPOINT currentCue;
        m_cues->GetCuePoint(i, currentCue);
        if (currentCue.dwSampleOffset > lastEndSample) {
          unsigned newCuePosition = currentCue.dwSampleOffset - samplesToRemove;
          m_cues->ChangePosition(newCuePosition, i);
        }
      }
    }

    // Time to copy data to a temporary array and then to cleaned target array
    if ((m_minorFormat == SF_FORMAT_DOUBLE) || (m_minorFormat == SF_FORMAT_FLOAT)) {
      double *audioData = new double[newArrayLength];

      for (unsigned i = 0; i < (lastEndSample + 3) * m_channels; i++)
        audioData[i] = doubleAudioData[i];

      if (firstCuePosAfter) {
        unsigned newIdx = (lastEndSample + 3) * m_channels;
        for (unsigned i = (firstCuePosAfter - 1) * m_channels; i < ArrayLength; i++) {
          audioData[newIdx] = doubleAudioData[i];
          newIdx++;
        }
      }

      delete[] doubleAudioData;
      doubleAudioData = new double[newArrayLength];
      ArrayLength = newArrayLength;

      for (unsigned i = 0; i < ArrayLength; i++)
        doubleAudioData[i] = audioData[i];

      delete[] audioData;
    } else if ((m_minorFormat == SF_FORMAT_PCM_16) || (m_minorFormat == SF_FORMAT_PCM_S8) || (m_minorFormat == SF_FORMAT_PCM_U8)) {
      short *audioData = new short[newArrayLength];

      for (unsigned i = 0; i < (lastEndSample + 3) * m_channels; i++)
        audioData[i] = shortAudioData[i];

      if (firstCuePosAfter) {
        unsigned newIdx = (lastEndSample + 3) * m_channels;
        for (unsigned i = (firstCuePosAfter - 1) * m_channels; i < ArrayLength; i++) {
          audioData[newIdx] = shortAudioData[i];
          newIdx++;
        }
      }

      delete[] shortAudioData;
      shortAudioData = new short[newArrayLength];
      ArrayLength = newArrayLength;

      for (unsigned i = 0; i < ArrayLength; i++)
        shortAudioData[i] = audioData[i];

      delete[] audioData;
    } else {
      int *audioData = new int[newArrayLength];

      for (unsigned i = 0; i < (lastEndSample + 3) * m_channels; i++)
        audioData[i] = intAudioData[i];

      if (firstCuePosAfter) {
        unsigned newIdx = (lastEndSample + 3) * m_channels;
        for (unsigned i = (firstCuePosAfter - 1) * m_channels; i < ArrayLength; i++) {
          audioData[newIdx] = intAudioData[i];
          newIdx++;
        }
      }

      delete[] intAudioData;
      intAudioData = new int[newArrayLength];
      ArrayLength = newArrayLength;

      for (unsigned i = 0; i < ArrayLength; i++)
        intAudioData[i] = audioData[i];

      delete[] audioData;
    }
  }
}

bool FileHandling::TrimStart(unsigned timeToTrim) {
  // convert time to samples
  unsigned samples = (timeToTrim / 1000.0) * m_samplerate;
  unsigned samplesToCut = samples * m_channels;

  if (samplesToCut < ArrayLength) {
    // calculate new arraylength
    long unsigned int newArrayLength = ArrayLength - samplesToCut;

    if ((m_minorFormat == SF_FORMAT_DOUBLE) || (m_minorFormat == SF_FORMAT_FLOAT)) {
      double *audioData = new double[newArrayLength];

      for (unsigned i = 0; i < newArrayLength; i++)
        audioData[i] = doubleAudioData[samplesToCut + i];

      delete[] doubleAudioData;
      doubleAudioData = new double[newArrayLength];
      ArrayLength = newArrayLength;

      for (unsigned i = 0; i < ArrayLength; i++)
        doubleAudioData[i] = audioData[i];

      delete[] audioData;
    } else if ((m_minorFormat == SF_FORMAT_PCM_16) || (m_minorFormat == SF_FORMAT_PCM_S8) || (m_minorFormat == SF_FORMAT_PCM_U8)) {
      short *audioData = new short[newArrayLength];

      for (unsigned i = 0; i < newArrayLength; i++)
        audioData[i] = shortAudioData[samplesToCut + i];

      delete[] shortAudioData;
      shortAudioData = new short[newArrayLength];
      ArrayLength = newArrayLength;

      for (unsigned i = 0; i < ArrayLength; i++)
        shortAudioData[i] = audioData[i];

      delete[] audioData;
    } else {
      int *audioData = new int[newArrayLength];

      for (unsigned i = 0; i < newArrayLength; i++)
        audioData[i] = intAudioData[samplesToCut + i];

      delete[] intAudioData;
      intAudioData = new int[newArrayLength];
      ArrayLength = newArrayLength;

      for (unsigned i = 0; i < ArrayLength; i++)
        intAudioData[i] = audioData[i];

      delete[] audioData;
    }

    // if loops and/or cues exist they must now be moved!
    m_loops->MoveLoops(samples);
    m_cues->MoveCues(samples);

    return true;
  } else {
    return false;
  }
}

bool FileHandling::TrimEnd(unsigned timeToTrim) {
  // convert time to samples
  unsigned samples = (timeToTrim / 1000.0) * m_samplerate;
  unsigned samplesToCut = samples * m_channels;

  if (samplesToCut < ArrayLength) {
    // calculate new arraylength
    long unsigned int newArrayLength = ArrayLength - samplesToCut;

    if ((m_minorFormat == SF_FORMAT_DOUBLE) || (m_minorFormat == SF_FORMAT_FLOAT)) {
      double *audioData = new double[newArrayLength];

      for (unsigned i = 0; i < newArrayLength; i++)
        audioData[i] = doubleAudioData[i];

      delete[] doubleAudioData;
      doubleAudioData = new double[newArrayLength];
      ArrayLength = newArrayLength;

      for (unsigned i = 0; i < ArrayLength; i++)
        doubleAudioData[i] = audioData[i];

      delete[] audioData;
    } else if ((m_minorFormat == SF_FORMAT_PCM_16) || (m_minorFormat == SF_FORMAT_PCM_S8) || (m_minorFormat == SF_FORMAT_PCM_U8)) {
      short *audioData = new short[newArrayLength];

      for (unsigned i = 0; i < newArrayLength; i++)
        audioData[i] = shortAudioData[i];

      delete[] shortAudioData;
      shortAudioData = new short[newArrayLength];
      ArrayLength = newArrayLength;

      for (unsigned i = 0; i < ArrayLength; i++)
        shortAudioData[i] = audioData[i];

      delete[] audioData;
    } else {
      int *audioData = new int[newArrayLength];

      for (unsigned i = 0; i < newArrayLength; i++)
        audioData[i] = intAudioData[i];

      delete[] intAudioData;
      intAudioData = new int[newArrayLength];
      ArrayLength = newArrayLength;

      for (unsigned i = 0; i < ArrayLength; i++)
        intAudioData[i] = audioData[i];

      delete[] audioData;
    }

    // Check if loops and/or cues still is within audio data!
    m_loops->AreLoopsStillValid(ArrayLength);
    m_cues->AreCuesValidStill(ArrayLength);

    return true;
  } else {
    return false;
  }
}

void FileHandling::PerformFade(double audioData[], unsigned fadeLength, int fadeType) {
  // fadeType 0 == fade in, anything else is a fade out

  unsigned samplesToFade = (fadeLength / 1000.0) * m_samplerate;

  // prepare an array for the crossfade curve data
  double fadeData[samplesToFade];

  // linear data from 0 to 1
  for (int i = 0; i < samplesToFade; i++)
    fadeData[i] = i * 1.0 / (samplesToFade - 1);

  if (fadeType == 0) {
    for (unsigned i = 0; i < samplesToFade; i++) {
      for (int j = 0; j < m_channels; j++) {
        audioData[i * m_channels + j] *= fadeData[i];
      }
    }
  } else {
    for (unsigned i = 0; i < samplesToFade; i++) {
      for (int j = 0; j < m_channels; j++) {
        audioData[(ArrayLength - 1) - (i + j)] *= fadeData[i];
      }
    }
  }
}

bool FileHandling::GetDoubleAudioData(double audio[]) {
  if (!waveTracks.empty()) {
    unsigned length = waveTracks.size() * waveTracks[0].waveData.size();

    if (ArrayLength == length) {
      for (unsigned i = 0; i < waveTracks.size(); i++) {
        for (unsigned j = 0; j < waveTracks[0].waveData.size(); j++) {
          audio[i + j * waveTracks.size()] = waveTracks[i].waveData[j];
        }
      }
      return true;
    }
    return false;
  } else {
    return false;
  }
}

void FileHandling::UpdateWaveTracks(double audio[]) {
  // first empty old wavetracks
  for (int i = 0; i < waveTracks.size(); i++)
    waveTracks[i].waveData.clear();

    // copy the new track data to right tracks
    int index = 0;
    for (unsigned i = 0; i < ArrayLength; i++) {
      // de-interleaving
      waveTracks[index].waveData.push_back(audio[i]);
      index++;

      if (index == m_channels)
        index = 0;
    }
}