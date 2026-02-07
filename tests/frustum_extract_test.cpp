// Frustum extraction regression test.
// Validates GLM column-major extraction (row3 +/- rowN).

#include "../src/math/frustum.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

using namespace globe;

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return false;
    }
    return true;
}

} // namespace

int main() {
    int failed = 0;

    {
        glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 1.0f, 100.0f);
        glm::mat4 view = glm::mat4(1.0f);
        Frustum frustum;
        frustum.Extract(proj * view);

        failed += !Expect(frustum.IsSphereVisible(glm::vec3(0.0f, 0.0f, -3.0f), 0.1f),
                          "forward sphere should be visible");
        failed += !Expect(!frustum.IsSphereVisible(glm::vec3(0.0f, 0.0f, 3.0f), 0.1f),
                          "behind-camera sphere should be culled");
        failed += !Expect(!frustum.IsSphereVisible(glm::vec3(100.0f, 0.0f, -3.0f), 0.1f),
                          "far off-axis sphere should be culled");
    }

    {
        glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.5f, 200.0f);
        glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 5.0f),
                                     glm::vec3(0.0f, 0.0f, 0.0f),
                                     glm::vec3(0.0f, 1.0f, 0.0f));
        Frustum frustum;
        frustum.Extract(proj * view);

        failed += !Expect(frustum.IsSphereVisible(glm::vec3(0.0f, 0.0f, 0.0f), 0.1f),
                          "origin should be visible from translated camera");
        failed += !Expect(!frustum.IsSphereVisible(glm::vec3(0.0f, 0.0f, 30.0f), 0.1f),
                          "point behind camera should be culled after translation");
    }

    if (failed == 0) {
        std::cout << "FrustumExtractTest PASSED\n";
        return 0;
    }

    std::cerr << "FrustumExtractTest FAILED (" << failed << " checks failed)\n";
    return 1;
}

