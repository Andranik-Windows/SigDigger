//
//    PanoramicDialog.cpp: Description
//    Copyright (C) 2020 Gonzalo José Carracedo Carballal
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

#include <PanoramicDialog.h>
#include <Suscan/Library.h>
#include "ui_PanoramicDialog.h"
#include "MainSpectrum.h"
#include <SuWidgetsHelpers.h>
#include <SigDiggerHelpers.h>
#include <fstream>
#include <iomanip>
#include <limits>
#include <QFileDialog>
#include <QMessageBox>

using namespace SigDigger;

void
SavedSpectrum::set(qint64 start, qint64 end, const float *data, size_t size)
{
  this->start = start;
  this->end   = end;
  this->data.assign(data, data + size);
}

bool
SavedSpectrum::exportToFile(QString const &path)
{
  std::ofstream of(path.toStdString().c_str(), std::ofstream::binary);

  if (!of.is_open())
    return false;

  of << "%\n";
  of << "% Panoramic Spectrum file generated by SigDigger\n";
  of << "%\n\n";

  of << "freqMin = " << this->start << ";\n";
  of << "freqMax = " << this->end << ";\n";
  of << "PSD = [ ";

  of << std::setprecision(std::numeric_limits<float>::digits10);

  for (auto p : this->data)
    of << p << " ";

  of << "];\n";

  return true;
}

////////////////////////// PanoramicDialogConfig ///////////////////////////////
#define STRINGFY(x) #x
#define STORE(field) obj.set(STRINGFY(field), this->field)
#define LOAD(field) this->field = conf.get(STRINGFY(field), this->field)

void
PanoramicDialogConfig::deserialize(Suscan::Object const &conf)
{
  LOAD(fullRange);
  LOAD(rangeMin);
  LOAD(rangeMax);
  LOAD(panRangeMin);
  LOAD(panRangeMax);
  LOAD(lnbFreq);
  LOAD(device);
  LOAD(antenna);
  LOAD(sampRate);
  LOAD(strategy);
  LOAD(partitioning);
  LOAD(palette);

  for (unsigned int i = 0; i < conf.getFieldCount(); ++i)
    if (conf.getFieldByIndex(i).name().substr(0, 5) == "gain.") {
      this->gains[conf.getFieldByIndex(i).name()] =
          conf.get(
            conf.getFieldByIndex(i).name(),
            static_cast<SUFLOAT>(0));
    }
}

Suscan::Object &&
PanoramicDialogConfig::serialize(void)
{
  Suscan::Object obj(SUSCAN_OBJECT_TYPE_OBJECT);

  obj.setClass("PanoramicDialogConfig");

  STORE(fullRange);
  STORE(rangeMin);
  STORE(rangeMax);
  STORE(panRangeMin);
  STORE(panRangeMax);
  STORE(lnbFreq);
  STORE(device);
  STORE(antenna);
  STORE(sampRate);
  STORE(strategy);
  STORE(partitioning);
  STORE(palette);

  for (auto p : this->gains)
    obj.set(p.first, p.second);

  return this->persist(obj);
}

bool
PanoramicDialogConfig::hasGain(
    std::string const &dev,
    std::string const &name) const
{
  std::string fullName = "gain." + dev + "." + name;

  return this->gains.find(fullName) != this->gains.cend();
}

SUFLOAT
PanoramicDialogConfig::getGain(
    std::string const &dev,
    std::string const &name) const
{
  std::string fullName = "gain." + dev + "." + name;

  if (this->gains.find(fullName) == this->gains.cend())
    return 0;

  return this->gains.at(fullName);
}

void
PanoramicDialogConfig::setGain(
    std::string const &dev,
    std::string const &name,
    SUFLOAT val)
{
  std::string fullName = "gain." + dev + "." + name;

  this->gains[fullName] = val;
}

