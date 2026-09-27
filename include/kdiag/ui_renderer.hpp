// Text rendering for correlated per-buffer pipeline timelines.
#ifndef DBDF8942_E0FC_42A5_8FF4_3E704BEB3A46
#define DBDF8942_E0FC_42A5_8FF4_3E704BEB3A46

#include <string>

#include "kdiag/correlator.hpp"

namespace kdiag {

class PipelineRenderer {
public:
    /// Render reached stages and the first missing stage as an ASCII timeline.
    std::string RenderTimeline(const ChainReport& report) const;
};

}  // namespace kdiag


#endif /* DBDF8942_E0FC_42A5_8FF4_3E704BEB3A46 */
