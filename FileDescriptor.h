#pragma once
#include "Uniqued.h"

using FileDescriptor = Uniqued<int, ::close>;