///////////////////////////// PanoramicDialog //////////////////////////////////
PanoramicDialog::PanoramicDialog(QWidget *parent) :
  QDialog(parent),
  ui(new Ui::PanoramicDialog)
{
  ui->setupUi(static_cast<QDialog *>(this));

  this->assertConfig();
  this->setWindowFlags(Qt::Window);
  this->ui->sampleRateSpin->setUnits("sps");

  this->ui->centerLabel->setFixedWidth(
        SuWidgetsHelpers::getWidgetTextWidth(
          this->ui->centerLabel,
          "XXX.XXXXXXXXX XHz"));

  this->ui->bwLabel->setFixedWidth(
        SuWidgetsHelpers::getWidgetTextWidth(
          this->ui->bwLabel,
          "XXX.XXXXXXXXX XHz"));

  this->ui->lnbDoubleSpinBox->setMinimum(-300e9);
  this->ui->lnbDoubleSpinBox->setMaximum(300e9);

  this->ui->waterfall->setUseLBMdrag(true);

  this->connectAll();
}

PanoramicDialog::~PanoramicDialog()
{
  if (this->noGainLabel != nullptr)
    this->noGainLabel->deleteLater();
  delete ui;
}

void
PanoramicDialog::connectAll(void)
{
  connect(
        this->ui->deviceCombo,
        SIGNAL(activated(int)),
        this,
        SLOT(onDeviceChanged(void)));

  connect(
        this->ui->lnbDoubleSpinBox,
        SIGNAL(valueChanged(double)),
        this,
        SLOT(onLnbOffsetChanged(void)));

  connect(
        this->ui->sampleRateSpin,
        SIGNAL(valueChanged(double)),
        this,
        SLOT(onSampleRateSpinChanged(void)));

  connect(
        this->ui->fullRangeCheck,
        SIGNAL(stateChanged(int)),
        this,
        SLOT(onFullRangeChanged(void)));

  connect(
        this->ui->rangeStartSpin,
        SIGNAL(valueChanged(double)),
        this,
        SLOT(onFreqRangeChanged(void)));

  connect(
        this->ui->rangeEndSpin,
        SIGNAL(valueChanged(double)),
        this,
        SLOT(onFreqRangeChanged(void)));

  connect(
        this->ui->scanButton,
        SIGNAL(clicked(bool)),
        this,
        SLOT(onToggleScan(void)));

  connect(
        this->ui->resetButton,
        SIGNAL(clicked(bool)),
        this,
        SIGNAL(reset(void)));

  connect(
        this->ui->waterfall,
        SIGNAL(newFilterFreq(int, int)),
        this,
        SLOT(onNewBandwidth(int, int)));

  connect(
        this->ui->waterfall,
        SIGNAL(newDemodFreq(qint64, qint64)),
        this,
        SLOT(onNewOffset()));

  connect(
        this->ui->waterfall,
        SIGNAL(newZoomLevel(float)),
        this,
        SLOT(onNewZoomLevel(float)));

  connect(
        this->ui->waterfall,
        SIGNAL(newCenterFreq(qint64)),
        this,
        SLOT(onNewCenterFreq(qint64)));

  connect(
        this->ui->rttSpin,
        SIGNAL(valueChanged(int)),
        this,
        SIGNAL(frameSkipChanged(void)));

  connect(
        this->ui->relBwSlider,
        SIGNAL(valueChanged(int)),
        this,
        SIGNAL(relBandwidthChanged(void)));

  connect(
        this->ui->waterfall,
        SIGNAL(pandapterRangeChanged(float, float)),
        this,
        SLOT(onRangeChanged(float, float)));

  connect(
        this->ui->paletteCombo,
        SIGNAL(activated(int)),
        this,
        SLOT(onPaletteChanged(int)));

  connect(
        this->ui->allocationCombo,
        SIGNAL(activated(int)),
        this,
        SLOT(onBandPlanChanged(int)));

  connect(
        this->ui->walkStrategyCombo,
        SIGNAL(currentIndexChanged(int)),
        this,
        SLOT(onStrategyChanged(int)));

  connect(
        this->ui->partitioningCombo,
        SIGNAL(currentIndexChanged(int)),
        this,
        SLOT(onPartitioningChanged(int)));

  connect(
        this->ui->exportButton,
        SIGNAL(clicked(bool)),
        this,
        SLOT(onExport(void)));
}


// The following values are purely experimental
unsigned int
PanoramicDialog::preferredRttMs(Suscan::Source::Device const &dev)
{
  if (dev.getDriver() == "rtlsdr")
    return 60;
  else if (dev.getDriver() == "airspy")
    return 16;
  else if (dev.getDriver() == "hackrf")
    return 10;
  else if (dev.getDriver() == "uhd")
    return 8;

  return 0;
}

