#pragma once

#include "_HeaderOutputPredefine.h"

namespace Bedrock::Safety {

class RedactableString {
public:
    ::std::string                  mUnredactedString;
    ::std::optional<::std::string> mRedactedString;
};

} // namespace Bedrock::Safety
