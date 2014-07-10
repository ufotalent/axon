#pragma once

#define ENSURE_RETURN_ZERO(x) \
    if (x) \
        throw std::runtime_error("unexpected return status");