void
PanoramicDialog::refreshUi(void)
{
  bool empty = this->deviceMap.size() == 0;
  bool fullRange = this->ui->fullRangeCheck->isChecked();

  this->ui->deviceCombo->setEnabled(!this->running && !empty);
  this->ui->antennaCombo->setEnabled(!this->running && !empty &&
        this->ui->antennaCombo->count());
  this->ui->fullRangeCheck->setEnabled(!this->running && !empty);
  this->ui->rangeEndSpin->setEnabled(!this->running && !empty && !fullRange);
  this->ui->rangeStartSpin->setEnabled(!this->running && !empty && !fullRange);
  this->ui->lnbDoubleSpinBox->setEnabled(!this->running);
  this->ui->scanButton->setChecked(this->running);
  this->ui->sampleRateSpin->setEnabled(!this->running);
}

SUFREQ
PanoramicDialog::getLnbOffset(void) const
{
  return this->ui->lnbDoubleSpinBox->value();
}

SUFREQ
PanoramicDialog::getMinFreq(void) const
{
  return this->ui->rangeStartSpin->value();
}

SUFREQ
PanoramicDialog::getMaxFreq(void) const
{
  return this->ui->rangeEndSpin->value();
}

void
PanoramicDialog::setRunning(bool running)
{
  if (running && !this->running) {
    this->frames = 0;
    this->ui->framesLabel->setText("0");
  } else if (!running && this->running) {
    this->ui->sampleRateSpin->setValue(this->dialogConfig->sampRate);
  }

  this->running = running;
  this->refreshUi();
}

QString
PanoramicDialog::getAntenna(void) const
{
  return this->ui->antennaCombo->currentText();
}

QString
PanoramicDialog::getStrategy(void) const
{
  return this->ui->walkStrategyCombo->currentText();
}

QString
PanoramicDialog::getPartitioning(void) const
{
  return this->ui->partitioningCombo->currentText();
}

float
PanoramicDialog::getGain(QString const &gain) const
{
  for (auto p : this->gainControls)
    if (p->getName() == gain.toStdString())
      return p->getGain();

  return 0;
}

void
PanoramicDialog::setBannedDevice(QString const &desc)
{
  this->bannedDevice = desc;
}

void
PanoramicDialog::setWfRange(qint64 freqStart, qint64 freqEnd)
{
  if (this->fixedFreqMode) {
    qint64 bw = static_cast<qint64>(this->minBwForZoom);

    // In fixed frequency mode we never set the center frequency.
    // That remains fixed. Spectrum is received according to the
    // waterfall's span.
    if (bw != this->currBw) {
      this->ui->waterfall->setSampleRate(bw);
      this->currBw = bw;
    }
  } else {
    qint64 fc = static_cast<qint64>(.5 * (freqEnd + freqStart));
    qint64 bw = static_cast<qint64>(freqEnd - freqStart);

    // In other cases, we must adjust the limits and the bandwidth.
    // When also have to adjust the bandwidth, we must reset the zoom
    // so the sure can keep zooming in the spectrum,

    this->ui->waterfall->setCenterFreq(fc);

    if (bw != this->currBw) {
      qint64 demodBw = bw / 10;
      this->ui->waterfall->setLocked(false);
      this->ui->waterfall->setSampleRate(bw);
      this->ui->waterfall->setDemodRanges(
            -bw / 2,
            0,
            0,
            bw / 2,
            true);


      if (demodBw > 4000000000)
        demodBw = 4000000000;

      this->ui->waterfall->setHiLowCutFrequencies(
            -demodBw / 2,
            demodBw / 2);

      this->ui->waterfall->resetHorizontalZoom();
      this->currBw = bw;
    }
  }
}

void
PanoramicDialog::feed(
    qint64 freqStart,
    qint64 freqEnd,
    float *data,
    size_t size)
{
  if (this->freqStart != freqStart || this->freqEnd != freqEnd) {
    this->freqStart = freqStart;
    this->freqEnd   = freqEnd;

    this->adjustingRange = true;
    this->setWfRange(
          static_cast<qint64>(freqStart),
          static_cast<qint64>(freqEnd));
    this->adjustingRange = false;
  }

  this->saved.set(
        static_cast<qint64>(freqStart),
        static_cast<qint64>(freqEnd),
        data,
        size);

  this->ui->exportButton->setEnabled(true);
  this->ui->waterfall->setNewFftData(data, static_cast<int>(size));

  ++this->frames;
  this->redrawMeasures();
}

