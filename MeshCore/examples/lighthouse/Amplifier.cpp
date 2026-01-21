#include "Amplifier.h"

#ifdef ESP32
#include <driver/i2s.h>

namespace {
constexpr i2s_port_t kI2sPort = I2S_NUM_0;
}

Amplifier::Amplifier()
  : _initialized(false),
    _sample_rate(AMP_SAMPLE_RATE) {
}

void Amplifier::begin(uint32_t sample_rate) {
  _sample_rate = sample_rate;

  i2s_config_t config = {};
  config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  config.sample_rate = _sample_rate;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  config.communication_format = I2S_COMM_FORMAT_I2S_MSB;
  config.intr_alloc_flags = 0;
  config.dma_buf_count = 4;
  config.dma_buf_len = 256;
  config.use_apll = false;
  config.tx_desc_auto_clear = true;
  config.fixed_mclk = 0;

  i2s_pin_config_t pins = {};
  pins.bck_io_num = AMP_I2S_BCLK;
  pins.ws_io_num = AMP_I2S_WS;
  pins.data_out_num = AMP_I2S_DOUT;
  pins.data_in_num = I2S_PIN_NO_CHANGE;

  i2s_driver_install(kI2sPort, &config, 0, nullptr);
  i2s_set_pin(kI2sPort, &pins);
  i2s_zero_dma_buffer(kI2sPort);
  _initialized = true;
}

void Amplifier::end() {
  if (!_initialized) {
    return;
  }
  i2s_driver_uninstall(kI2sPort);
  _initialized = false;
}

bool Amplifier::isInitialized() const {
  return _initialized;
}

size_t Amplifier::writeBytes(const void *data, size_t bytes) {
  size_t written = 0;
  if (!_initialized || bytes == 0) {
    return 0;
  }
  i2s_write(kI2sPort, data, bytes, &written, portMAX_DELAY);
  return written;
}

size_t Amplifier::writeSamples(const int16_t *interleaved_stereo, size_t frames) {
  if (!_initialized || interleaved_stereo == nullptr || frames == 0) {
    return 0;
  }
  size_t bytes = frames * sizeof(int16_t) * 2;
  return writeBytes(interleaved_stereo, bytes) / (sizeof(int16_t) * 2);
}

size_t Amplifier::writeMonoSamples(const int16_t *mono, size_t frames) {
  if (!_initialized || mono == nullptr || frames == 0) {
    return 0;
  }

  static int16_t stereo_buf[256 * 2];
  size_t remaining = frames;
  size_t offset = 0;
  size_t written_frames = 0;

  while (remaining > 0) {
    size_t batch = remaining > 256 ? 256 : remaining;
    for (size_t i = 0; i < batch; ++i) {
      int16_t sample = mono[offset + i];
      stereo_buf[i * 2] = sample;
      stereo_buf[i * 2 + 1] = sample;
    }
    size_t bytes = batch * sizeof(int16_t) * 2;
    size_t bytes_written = writeBytes(stereo_buf, bytes);
    written_frames += bytes_written / (sizeof(int16_t) * 2);
    if (bytes_written < bytes) {
      break;
    }
    offset += batch;
    remaining -= batch;
  }

  return written_frames;
}

#else

Amplifier::Amplifier() {}
void Amplifier::begin(uint32_t) {}
void Amplifier::end() {}
bool Amplifier::isInitialized() const { return false; }
size_t Amplifier::writeSamples(const int16_t *, size_t) { return 0; }
size_t Amplifier::writeMonoSamples(const int16_t *, size_t) { return 0; }

#endif
