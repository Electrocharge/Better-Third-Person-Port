#pragma once

#include <pl/Mod.hpp>

namespace nexcaise {

class FreeLook {
  public:
    static FreeLook &getInstance();

    FreeLook();

    [[nodiscard]] ll::mod::NativeMod &getSelf() const { return mSelf; }

    bool load();
    bool enable();
    bool disable();
    bool unload();

  private:
    ll::mod::NativeMod &mSelf;
};

} // namespace nexcaise
