#pragma once

#define ENSURE_RETURN_ZERO(x) \
    if (x) \
        throw std::runtime_error("unexpected return status");

#define ENSURE_RETURN_ZERO_PERROR(x) \
    if (x) { \
        perror("unexpected return status"); \
        throw std::runtime_error("unexpected return status"); \
    }
