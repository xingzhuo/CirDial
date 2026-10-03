#pragma once
#include <cmath>
#include <algorithm>
namespace dial {
struct Position { double x; double y; };
inline Position ToNative(double x,double y,unsigned orientation) {
    switch(orientation) {
    case 1: return {1-y,x};
    case 2: return {1-x,1-y};
    case 3: return {y,1-x};
    default: return {x,y};
    }
}
inline unsigned short Normalize(double value) {
    return static_cast<unsigned short>(std::lround(std::clamp(value,0.0,1.0)*32767.0));
}
constexpr double Pi = 3.14159265358979323846;
inline bool StartDrag(long dx,long dy,unsigned dpi) {
    return std::hypot(static_cast<double>(dx),static_cast<double>(dy))>8.0*dpi/96.0;
}
inline double Angle(double x, double y) { return std::atan2(y, x) * 180.0 / Pi; }
inline double Delta(double previous, double current) {
    double value = std::remainder(current - previous, 360.0);
    return value;
}
// Distinguish an initial arc from a centre drag before committing the gesture.
inline bool InnerArc(double sx,double sy,double x,double y) {
    double radius=std::hypot(sx,sy);
    return radius>=6 && std::abs(std::hypot(x,y)-radius)<=std::max(3.0,radius*.25)
        && std::abs(Delta(Angle(sx,sy),Angle(x,y)))>=12;
}
class Accumulator {
    double remainder = 0;
public:
    void Reset() { remainder = 0; }
    long Add(double degrees, double sensitivity, bool reverse) {
        remainder += degrees * 10.0 * sensitivity * (reverse ? -1 : 1);
        long value = static_cast<long>(std::trunc(remainder));
        remainder -= value;
        return value;
    }
};
}