void
PanoramicDialog::setColors(ColorConfig const &cfg)
{
  this->ui->waterfall->setFftPlotColor(cfg.spectrumForeground);
  this->ui->waterfall->setFftAxesColor(cfg.spectrumAxes);
  this->ui->waterfall->setFftBgColor(cfg.spectrumBackground);
  this->ui->waterfall->setFftTextColor(cfg.spectrumText);
  this->ui->waterfall->setFilterBoxColor(cfg.filterBox);
}

void
PanoramicDialog::setPaletteGradient(QString const &name)
{
  int index = SigDiggerHelpers::instance()->getPaletteIndex(name.toStdString());
  this->paletteGradient = name;

  if (index >= 0) {
    this->ui->paletteCombo->setCurrentIndex(index);
    this->ui->waterfall->setPalette(
          SigDiggerHelpers::instance()->getPalette(index)->getGradient());
  }
}

SUFLOAT
PanoramicDialog::getPreferredSampleRate(void) const
{
  return this->ui->sampleRateSpin->value();
}

void
PanoramicDialog::setMinBwForZoom(quint64 bw)
{
  this->minBwForZoom = bw;
  this->ui->sampleRateSpin->setValue(static_cast<int>(bw));
}

void
PanoramicDialog::populateDeviceCombo(void)
{
  Suscan::Singleton *sus = Suscan::Singleton::get_instance();

  this->ui->deviceCombo->clear();
  this->deviceMap.clear();

  for (auto i = sus->getFirstDevice(); i != sus->getLastDevice(); ++i) {
    if (i->getMaxFreq() > 0 && i->isAvailable()) {
      std::string name = i->getDesc();
      this->deviceMap[name] = *i;
      this->ui->deviceCombo->addItem(QString::fromStdString(name));
    }
  }

  if (this->deviceMap.size() > 0)
    this->onDeviceChanged();

  this->refreshUi();
}

bool
PanoramicDialog::getSelectedDevice(Suscan::Source::Device &dev) const
{
  std::string name = this->ui->deviceCombo->currentText().toStdString();
  auto p = this->deviceMap.find(name);

  if (p != this->deviceMap.cend()) {
    dev = p->second;
    return true;
  }

  return false;
}

void
PanoramicDialog::adjustRanges(void)
{
  SUFREQ minFreq, maxFreq;

  minFreq = this->ui->rangeStartSpin->value();
  maxFreq = this->ui->rangeEndSpin->value();

  if (this->ui->rangeStartSpin->value() >
      this->ui->rangeEndSpin->value()) {
    auto val = this->ui->rangeStartSpin->value();
    this->ui->rangeStartSpin->setValue(
          this->ui->rangeEndSpin->value());
    this->ui->rangeEndSpin->setValue(val);
  }

  this->ui->waterfall->setFreqUnits(
        getFrequencyUnits(
          static_cast<qint64>(maxFreq)));

  this->ui->waterfall->setSpanFreq(static_cast<qint64>(maxFreq - minFreq));
  this->ui->waterfall->setCenterFreq(static_cast<qint64>(maxFreq + minFreq) / 2);
}

bool
PanoramicDialog::invalidRange(void) const
{
  return fabs(
        this->ui->rangeEndSpin->value() - this->ui->rangeStartSpin->value()) < 1;
}

int
PanoramicDialog::getFrequencyUnits(qint64 freq)
{
  if (freq < 0)
    freq = -freq;

  if (freq < 1000)
    return 1;

  if (freq < 1000000)
    return 1000;

  if (freq < 1000000000)
    return 1000000;

  return 1000000000;
}


void
PanoramicDialog::setRanges(Suscan::Source::Device const &dev)
{
  SUFREQ minFreq = dev.getMinFreq() + this->getLnbOffset();
  SUFREQ maxFreq = dev.getMaxFreq() + this->getLnbOffset();

  // Prevents Waterfall frequencies from overflowing.

  this->ui->rangeStartSpin->setMinimum(minFreq);
  this->ui->rangeStartSpin->setMaximum(maxFreq);
  this->ui->rangeEndSpin->setMinimum(minFreq);
  this->ui->rangeEndSpin->setMaximum(maxFreq);

  if (this->invalidRange() || this->ui->fullRangeCheck->isChecked()) {
    this->ui->rangeStartSpin->setValue(minFreq);
    this->ui->rangeEndSpin->setValue(maxFreq);
  }

  this->adjustRanges();
}

