// Smoke report parser + ownership classifier regression test.

#include <iostream>
#include <regex>
#include <string>

namespace {

struct SmokeMetrics {
    int missingMax = -1;
    int placeholderMax = -1;
    int fetchPendingMax = -1;
    int fetchDecodeMax = -1;
    int fetchActiveMax = -1;
    int quorumDownMax = -1;
    double seamGapMaxPeakM = -1.0;
    double seamGapMaxEndM = -1.0;
    int cliffMax = -1;
    int cliffEnd = -1;
    int placeholderEnd = -1;
    int demFlatEnd = -1;
    int demPendingEnd = -1;
    int leafUnderflowDelta = -1;
};

struct OwnershipHints {
    bool renderSetOrQuorum = false;
    bool demEdgePackOrStitch = false;
    bool demPendingConvergence = false;
};

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return false;
    }
    return true;
}

bool ParseSmokeMetricsLine(const std::string& line, SmokeMetrics& out) {
    static const std::regex kMetrics(
        R"(Metrics:\s+missing\(max\)=(\d+)\s+placeholder\(max\)=(\d+)\s+fetch\(p/d/a max\)=(\d+)\/(\d+)\/(\d+)\s+quorumDown\(max\)=(\d+)\s+seamGapMax\(max/end\)=([0-9]+(?:\.[0-9]+)?)\/([0-9]+(?:\.[0-9]+)?)m\s+cliffs\(max/end\)=(\d+)\/(\d+)\s+placeholder\(end\)=(\d+)\s+demFlat\(end\)=(\d+)\s+demPending\(end\)=(\d+)\s+leafUnderflow\(delta\)=(\d+))"
    );

    std::smatch m;
    if (!std::regex_search(line, m, kMetrics) || m.size() != 15) {
        return false;
    }

    out.missingMax = std::stoi(m[1].str());
    out.placeholderMax = std::stoi(m[2].str());
    out.fetchPendingMax = std::stoi(m[3].str());
    out.fetchDecodeMax = std::stoi(m[4].str());
    out.fetchActiveMax = std::stoi(m[5].str());
    out.quorumDownMax = std::stoi(m[6].str());
    out.seamGapMaxPeakM = std::stod(m[7].str());
    out.seamGapMaxEndM = std::stod(m[8].str());
    out.cliffMax = std::stoi(m[9].str());
    out.cliffEnd = std::stoi(m[10].str());
    out.placeholderEnd = std::stoi(m[11].str());
    out.demFlatEnd = std::stoi(m[12].str());
    out.demPendingEnd = std::stoi(m[13].str());
    out.leafUnderflowDelta = std::stoi(m[14].str());
    return true;
}

OwnershipHints ClassifyOwnership(const SmokeMetrics& m) {
    OwnershipHints hints{};
    if (m.missingMax > 0) {
        hints.renderSetOrQuorum = true;
    }
    if (m.quorumDownMax > 0 && m.missingMax > 0) {
        hints.renderSetOrQuorum = true;
    }
    if (m.seamGapMaxPeakM >= 10.0 || m.cliffEnd > 0) {
        hints.demEdgePackOrStitch = true;
    }
    if (m.demPendingEnd > 0) {
        hints.demPendingConvergence = true;
    }
    return hints;
}

} // namespace

int main() {
    int failed = 0;

    {
        const std::string line =
            "Metrics: missing(max)=3 placeholder(max)=0 fetch(p/d/a max)=16/16/16 "
            "quorumDown(max)=8 seamGapMax(max/end)=24.000000/24.000000m cliffs(max/end)=12/2 "
            "placeholder(end)=0 demFlat(end)=0 demPending(end)=0 leafUnderflow(delta)=0";
        SmokeMetrics metrics;
        failed += !Expect(ParseSmokeMetricsLine(line, metrics), "parser must accept canonical metrics line");
        failed += !Expect(metrics.missingMax == 3, "missing(max) should parse as 3");
        failed += !Expect(metrics.quorumDownMax == 8, "quorumDown(max) should parse as 8");
        failed += !Expect(metrics.seamGapMaxPeakM == 24.0, "seam peak should parse as 24.0");
        failed += !Expect(metrics.seamGapMaxEndM == 24.0, "seam end should parse as 24.0");
        failed += !Expect(metrics.cliffEnd == 2, "cliff end should parse as 2");
        failed += !Expect(metrics.demPendingEnd == 0, "demPending(end) should parse as 0");

        OwnershipHints hints = ClassifyOwnership(metrics);
        failed += !Expect(hints.renderSetOrQuorum, "missing tiles should map to render-set/quorum ownership");
        failed += !Expect(hints.demEdgePackOrStitch, "high seam/cliff should map to edge-pack/stitch ownership");
        failed += !Expect(!hints.demPendingConvergence, "demPending end=0 should not map to pending convergence");
    }

    {
        const std::string line =
            "Metrics: missing(max)=0 placeholder(max)=0 fetch(p/d/a max)=2/0/1 "
            "quorumDown(max)=0 seamGapMax(max/end)=6.000000/4.000000m cliffs(max/end)=0/0 "
            "placeholder(end)=0 demFlat(end)=0 demPending(end)=0 leafUnderflow(delta)=0";
        SmokeMetrics metrics;
        failed += !Expect(ParseSmokeMetricsLine(line, metrics), "parser must accept clean metrics line");
        OwnershipHints hints = ClassifyOwnership(metrics);
        failed += !Expect(!hints.renderSetOrQuorum, "clean run should not map to render-set/quorum ownership");
        failed += !Expect(!hints.demEdgePackOrStitch, "clean seam/cliff should not map to edge-pack ownership");
        failed += !Expect(!hints.demPendingConvergence, "clean dem pending should not map to pending ownership");
    }

    {
        const std::string malformed = "Metrics: missing=3 seam=24";
        SmokeMetrics metrics;
        failed += !Expect(!ParseSmokeMetricsLine(malformed, metrics), "parser must reject malformed metrics line");
    }

    if (failed == 0) {
        std::cout << "SmokeReportParserTest PASSED\n";
        return 0;
    }

    std::cerr << "SmokeReportParserTest FAILED (" << failed << " checks failed)\n";
    return 1;
}
