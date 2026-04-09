#include "Physics3D/misc/profiling.h"

#include <array>
#include <iomanip>
#include <iostream>

#include "math/linalg/vec.h"

using namespace P3D;

enum class DemoProcess {
    DOTS,
    CROSS,
    OTHER,
    COUNT
};

const char* demoLabels[] = {
    "BarycentricDots",
    "BarycentricCross",
    "Other"
};

Vec3f calcBarycentricCoordinatesDots(Vec3f a, Vec3f b, Vec3f c, Vec3f p)
{
    // 计算重心坐标 u v w
    // 采用克拉默法则 避免了叉乘运算 更为高效
    Vec3f v0 = b - a, v1 = c - a, v2 = p - a;
    float d00 = v0 * v0;
    float d01 = v0 * v1;
    float d11 = v1 * v1;
    float d20 = v2 * v0;
    float d21 = v2 * v1;
    float denom = d00 * d11 - d01 * d01;

    float v = (d11 * d20 - d01 * d21) / denom;
    float w = (d00 * d21 - d01 * d20) / denom;
    float u = 1.0f - v - w;

    return Vec3f{u, v, w};
}

Vec3f calcBarycentricCoordinatesCross(Vec3f a, Vec3f b, Vec3f c, Vec3f p)
{
    Vec3f n = (b - a) % (c - a);
    Vec3f bary;

    // The area of a triangle is
    float areaABC = n * ((b - a) % (c - a));
    float areaPBC = n * ((b - p) % (c - p));
    float areaPCA = n * ((c - p) % (a - p));

    bary.x = areaPBC / areaABC; // alpha
    bary.y = areaPCA / areaABC; // beta
    bary.z = 1.0f - bary.y - bary.x; // gamma

    return bary;
}

BreakdownAverageProfiler<DemoProcess> demoProfiler(demoLabels, 120);
HistoricTally<long long, DemoProcess> demoCounter(demoLabels, 120);

struct BaryInput {
    Vec3f a;
    Vec3f b;
    Vec3f c;
    Vec3f p;
};

static constexpr std::array<BaryInput, 8> kInputs = {{
    {{0.0f, 0.0f, 0.0f}, {1.0f, 0.2f, 0.0f}, {0.1f, 1.0f, 0.3f}, {0.2f, 0.3f, 0.1f}},
    {{1.2f, 2.1f, 0.5f}, {2.0f, 2.2f, 0.8f}, {1.4f, 3.0f, 1.2f}, {1.7f, 2.4f, 0.9f}},
    {{-0.3f, 0.4f, 1.2f}, {0.8f, 0.7f, 1.5f}, {0.1f, 1.5f, 1.9f}, {0.2f, 0.8f, 1.4f}},
    {{3.0f, 1.0f, 2.0f}, {3.8f, 1.4f, 2.6f}, {3.1f, 2.0f, 2.5f}, {3.3f, 1.5f, 2.2f}},
    {{-2.0f, -1.0f, 0.5f}, {-1.2f, -0.7f, 1.1f}, {-1.9f, -0.1f, 0.8f}, {-1.6f, -0.6f, 0.9f}},
    {{0.6f, -1.5f, 2.2f}, {1.3f, -1.2f, 2.6f}, {0.7f, -0.6f, 2.9f}, {0.9f, -1.0f, 2.5f}},
    {{2.2f, 0.1f, -0.7f}, {2.9f, 0.6f, -0.1f}, {2.4f, 1.3f, -0.4f}, {2.5f, 0.7f, -0.3f}},
    {{-1.0f, 2.3f, 1.7f}, {-0.2f, 2.6f, 2.2f}, {-0.7f, 3.1f, 2.0f}, {-0.6f, 2.7f, 1.9f}}
}};

static constexpr int kInnerRepeats = 4000;
volatile float sink = 0.0f;

void runOneTick() {
    Vec3f dotsAccum{0.0f, 0.0f, 0.0f};
    Vec3f crossAccum{0.0f, 0.0f, 0.0f};

    demoProfiler.mark(DemoProcess::DOTS);
    for(int repeat = 0; repeat < kInnerRepeats; repeat++) {
        for(const BaryInput& input : kInputs) {
            dotsAccum += calcBarycentricCoordinatesDots(input.a, input.b, input.c, input.p);
        }
    }
    demoCounter.addToTally(DemoProcess::DOTS, static_cast<long long>(kInnerRepeats * kInputs.size()));

    demoProfiler.mark(DemoProcess::CROSS);
    for(int repeat = 0; repeat < kInnerRepeats; repeat++) {
        for(const BaryInput& input : kInputs) {
            crossAccum += calcBarycentricCoordinatesCross(input.a, input.b, input.c, input.p);
        }
    }
    demoCounter.addToTally(DemoProcess::CROSS, static_cast<long long>(kInnerRepeats * kInputs.size()));

    if (dotsAccum != crossAccum)
        std::cout << "Warning: Accumulated results differ! This may indicate a bug in the implementations.\n";

    demoProfiler.mark(DemoProcess::OTHER);
    // Keep accumulated results observable so optimizer cannot remove benchmarked calls.
    sink += dotsAccum.x + dotsAccum.y + dotsAccum.z;
    sink += crossAccum.x + crossAccum.y + crossAccum.z;

    demoProfiler.end();
    demoCounter.nextTally();
}

void printReport() {
    auto avgTime = demoProfiler.history.avg();
    auto avgCalls = demoCounter.history.avg();
    const double avgTickNs = static_cast<double>(avgTime.sum().count());

    std::cout << std::fixed << std::setprecision(2);
    // Ticks Per Second（每秒 tick 数），也就是每秒能跑多少次 runOneTick()
    std::cout << "Avg TPS: " << demoProfiler.getAvgTPS() << "\n";

    for(size_t i = 0; i < static_cast<size_t>(DemoProcess::COUNT); i++) {
        const double avgNs = static_cast<double>(avgTime[i].count());
        const long long calls = avgCalls[i];
        const double nsPerCall = calls > 0 ? avgNs / static_cast<double>(calls) : 0.0;
        const double ratio = avgTickNs > 0.0 ? (avgNs / avgTickNs) * 100.0 : 0.0;

        std::cout << demoLabels[i] << ": avg=" << avgNs * 1e-6 << " ms/tick"
                  << ", calls=" << calls
                  << ", " << nsPerCall << " ns/call"
                  << ", ratio=" << ratio << "%\n";
    }

    std::cout << "Sink: " << sink << "\n";
}

int main() {
    for(int i = 0; i < 20; i++) {
        runOneTick();
    }

    printReport();
    return 0;
}