void
PanoramicDialog::saveConfig(void)
{
  Suscan::Source::Device dev;
  if (this->getSelectedDevice(dev)) {
    this->dialogConfig->device = dev.getDesc();
    this->dialogConfig->antenna = this->ui->antennaCombo->currentText().toStdString();
  }

  this->dialogConfig->lnbFreq = this->ui->lnbDoubleSpinBox->value();
  this->dialogConfig->palette = this->paletteGradient.toStdString();
  this->dialogConfig->rangeMin = this->ui->rangeStartSpin->value();
  this->dialogConfig->rangeMax = this->ui->rangeEndSpin->value();

  this->dialogConfig->strategy =
      this->ui->walkStrategyCombo->currentText().toStdString();

  this->dialogConfig->partitioning =
      this->ui->partitioningCombo->currentText().toStdString();

  this->dialogConfig->fullRange = this->ui->fullRangeCheck->isChecked();
}

FrequencyBand
PanoramicDialog::deserializeFrequencyBand(Suscan::Object const &obj)
{
  FrequencyBand band;

  band.min = static_cast<qint64>(obj.get("min", 0.f));
  band.max = static_cast<qint64>(obj.get("max", 0.f));
  band.primary = obj.get("primary", std::string());
  band.secondary = obj.get("secondary", std::string());
  band.footnotes = obj.get("footnotes", std::string());

  band.color.setNamedColor(
        QString::fromStdString(obj.get("color", std::string("#1f1f1f"))));

  return band;
}

void
PanoramicDialog::deserializeFATs(void)
{
  if (this->FATs.size() == 0) {
    Suscan::Singleton *sus = Suscan::Singleton::get_instance();
    Suscan::Object bands;
    unsigned int i, count, ndx = 0;

    for (auto p = sus->getFirstFAT();
         p != sus->getLastFAT();
         p++) {
      this->FATs.resize(ndx + 1);
      this->FATs[ndx] = new FrequencyAllocationTable(p->getField("name").value());
      bands = p->getField("bands");

      SU_ATTEMPT(bands.getType() == SUSCAN_OBJECT_TYPE_SET);

      count = bands.length();

      for (i = 0; i < count; ++i) {
        try {
          this->FATs[ndx]->pushBand(deserializeFrequencyBand(bands[i]));
        } catch (Suscan::Exception &) {
        }
      }

      ++ndx;
    }
  }

  if (this->ui->allocationCombo->count() == 0) {
    this->ui->allocationCombo->insertItem(
          0,
          "(No bandplan)",
          QVariant::fromValue(-1));

    for (unsigned i = 0; i < this->FATs.size(); ++i)
      this->ui->allocationCombo->insertItem(
          static_cast<int>(i + 1),
          QString::fromStdString(this->FATs[i]->getName()),
          QVariant::fromValue(static_cast<int>(i)));
  }
}

void
PanoramicDialog::run(void)
{
  this->populateDeviceCombo();
  this->deserializeFATs();
  this->exec();
  this->saveConfig();
  this->ui->scanButton->setChecked(false);
  this->onToggleScan();
  emit stop();
}

void
PanoramicDialog::redrawMeasures(void)
{
  this->demodFreq = static_cast<qint64>(
        this->ui->waterfall->getFilterOffset() +
        .5 * (this->freqStart + this->freqEnd));

  this->ui->centerLabel->setText(
        SuWidgetsHelpers::formatQuantity(
          static_cast<qreal>(
            this->ui->waterfall->getFilterOffset() +
            .5 * (this->freqStart + this->freqEnd)),
          6,
          "Hz"));

  this->ui->bwLabel->setText(
        SuWidgetsHelpers::formatQuantity(
          static_cast<qreal>(this->ui->waterfall->getFilterBw()),
          6,
          "Hz"));

  this->ui->framesLabel->setText(QString::number(this->frames));
}

