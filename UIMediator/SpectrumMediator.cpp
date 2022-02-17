//
//    Spectrum.cpp: Coordinate spectrum signals
//    Copyright (C) 2018 Gonzalo José Carracedo Carballal
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as
//    published by the Free Software Foundation, either version 3 of the
//    License, or (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful, but
//    WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public
//    License along with this program.  If not, see
//    <http://www.gnu.org/licenses/>
//

#include "UIMediator.h"
#include "MainWindow.h"
#include "FftPanel.h"
#include "MainSpectrum.h"
#include "InspectorPanel.h"
#include "AudioPanel.h"
#include "Inspector.h"

using namespace SigDigger;

void
UIMediator::feedPSD(const Suscan::PSDMessage &msg)
{
  bool expired = false;

  if (this->appConfig->guiConfig.enableMsgTTL) {
    struct timeval now, rttime, diff;
    struct timeval max_delta;

    max_delta.tv_sec  =  this->appConfig->guiConfig.msgTTL / 1000;
    max_delta.tv_usec = (this->appConfig->guiConfig.msgTTL % 1000) * 1000;

    gettimeofday(&now, nullptr);

    rttime = msg.getRealTimeStamp();

    if (!this->haveRtDelta) {
      /* Dont' have implementation timestamp */
      timersub(&now, &rttime, &this->rtDelta);
      this->haveRtDelta = SU_TRUE;
    } else {
      /* Calculate difference */
      timersub(&now, &rttime, &diff);

      /* Upgrade RT Delta if this message arrived faster */
      if (timercmp(&this->rtDelta, &diff, >))
        this->rtDelta = diff;

      /* Subtract the intrinsic time delta */
      timersub(&diff, &this->rtDelta, &diff);

      expired = timercmp(&diff, &max_delta, >);
    }
  }

  this->setSampleRate(msg.getSampleRate());

  if (!expired) {
    this->setProcessRate(msg.getMeasuredSampleRate());
    this->averager.feed(msg);
    this->ui->spectrum->feed(
          this->averager.get(),
          static_cast<int>(this->averager.size()),
          msg.getTimeStamp(),
          msg.hasLooped());
  }
}

void
UIMediator::connectSpectrum(void)
{
  connect(
        this->ui->spectrum,
        SIGNAL(bandwidthChanged(void)),
        this,
        SLOT(onSpectrumBandwidthChanged(void)));

  connect(
        this->ui->spectrum,
        SIGNAL(frequencyChanged(qint64)),
        this,
        SLOT(onFrequencyChanged(qint64)));

  connect(
        this->ui->spectrum,
        SIGNAL(lnbFrequencyChanged(qint64)),
        this,
        SLOT(onFrequencyChanged(qint64)));

  connect(
        this->ui->spectrum,
        SIGNAL(loChanged(qint64)),
        this,
        SLOT(onLoChanged(qint64)));

  connect(
        this->ui->spectrum,
        SIGNAL(rangeChanged(float, float)),
        this,
        SLOT(onRangeChanged(float, float)));

  connect(
        this->ui->spectrum,
        SIGNAL(zoomChanged(float)),
        this,
        SLOT(onZoomChanged(float)));

  connect(
        this->ui->spectrum,
        SIGNAL(newBandPlan(QString)),
        this,
        SLOT(onNewBandPlan(QString)));

  connect(
        this->ui->spectrum,
        SIGNAL(seek(struct timeval)),
        this,
        SIGNAL(seek(struct timeval)));
}

void
UIMediator::onSpectrumBandwidthChanged(void)
{
  this->ui->inspectorPanel->setBandwidth(this->ui->spectrum->getBandwidth());
  this->appConfig->bandwidth =
      static_cast<unsigned int>(this->ui->spectrum->getBandwidth());
  emit channelBandwidthChanged(this->ui->spectrum->getBandwidth());
}

void
UIMediator::onFrequencyChanged(qint64)
{
  qint64 freq = this->ui->spectrum->getCenterFreq();

  this->ui->inspectorPanel->setDemodFrequency(freq);
  this->ui->audioPanel->setDemodFreq(freq);
  this->appConfig->profile.setFreq(static_cast<SUFREQ>(freq));

  for (auto i : this->ui->inspectorTable)
    i.second->setTunerFrequency(
          this->ui->spectrum->getCenterFreq());

  emit frequencyChanged(
        this->ui->spectrum->getCenterFreq(),
        this->ui->spectrum->getLnbFreq());

  emit loChanged(this->ui->spectrum->getLoFreq());

}

void
UIMediator::onLoChanged(qint64)
{
  qint64 freq = this->ui->spectrum->getCenterFreq()
      + this->ui->spectrum->getLoFreq();
  this->ui->inspectorPanel->setDemodFrequency(freq);
  this->ui->audioPanel->setDemodFreq(freq);
  this->appConfig->loFreq = static_cast<int>(this->ui->spectrum->getLoFreq());
  emit loChanged(this->ui->spectrum->getLoFreq());
}

void
UIMediator::onRangeChanged(float min, float max)
{
  if (!this->settingRanges) {
    this->settingRanges = true;
    this->ui->spectrum->setPandapterRange(min, max);
    this->ui->fftPanel->setPandRangeMin(std::floor(min));
    this->ui->fftPanel->setPandRangeMax(std::floor(max));

    if (this->ui->fftPanel->getRangeLock()) {
      this->ui->spectrum->setWfRange(min, max);
      this->ui->fftPanel->setWfRangeMin(std::floor(min));
      this->ui->fftPanel->setWfRangeMax(std::floor(max));
    }
    this->settingRanges = false;
  }
}

void
UIMediator::onZoomChanged(float level)
{
  bool oldState = this->ui->fftPanel->signalsBlocked();

  this->ui->fftPanel->blockSignals(true);
  this->ui->fftPanel->setFreqZoom(static_cast<int>(level));
  this->ui->fftPanel->blockSignals(oldState);
}

void
UIMediator::onNewBandPlan(QString plan)
{
  this->addBandPlan(plan.toStdString());
}

void
UIMediator::onBookmarkChanged(void)
{
  this->ui->spectrum->updateOverlay();
}

void
UIMediator::onModulationChanged(QString newModulation)
{
  this->ui->audioPanel->setDemod(
        AudioPanel::strToDemod(
          newModulation.toStdString()));
  this->refreshSpectrumFilterShape();
}
