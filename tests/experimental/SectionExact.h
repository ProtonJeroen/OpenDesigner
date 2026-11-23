#pragma once
#include "SectionVerifier.h"
#include <bit>
#include <cstdint>
#include <limits>
#include <cmath>
#include <stdexcept>
namespace SectionExperiment::Arithmetic {
using I=boost::multiprecision::cpp_int;
inline Rational exact(double d) {
    static_assert(std::numeric_limits<double>::is_iec559 && std::numeric_limits<double>::digits==53 && std::numeric_limits<double>::max_exponent==1024);
    if(!std::isfinite(d))throw std::runtime_error("nonfinite input");
    const auto bits=std::bit_cast<uint64_t>(d);const auto exponent=(bits>>52)&2047;
    I mantissa=bits&((uint64_t(1)<<52)-1);
    if(exponent)mantissa+=I(1)<<52;
    int shift=exponent?int(exponent)-1023-52:-1074;
    if(bits>>63)mantissa=-mantissa;
    if(shift>=0)return Rational(I(mantissa<<shift));
    return Rational(mantissa,I(I(1)<<(-shift)));
}
}