unsigned int
PanoramicDialog::getRttMs(void) const
{
  return static_cast<unsigned int>(this->ui->rttSpin->value());
}

float
PanoramicDialog::getRelBw(void) const
{
  return this->ui->relBwSlider->value() / 100.f;
}

DeviceGain *
PanoramicDialog::lookupGain(std::string const &name)
{
  // Why is this? Use a map instead.
  for (auto p = this->gainControls.begin();
       p != this->gainControls.end();
       ++p) {
    if ((*p)->getName() == name)
      return *p;
  }

  return nullptr;
}

void
PanoramicDialog::clearGains(void)
{
  int i, len;

  len = static_cast<int>(this->gainControls.size());

  if (len == 0) {
    QLayoutItem *item = this->ui->gainGridLayout->takeAt(0);
    delete item;

    if (this->noGainLabel != nullptr) {
      this->noGainLabel->deleteLater();
      this->noGainLabel = nullptr;
    }
  } else {
    for (i = 0; i < len; ++i) {
      QLayoutItem *item = this->ui->gainGridLayout->takeAt(0);
      if (item != nullptr)
        delete item;

      // This is what C++ is for.
      this->gainControls[static_cast<unsigned long>(i)]->setVisible(false);
      this->gainControls[static_cast<unsigned long>(i)]->deleteLater();
    }

    QLayoutItem *item = this->ui->gainGridLayout->takeAt(0);
    if (item != nullptr)
      delete item;

    this->gainControls.clear();
  }
}

void
PanoramicDialog::refreshGains(Suscan::Source::Device &device)
{
  DeviceGain *gain = nullptr;

  this->clearGains();

  for (auto p = device.getFirstGain();
       p != device.getLastGain();
       ++p) {
    gain = new DeviceGain(nullptr, *p);
    this->gainControls.push_back(gain);
    this->ui->gainGridLayout->addWidget(
          gain,
          static_cast<int>(this->gainControls.size() - 1),
          0,
          1,
          1);

    connect(
          gain,
          SIGNAL(gainChanged(QString, float)),
          this,
          SLOT(onGainChanged(QString, float)));

    if (this->dialogConfig->hasGain(device.getDriver(), p->getName()))
      gain->setGain(this->dialogConfig->getGain(device.getDriver(), p->getName()));
    else
      gain->setGain(p->getDefault());
  }

  if (this->gainControls.size() == 0) {
    this->ui->gainGridLayout->addWidget(
        this->noGainLabel = new QLabel("(device has no gains)"),
        0,
        0,
        Qt::AlignCenter | Qt::AlignVCenter);
  } else {
    this->ui->gainGridLayout->addItem(
          new QSpacerItem(40, 20, QSizePolicy::Minimum, QSizePolicy::Minimum),
          static_cast<int>(this->gainControls.size()),
          0);
  }
}

// Overriden methods
Suscan::Serializable *
PanoramicDialog::allocConfig(void)
{
  return this->dialogConfig = new PanoramicDialogConfig();
}

void
PanoramicDialog::applyConfig(void)
{
  SigDiggerHelpers::instance()->populatePaletteCombo(this->ui->paletteCombo);

  this->setPaletteGradient(QString::fromStdString(this->dialogConfig->palette));
  this->ui->lnbDoubleSpinBox->setValue(
        static_cast<SUFREQ>(this->dialogConfig->lnbFreq));
  this->ui->rangeStartSpin->setValue(this->dialogConfig->rangeMin);
  this->ui->rangeEndSpin->setValue(this->dialogConfig->rangeMax);
  this->ui->fullRangeCheck->setChecked(this->dialogConfig->fullRange);
  this->ui->sampleRateSpin->setValue(this->dialogConfig->sampRate);
  this->ui->waterfall->setPandapterRange(
        this->dialogConfig->panRangeMin,
        this->dialogConfig->panRangeMax);
  this->ui->waterfall->setWaterfallRange(
        this->dialogConfig->panRangeMin,
        this->dialogConfig->panRangeMax);
  this->ui->walkStrategyCombo->setCurrentText(QString::fromStdString(
        this->dialogConfig->strategy));
  this->ui->partitioningCombo->setCurrentText(QString::fromStdString(
        this->dialogConfig->partitioning));
  this->ui->deviceCombo->setCurrentText(QString::fromStdString(
        this->dialogConfig->device));
  this->onDeviceChanged();
  this->ui->antennaCombo->setCurrentText(QString::fromStdString(
        this->dialogConfig->antenna));
}

