// Lightweight legacy findings produced directly from correlated chains.
#ifndef D571BEEF_3FF7_4BEB_9D21_1D6C1B3F37A5
#define D571BEEF_3FF7_4BEB_9D21_1D6C1B3F37A5

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "kdiag/correlator.hpp"
#include "kdiag/stage.hpp"

namespace kdiag {

struct DiagnosisFinding {
    std::string rule_id;  ///< Stable identifier for the matched rule.
    std::string title;  ///< Short user-facing hypothesis.
    std::string assessment;  ///< Evidence-bounded explanation of the pattern.
    std::optional<int64_t> buffer_id;  ///< Buffer ID, or empty for a session chain.
    std::vector<StageOccurrence> evidence;  ///< Observed stages supporting the finding.
    std::vector<Stage> not_observed;  ///< Stages absent from the correlated chain.
};

class Diagnoser {
public:
    /// Generate the initial fixed set of findings for each supplied chain.
    std::vector<DiagnosisFinding> Diagnose(
        const std::vector<ChainReport>& reports) const;
};

}  // namespace kdiag


#endif /* D571BEEF_3FF7_4BEB_9D21_1D6C1B3F37A5 */
