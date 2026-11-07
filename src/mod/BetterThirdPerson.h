#pragma once

#include <pl/Mod.hpp>

namespace btp {

class BetterThirdPerson {
  public:
    static BetterThirdPerson &getInstance();

    BetterThirdPerson();

    [[nodiscard]] ll::mod::NativeMod &getSelf() const { return mSelf; }

    bool load();
    bool enable();
    bool disable();
    bool unload();

  private:
    ll::mod::NativeMod &mSelf;
};

} // namespace btp