////////////////////////////// Slots //////////////////////////////////////

void
PanoramicDialog::onDeviceChanged(void)
{
  Suscan::Source::Device dev;

  if (this->getSelectedDevice(dev)) {
    unsigned int rtt = preferredRttMs(dev);
    this->setRanges(dev);
    this->refreshGains(dev);
    if (rtt != 0)
      this->ui->rttSpin->setValue(static_cast<int>(rtt));
    if (this->ui->fullRangeCheck->isChecked()) {
      this->ui->rangeStartSpin->setValue(dev.getMinFreq() + this->getLnbOffset());
      this->ui->rangeEndSpin->setValue(dev.getMaxFreq() + this->getLnbOffset());
    }

    int curAntennaIndex = this->ui->antennaCombo->currentIndex();
    this->ui->antennaCombo->clear();
    for (auto i = dev.getFirstAntenna(); i != dev.getLastAntenna(); i++) {
      this->ui->antennaCombo->addItem(QString::fromStdString(*i));
    }
    int antennaCount = this->ui->antennaCombo->count();
    this->ui->antennaCombo->setEnabled(antennaCount > 0);
    if (curAntennaIndex < antennaCount && curAntennaIndex >= 0)
      this->ui->antennaCombo->setCurrentIndex(curAntennaIndex);
  } else {
    this->clearGains();
  }

  this->adjustRanges();
}

void
PanoramicDialog::onFullRangeChanged(void)
{
  Suscan::Source::Device dev;
  bool checked = this->ui->fullRangeCheck->isChecked();

  if (this->getSelectedDevice(dev)) {
    if (checked) {
      this->ui->rangeStartSpin->setValue(dev.getMinFreq() + this->getLnbOffset());
      this->ui->rangeEndSpin->setValue(dev.getMaxFreq() + this->getLnbOffset());
    }
  }

  this->refreshUi();
}

void
PanoramicDialog::onFreqRangeChanged(void)
{
  this->adjustRanges();
}

void
PanoramicDialog::onToggleScan(void)
{
  if (this->ui->scanButton->isChecked()) {
    Suscan::Source::Device dev;
    this->getSelectedDevice(dev);

    if (this->bannedDevice.length() > 0
        && dev.getDesc() == this->bannedDevice.toStdString()) {
      (void)  QMessageBox::critical(
            this,
            "Panoramic spectrum error error",
            "Scan cannot start because the selected device is in use by the main window.",
            QMessageBox::Ok);
      this->ui->scanButton->setChecked(false);
    } else {
      emit start();
    }
  } else {
    emit stop();
  }

  this->ui->waterfall->setRunningState(this->ui->scanButton->isChecked());
  this->ui->scanButton->setText(
        this->ui->scanButton->isChecked()
        ? "Stop"
        : "Start scan");

}

void
PanoramicDialog::onNewZoomLevel(float)
{
  qint64 min, max;
  qint64 fc =
        this->ui->waterfall->getCenterFreq()
        + this->ui->waterfall->getFftCenterFreq();
  qint64 span = static_cast<qint64>(this->ui->waterfall->getSpanFreq());
  bool adjLeft = false;
  bool adjRight = false;

  if (!this->adjustingRange) {
    this->adjustingRange = true;

    min = fc - span / 2;
    max = fc + span / 2;

    if (min < this->getMinFreq() && max <= this->getMaxFreq()) {
      // Too much zooming on the left. Reinject it to the max
      qint64 extra = static_cast<qint64>(this->getMinFreq()) - min;
      min += extra;
      max += extra;
      adjLeft = adjRight = true;
    } else if (min >= this->getMinFreq() && max > this->getMaxFreq()) {
      // Too much zooming on the right. Reinject it to the max
      qint64 extra = max - static_cast<qint64>(this->getMaxFreq());
      min -= extra;
      max -= extra;
      adjLeft = adjRight = true;
    }

    if (min < this->getMinFreq()) {
      min = static_cast<qint64>(this->getMinFreq());
      adjLeft = true;
    }

    if (max > this->getMaxFreq()) {
      max = static_cast<qint64>(this->getMaxFreq());
      adjRight = true;
    }

    if (adjLeft && adjRight)
      this->ui->waterfall->resetHorizontalZoom();

    this->fixedFreqMode = max - min <= this->minBwForZoom * this->getRelBw();

    if (this->fixedFreqMode) {
      fc = this->ui->waterfall->getCenterFreq();
      min = fc - span / 2;
      max = fc + span / 2;
    }

    this->setWfRange(min, max);
    this->adjustingRange = false;

    emit detailChanged(min, max, this->fixedFreqMode);
  }
}

