#pragma once
#include <unistd.h>
#include "Uniqued.h"

namespace Toastbox {

using FileDescriptor = Uniqued<int, ::close>;

}
