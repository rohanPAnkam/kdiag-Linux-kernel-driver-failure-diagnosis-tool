// Applies the original fixed diagnosis predicates to correlated chains.
#include "kdiag/diagnoser.hpp"

namespace kdiag {

namespace {

// Find the first recorded occurrence of a stage in a correlated chain.
const StageOccurrence* FindStage(const ChainReport& report, Stage stage) {
    for (const auto& occurrence : report.reached_stages) {
        if (occurrence.stage == stage) return &occurrence;
    }
    return nullptr;
}

}  // namespace

std::vector<DiagnosisFinding> Diagnoser::Diagnose(
    const std::vector<ChainReport>& reports) const {
    std::vector<DiagnosisFinding> findings;

    for (const auto& report : reports) {
        const auto* descriptor = FindStage(report, Stage::kDescriptorSubmission);
        const auto* irq = FindStage(report, Stage::kIrq);
        const auto* completion = FindStage(report, Stage::kDmaCompletion);
        const auto* vb2_done = FindStage(report, Stage::kVb2BufferDone);
        const auto* dequeue = FindStage(report, Stage::kV4l2Dequeue);
        const auto* gstreamer = FindStage(report, Stage::kGstreamerConsume);

        if (descriptor && !irq && !completion) {
            findings.push_back({
                "KDIAG-DMA-001",
                "Possible DMA/interrupt failure",
                "A descriptor submission was observed, but neither an IRQ nor DMA completion was observed in this chain.",
                report.buffer_id,
                {*descriptor},
                {Stage::kIrq, Stage::kDmaCompletion},
            });
        }

        if (completion && !vb2_done) {
            findings.push_back({
                "KDIAG-DRV-001",
                "Possible driver completion-path bug",
                "DMA completion was observed, but vb2_buffer_done() was not observed in this chain.",
                report.buffer_id,
                {*completion},
                {Stage::kVb2BufferDone},
            });
        }

        if (dequeue && !gstreamer) {
            findings.push_back({
                "KDIAG-USER-001",
                "Possible userspace pipeline issue",
                "A V4L2 buffer dequeue was observed, but GStreamer consumption was not observed in this chain.",
                report.buffer_id,
                {*dequeue},
                {Stage::kGstreamerConsume},
            });
        }
    }

    return findings;
}

}  // namespace kdiag