void
PanoramicDialog::onRangeChanged(float min, float max)
{
  this->dialogConfig->panRangeMin = min;
  this->dialogConfig->panRangeMax = max;
  this->ui->waterfall->setWaterfallRange(min, max);
}

void
PanoramicDialog::onNewOffset(void)
{
  this->redrawMeasures();
}

void
PanoramicDialog::onNewBandwidth(int, int)
{
  this->redrawMeasures();
}

void
PanoramicDialog::onNewCenterFreq(qint64 freq)
{
  qint64 span = this->currBw;
  qint64 min = freq - span / 2;
  qint64 max = freq + span / 2;
  bool leftBorder = false;
  bool rightBorder = false;

  if (min <= this->getMinFreq()) {
    leftBorder = true;
    min = static_cast<qint64>(this->getMinFreq());
  }

  if (max >= this->getMaxFreq()) {
    rightBorder = true;
    max = static_cast<qint64>(this->getMaxFreq());
  }

  if (rightBorder || leftBorder) {
    if (leftBorder && !rightBorder) {
      max = min + span;
    } else if (rightBorder && !leftBorder) {
      min = max - span;
    }

    this->ui->waterfall->setCenterFreq(
        static_cast<qint64>(.5 * (max + min)));
  }

  emit detailChanged(min, max, this->fixedFreqMode);
}

void
PanoramicDialog::onPaletteChanged(int)
{
  this->setPaletteGradient(this->ui->paletteCombo->currentText());
}

void
PanoramicDialog::onStrategyChanged(int)
{
  emit strategyChanged(this->ui->walkStrategyCombo->currentText());
}

void
PanoramicDialog::onPartitioningChanged(int)
{
  emit partitioningChanged(this->ui->partitioningCombo->currentText());
}

void
PanoramicDialog::onLnbOffsetChanged(void)
{
  Suscan::Source::Device dev;

  if (this->getSelectedDevice(dev))
    this->setRanges(dev);
}

void
PanoramicDialog::onExport(void)
{
  bool done = false;

  do {
    QFileDialog dialog(this);

    dialog.setFileMode(QFileDialog::FileMode::AnyFile);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setWindowTitle(QString("Save panoramic spectrum"));
    dialog.setNameFilter(QString("MATLAB/Octave file (*.m)"));

    if (dialog.exec()) {
      QString path = dialog.selectedFiles().first();

        if (!this->saved.exportToFile(path)) {
          QMessageBox::warning(
                this,
                "Cannot open file",
                "Cannote save file in the specified location. Please choose "
                "a different location and try again.",
                QMessageBox::Ok);
        } else {
          done = true;
        }
    } else {
      done = true;
    }
  } while (!done);
}

void
PanoramicDialog::onBandPlanChanged(int)
{
  int val = this->ui->allocationCombo->currentData().value<int>();

  if (this->currentFAT.size() > 0)
    this->ui->waterfall->removeFAT(this->currentFAT);

  if (val >= 0) {
    this->ui->waterfall->setFATsVisible(true);
    this->ui->waterfall->pushFAT(this->FATs[static_cast<unsigned>(val)]);
    this->currentFAT = this->FATs[static_cast<unsigned>(val)]->getName();
  } else {
    this->ui->waterfall->setFATsVisible(false);
    this->currentFAT = "";
  }
}

void
PanoramicDialog::onGainChanged(QString name, float val)
{
  Suscan::Source::Device dev;

  if (this->getSelectedDevice(dev))
    this->dialogConfig->setGain(dev.getDriver(), name.toStdString(), val);

  emit gainChanged(name, val);
}

void
PanoramicDialog::onSampleRateSpinChanged(void)
{
  if (!this->running)
    this->dialogConfig->sampRate = static_cast<int>(
        this->ui->sampleRateSpin->value());
}
