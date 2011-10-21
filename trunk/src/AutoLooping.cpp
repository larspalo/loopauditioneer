/* 
 * AutoLooping.cpp tries to find natural good loop points in audio
 * Copyright (C) 2011 Lars Palo 
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

#include "AutoLooping.h"
#include <cmath>

AutoLooping::AutoLooping(
    double threshold,
    double minLoopDuration,
    double distanceBetweenLoops,
    double quality,
    int maxCandidates,
    int loopsToReturn,
    int maxLoopsMultiple) {
  m_derivativeThreshold = threshold;
  m_minLoopDuration = minLoopDuration;
  m_distanceBetweenLoops = distanceBetweenLoops;
  m_qualityFactor = quality;
  m_maxCandidates = maxCandidates;
  m_loopsToReturn = loopsToReturn;
  m_maxLoopsMultiple = maxLoopsMultiple;
}

AutoLooping::~AutoLooping() {
}

bool AutoLooping::AutoFindLoops(
  const double data[],
  unsigned arrayLength,
  int numberOfChannels,
  int samplerate,
  std::vector<std::pair<std::pair<unsigned, unsigned>, double> > &loops,
  bool autosearchSustainsection,
  int startPercentage,
  int endPercentage) {

  // find out which channel is strongest
  int strongestChannel;
  double maxValue = 0;
  for (unsigned i = 0; i < arrayLength; i += numberOfChannels) {
    for (int j = 0; j < numberOfChannels; j++) {
      double currentValue = fabs(data[i + j]);

      if (currentValue > maxValue) {
        maxValue = currentValue;
        strongestChannel = j;
      }
    }
  }

  unsigned sustainStartIndex, sustainEndIndex;
  if (autosearchSustainsection) {
    // set a windowsize for a 20 Hz frequency in current file
    int windowSize = samplerate / 20 * numberOfChannels;

    // Find sustainstart by scanning from the beginning
    double maxAmplitudeValue = 0;
  
    for (unsigned i = 0; i < arrayLength - windowSize; i += windowSize) {
      double maxValueInThisWindow = 0;
      for (int j = i; j < i + windowSize; j++) {
        double currentValue = fabs(data[j]);

        if (currentValue > maxValueInThisWindow)
          maxValueInThisWindow = currentValue;
      }

      if (maxValueInThisWindow > maxAmplitudeValue)
        maxAmplitudeValue = maxValueInThisWindow;
      else {
        // the max value in the window is not increasing anymore so 
        // sustainsection is reached
        sustainStartIndex = i + windowSize;
        break;
      }
    }

    sustainStartIndex -= (sustainStartIndex % numberOfChannels);

    // then we add an offset of 0.25 seconds to allow the tone to stabilize
    sustainStartIndex += samplerate * numberOfChannels / 4;

    // now find sustainend by scanning from the end of audio data
    maxAmplitudeValue = 0;
  
    for (unsigned i = arrayLength; i > 0 + windowSize; i -= windowSize) {
      double maxValueInThisWindow = 0;
      for (int j = i; j > i - windowSize; j--) {
        double currentValue = fabs(data[j]);

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
        // sustainsectionend is reached
        sustainEndIndex = i;
        break;
      }
    }

    sustainEndIndex -= (sustainEndIndex % numberOfChannels);

    if (sustainStartIndex > sustainEndIndex)
      return false;
  } else {
    if (endPercentage < startPercentage)
      return false;
    sustainStartIndex = ((double) startPercentage / 100.0) * arrayLength;
    sustainEndIndex = ((double) endPercentage / 100.0) * arrayLength;
  }

  // next we find maximum derivative in strongest channel which is where the
  // waveform will change the most (the opposite of what we're interested in)
  double maxDerivative = 0;
  for (
    unsigned i = sustainStartIndex + strongestChannel; 
    i < sustainEndIndex - numberOfChannels; 
    i += numberOfChannels) {

    double currentDerivative = fabs( (data[i + numberOfChannels] - data[i]) );

    if (currentDerivative > maxDerivative)
      maxDerivative = currentDerivative;
  }

  // since we're interested in sections where the waveform doesn't change a lot
  // we now add all indexes with a derivative below the derivativeThreshold to 
  // the every candidate vector
  std::vector<unsigned> everyLoopCandidates;
  double derivativeThreshold = maxDerivative * m_derivativeThreshold;
  for (
    unsigned i = sustainStartIndex + strongestChannel; 
    i < sustainEndIndex - numberOfChannels;
    i += numberOfChannels) {

    double currentDerivative = fabs( (data[i + numberOfChannels] - data[i]) );

    if (currentDerivative < derivativeThreshold)
      everyLoopCandidates.push_back(i);
  }

  std::vector<unsigned> loopCandidates;
  // to ensure even distribution of the candidates over sustainsection and
  // limit the number of candidates to m_maxCandidates if more are found
  if (everyLoopCandidates.size() > m_maxCandidates) {
    int totalAmountOfCandidates = everyLoopCandidates.size();
    double increment = (double) totalAmountOfCandidates / (double) m_maxCandidates;
    for (int i = 0; i < m_maxCandidates; i++) {
      int indexToUse = i * increment;
      if (indexToUse < totalAmountOfCandidates - 1)
        loopCandidates.push_back(everyLoopCandidates[indexToUse]);
    }

    everyLoopCandidates.clear();
  } else {
    // just copy the content of every loop candidates vector and get rid of it
    for (int i = 0; i < everyLoopCandidates.size(); i++)
      loopCandidates.push_back(everyLoopCandidates[i]);

    everyLoopCandidates.clear();
  }

  // Then we cross correlate the points and if we get a good match we push the
  // sample indexes of start and end into the foundLoops vector, just remember
  // that the values in the candidate vector is related to the selection and
  // needs adjustment if strongest channel wasn't the first.
  // Also note that for both the loopstart and end we compare a "window" of
  // two samples before the candidate to two after the candidate which gives
  // five samples per channel to the window. If correlation is sufficiently
  // good then we'll add the loop but adjust the end index to one sample less
  std::vector<std::pair<std::pair<unsigned, unsigned>, double > > foundLoops;
  if (loopCandidates.empty() == true)
    return false;
  for (int i = 0; i < loopCandidates.size() - 1; i++) {
    // this is for the start point
    unsigned loopStartIndex = loopCandidates[i] - strongestChannel;
    unsigned compareStartIndex = loopStartIndex - 2 * numberOfChannels;

    // if loop start point is too close to already stored loop continue
    if (foundLoops.empty() == false) {
      if (
        (loopStartIndex - foundLoops.back().first.first * numberOfChannels) < 
        (samplerate * m_distanceBetweenLoops * numberOfChannels)
      ) {
        continue;
      }
    }

    // and now compare to end point candidates
    for (int j = i + 1; j < loopCandidates.size(); j++) {
      unsigned loopEndIndex = loopCandidates[j] - strongestChannel;
      
      // check if the endpoint is too close to startpoint
      if (loopEndIndex - loopStartIndex < samplerate * m_minLoopDuration * numberOfChannels)
        continue;

      unsigned compareEndIndex = loopEndIndex - 2 * numberOfChannels;

      // now comes the actual cross correlation of the candidates
      double sum = 0, correlationValue;
      for (int k = 0; k < 5 * numberOfChannels; k++) {
        sum += pow( (data[compareStartIndex + k] - data[compareEndIndex + k]), 2);
  
        correlationValue = sqrt(sum / (5.0 * numberOfChannels));
      }

      // if the quality of the correlation is above quality threshold add the loop
      // but remove one sample from end index for a better loop match
      if (correlationValue < m_qualityFactor / 32767.0 * numberOfChannels) {
        foundLoops.push_back(
          std::make_pair(
            std::make_pair(
              (loopStartIndex / numberOfChannels),
              ((loopEndIndex / numberOfChannels) - 1)
            ),
            correlationValue
          )
        );
        break;
      }
    }
    // if enough loops to select from are found we abort
    if (foundLoops.size() > m_loopsToReturn * m_maxLoopsMultiple - 1)
      break;
  }

  // for easy handling the found loops vector should be sorted by quality
  // which will be done by searching for the best and exchange places so that
  // the best will be first in the vector
  for (int i = 0; i < foundLoops.size(); i++) {
    // find smallest (best) quality among i to foundLoops.size() - 1
    int best = i;
    for (int j = i + 1; j < foundLoops.size(); j++) {
      if (foundLoops[j].second < foundLoops[best].second)
        best = j;
    }

    // now we switch content of index i and best
    unsigned tempStart = foundLoops[i].first.first;
    unsigned tempEnd = foundLoops[i].first.second;
    double tempCorr = foundLoops[i].second;
    foundLoops[i].first.first = foundLoops[best].first.first;
    foundLoops[i].first.second = foundLoops[best].first.second;
    foundLoops[i].second = foundLoops[best].second;
    foundLoops[best].first.first = tempStart;
    foundLoops[best].first.second = tempEnd;
    foundLoops[best].second = tempCorr;
  }

  // the wished number of loops will be pushed back into the loops vector
  // selected from the best quality loops in the foundLoops vector
  if (foundLoops.size() > m_loopsToReturn) {
    for (int i = 0; i < m_loopsToReturn; i++)
      loops.push_back(foundLoops[i]);

    return true;
  } else if (foundLoops.empty() != true) {
    for (int i = 0; i < foundLoops.size(); i++)
      loops.push_back(foundLoops[i]);

    return true;
  } else {
    return false;
  }
}

void AutoLooping::SetThreshold(double th) {
  m_derivativeThreshold = th;
}
void AutoLooping::SetDuration(double d) {
  m_minLoopDuration = d;
}
void AutoLooping::SetBetween(double b) {
  m_distanceBetweenLoops = b;
}
void AutoLooping::SetQuality(double q) {
  m_qualityFactor = q;
}
void AutoLooping::SetCandidates(int c) {
  m_maxCandidates = c;
}
void AutoLooping::SetLoops(int l) {
  m_loopsToReturn = l;
}
void AutoLooping::SetMultiple(int m) {
  m_maxLoopsMultiple = m;
}

