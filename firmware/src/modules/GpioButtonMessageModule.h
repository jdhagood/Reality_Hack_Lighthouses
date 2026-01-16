#pragma once

#include "concurrency/OSThread.h"

class GpioButtonMessageModule : public concurrency::OSThread
{
  public:
    GpioButtonMessageModule();
    int32_t runOnce() override;
};
