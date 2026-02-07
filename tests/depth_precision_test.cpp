// Depth Precision Test
// Verifies standard and reversed-Z projection mapping, plus center ray consistency.

#include "../src/camera/earth_camera.h"
#include <cmath>
#include <iostream>

using namespace earth;

namespace {

bool Near(double a, double b, double eps = 1e-6) {
    return std::fabs(a - b) <= eps;
}

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return false;
    }
    return true;
}

double ProjectToNdcZ(const glm::dmat4& proj, double viewZ) {
    glm::dvec4 clip = proj * glm::dvec4(0.0, 0.0, viewZ, 1.0);
    if (std::fabs(clip.w) < 1e-12) {
        return 0.0;
    }
    return clip.z / clip.w;
}

} // namespace

int main() {
    int failed = 0;

    PerspectiveCamera cam;
    cam.SetFov(60.0);
    cam.SetAspectRatio(16.0 / 9.0);
    cam.SetNearFar(0.5, 20000.0);

    // Standard depth: near -> -1, far -> +1 (OpenGL NDC)
    cam.SetReverseZEnabled(false);
    glm::dmat4 projStandard = cam.GetProjectionMatrix();
    double ndcNear = ProjectToNdcZ(projStandard, -0.5);
    double ndcFar = ProjectToNdcZ(projStandard, -20000.0);
    failed += !Expect(Near(ndcNear, -1.0, 1e-5), "standard projection near plane should map to -1");
    failed += !Expect(Near(ndcFar, 1.0, 1e-5), "standard projection far plane should map to +1");

    // Reversed-Z depth: near -> +1, far -> -1 (OpenGL NDC)
    cam.SetReverseZEnabled(true);
    glm::dmat4 projReversed = cam.GetProjectionMatrix();
    double ndcNearRev = ProjectToNdcZ(projReversed, -0.5);
    double ndcFarRev = ProjectToNdcZ(projReversed, -20000.0);
    failed += !Expect(Near(ndcNearRev, 1.0, 1e-5), "reversed-Z projection near plane should map to +1");
    failed += !Expect(Near(ndcFarRev, -1.0, 1e-5), "reversed-Z projection far plane should map to -1");

    // Screen-center picking ray direction should remain stable across modes.
    glm::dvec3 oStandard, dStandard;
    cam.SetReverseZEnabled(false);
    cam.GetRay(640.0, 360.0, 1280, 720, oStandard, dStandard);

    glm::dvec3 oReversed, dReversed;
    cam.SetReverseZEnabled(true);
    cam.GetRay(640.0, 360.0, 1280, 720, oReversed, dReversed);

    double dirDot = glm::dot(glm::normalize(dStandard), glm::normalize(dReversed));
    failed += !Expect(dirDot > 0.9999, "center ray direction should match between standard and reversed-Z");

    if (failed == 0) {
        std::cout << "DepthPrecisionTest PASSED\n";
        return 0;
    }

    std::cerr << "DepthPrecisionTest FAILED (" << failed << " checks failed)\n";
    return 1;
}
