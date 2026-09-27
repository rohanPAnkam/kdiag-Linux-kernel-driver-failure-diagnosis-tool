// kdiag/stage.hpp
//
// The expected causal pipeline for one captured frame/buffer, exactly as
// laid out in the project brief:
//
//   PCI probe -> device init -> buffer alloc -> DMA mapping -> stream ON
//   -> descriptor submission -> IRQ -> DMA completion -> vb2_buffer_done()
//   -> V4L2 dequeue -> GStreamer
//
// Month 2's job is to map free-text events onto this ordered pipeline and
// find the first place a chain doesn't reach the next stage. This is a
// SEPARATE classification pass from Month 1's Component/event tagging
// (kernel_line_classifier.hpp): Component answers "which subsystem does
// this line belong to" (coarse, used for filtering/grouping); Stage
// answers "which point in a single buffer's lifecycle does this line
// represent" (fine-grained, ordered, used for gap detection). A line can
// have a Component but no Stage (most dmesg noise does) — that's normal
// and such events are simply excluded from timeline construction.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "kdiag/event.hpp"

namespace kdiag {

enum class Stage {
    kPciProbe = 0,
    kDeviceInit,
    kBufferAlloc,
    kDmaMapping,
    kStreamOn,
    kDescriptorSubmission,
    kIrq,
    kDmaCompletion,
    kVb2BufferDone,
    kV4l2Dequeue,
    kGstreamerConsume,
};

// The pipeline in causal order. Index into this vector == the Stage
// enum's underlying integer value; DetectBreak (correlator.hpp) relies
// on that correspondence.
/// Return the stable pipeline order used for stage sorting and gap detection.
const std::vector<Stage>& PipelineOrder();

/// Return the machine-readable name for a pipeline stage.
std::string ToString(Stage s);

// Classifies free-text log messages onto the pipeline above. Stateless
// substring rule table, same architecture as KernelLineClassifier.
class StageClassifier {
public:
    /// Build the fixed ordered substring rule table.
    StageClassifier();

    // Returns nullopt if `message` doesn't match any known stage marker
    // (the common case — most log lines aren't stage transitions).
    std::optional<Stage> Classify(const std::string& message) const;

private:
    struct Rule {
        std::string substring;
        Stage stage;
    };
    std::vector<Rule> rules_;
};

}  // namespace kdiag
