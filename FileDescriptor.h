#pragma once
#include "Uniqued.h"

namespace Toastbox {

using FileDescriptor = Uniqued<int, ::close>;

}
