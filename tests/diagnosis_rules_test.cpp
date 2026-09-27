// Verifies diagnosis rules, confidence ranking, evidence indices, and edge cases.
#include "kdiag/diagnosis_rules.hpp"

#include <algorithm>

#include <gtest/gtest.h>

namespace kdiag {
namespace {

Event MakeEvent(int64_t timestamp_ns, const std::string& message) {
    Event event;
    event.timestamp_ns = timestamp_ns;
    event.source = Source::kDmesg;
    event.component = Component::kDriver;
    event.event = "kernel_line";
    event.message = message;
    event.raw = message;
    return event;
}

Event MakeSnapshot(Source source, std::string event_name, std::string message,
                   std::string raw) {
    Event event;
    event.timestamp_ns = kSnapshotTimestampNs;
    event.source = source;
    event.component = source == Source::kLspci ? Component::kPci : Component::kV4l2;
    event.event = std::move(event_name);
    event.message = std::move(message);
    event.raw = std::move(raw);
    event.fields = {{"snapshot", true}};
    return event;
}

std::vector<DiagnosisReport> Diagnose(const CollectionRun& run) {
    const Correlator correlator(run.events);
    const DiagnosisRuleEngine engine(run);
    return engine.DiagnoseAll(correlator.Correlate());
}

const DiagnosisHypothesis* FindHypothesis(
    const std::vector<DiagnosisReport>& reports, const std::string& title) {
    for (const auto& report : reports) {
        for (const auto& hypothesis : report.hypotheses) {
            if (hypothesis.title == title) return &hypothesis;
        }
    }
    return nullptr;
}

TEST(DiagnosisRuleEngineTest, DescriptorWithoutIrqOrCompletionIsHighConfidence) {
    CollectionRun run;
    run.events = {
        MakeEvent(1, "streaming started on video3"),
        MakeEvent(2, "descriptor submitted for buffer 42"),
    };

    const auto reports = Diagnose(run);
    const auto* hypothesis = FindHypothesis(reports, "DMA Completion Interrupt Not Observed");

    ASSERT_NE(hypothesis, nullptr);
    EXPECT_EQ(hypothesis->rule_id, "dma_no_completion");
    EXPECT_EQ(hypothesis->scope, DiagnosisScope::kBuffer);
    EXPECT_EQ(hypothesis->buffer_id, 42);
    EXPECT_EQ(hypothesis->confidence, Confidence::kHigh);
    ASSERT_EQ(hypothesis->evidence_event_indices.size(), 2u);
    EXPECT_EQ(hypothesis->evidence_descriptions.size(), 2u);
    EXPECT_NE(std::find_if(hypothesis->suggested_investigation.begin(),
                           hypothesis->suggested_investigation.end(),
                           [](const std::string& step) {
                               return step.find("IRQ") != std::string::npos;
                           }), hypothesis->suggested_investigation.end());
}

TEST(DiagnosisRuleEngineTest, DeviceInitWithoutStreamOnProducesMediumHypothesis) {
    CollectionRun run;
    run.events = {MakeEvent(1, "device initialized")};

    const auto reports = Diagnose(run);
    const auto* hypothesis = FindHypothesis(reports, "Capture Stream Was Never Started");

    ASSERT_NE(hypothesis, nullptr);
    EXPECT_EQ(hypothesis->confidence, Confidence::kMedium);
}

TEST(DiagnosisRuleEngineTest, CompletionWithoutClassifiedIrqProducesInterruptHypothesis) {
    CollectionRun run;
    run.events = {
        MakeEvent(1, "descriptor submitted for buffer 7"),
        MakeEvent(2, "dma completion for buffer 7"),
    };

    const auto reports = Diagnose(run);
    const auto* hypothesis = FindHypothesis(reports, "Completion Interrupt Not Received");

    ASSERT_NE(hypothesis, nullptr);
    EXPECT_EQ(hypothesis->rule_id, "descriptor_without_irq");
    EXPECT_EQ(hypothesis->scope, DiagnosisScope::kBuffer);
    EXPECT_EQ(hypothesis->buffer_id, 7);
    EXPECT_EQ(hypothesis->confidence, Confidence::kHigh);
}

TEST(DiagnosisRuleEngineTest, CompletionWithoutVb2DoneProducesDriverHypothesis) {
    CollectionRun run;
    run.events = {MakeEvent(1, "dma completion for buffer 7")};

    const auto reports = Diagnose(run);
    const auto* hypothesis = FindHypothesis(reports, "DMA Completion Without vb2_buffer_done");

    ASSERT_NE(hypothesis, nullptr);
    EXPECT_EQ(hypothesis->confidence, Confidence::kMedium);
}

TEST(DiagnosisRuleEngineTest, Vb2DoneWithoutDequeueProducesUserspaceHypothesis) {
    CollectionRun run;
    run.events = {MakeEvent(1, "vb2_buffer_done: buffer 7 marked DONE")};

    const auto reports = Diagnose(run);
    EXPECT_NE(FindHypothesis(reports,
                             "Kernel Completed Buffer But Userspace Did Not Dequeue It"),
              nullptr);
}

TEST(DiagnosisRuleEngineTest, DetectsDegradedPcieLinkWithDmaFailure) {
    CollectionRun run;
    Event capability = MakeSnapshot(Source::kLspci, "pci_link_capability", "",
                                    "\tLnkCap: Port #0, Speed 8GT/s, Width x4");
    capability.fields["device_header"] = "0000:03:00.0 Multimedia controller";
    Event status = MakeSnapshot(Source::kLspci, "pci_link_status", "",
                                "\tLnkSta: Speed 2.5GT/s, Width x1");
    status.fields["device_header"] = "0000:03:00.0 Multimedia controller";
    run.events = {
        MakeEvent(1, "descriptor submitted for buffer 7"),
        capability,
        status,
    };

    const auto reports = Diagnose(run);
    const auto* hypothesis = FindHypothesis(reports, "PCIe Link Degraded");

    ASSERT_NE(hypothesis, nullptr);
    EXPECT_EQ(hypothesis->confidence, Confidence::kHigh);
    ASSERT_EQ(hypothesis->evidence_event_indices.size(), 2u);
    EXPECT_LT(hypothesis->evidence_event_indices[1], run.events.size());
}

TEST(DiagnosisRuleEngineTest, DeviceInitWithoutBuffersOrStreamOnSuggestsAllocationFailure) {
    CollectionRun run;
    run.events = {MakeEvent(1, "device initialized")};

    const auto reports = Diagnose(run);
    EXPECT_NE(FindHypothesis(reports, "Buffer Allocation May Have Failed"), nullptr);
}

TEST(DiagnosisRuleEngineTest, V4l2DequeueWithoutGstreamerProducesPipelineHypothesis) {
    CollectionRun run;
    run.events = {MakeEvent(1, "VIDIOC_DQBUF buffer 7")};

    const auto reports = Diagnose(run);
    EXPECT_NE(FindHypothesis(reports, "GStreamer Pipeline May Be Stalled"), nullptr);
}

TEST(DiagnosisRuleEngineTest, NoV4l2NodesProducesRunWideHypothesis) {
    CollectionRun run;
    run.events.push_back(MakeSnapshot(Source::kSysfsV4l2, "v4l2_no_nodes_found",
                                      "no video nodes", "no video nodes"));

    const auto reports = Diagnose(run);
    const auto* hypothesis = FindHypothesis(reports, "V4L2 Device Node Never Appeared");

    ASSERT_NE(hypothesis, nullptr);
    EXPECT_EQ(hypothesis->rule_id, "v4l2_nodes_missing");
    EXPECT_EQ(hypothesis->scope, DiagnosisScope::kCollection);
    EXPECT_FALSE(hypothesis->buffer_id.has_value());
    EXPECT_EQ(hypothesis->confidence, Confidence::kHigh);
    EXPECT_EQ(hypothesis->evidence_event_indices, (std::vector<size_t>{0}));
}

TEST(DiagnosisRuleEngineTest, KernelBugMarkerProducesRunWideHypothesis) {
    CollectionRun run;
    run.events = {MakeEvent(5, "kernel BUG at drivers/media/example.c:12")};

    const auto reports = Diagnose(run);
    const auto* hypothesis = FindHypothesis(reports, "Kernel BUG or Panic Detected");
    ASSERT_NE(hypothesis, nullptr);
    EXPECT_EQ(hypothesis->rule_id, "kernel_bug_or_panic");
    EXPECT_EQ(hypothesis->scope, DiagnosisScope::kCollection);
    EXPECT_FALSE(hypothesis->buffer_id.has_value());
}

TEST(DiagnosisRuleEngineTest, CompleteChainHasNoHypotheses) {
    CollectionRun run;
    const std::vector<std::string> messages = {
        "driver_name: probe succeeded", "device initialized", "buffer allocated for buffer 7",
        "dma_map_single: mapped buffer 7", "streaming started on video3",
        "descriptor submitted for buffer 7", "interrupt received for buffer 7",
        "dma completion for buffer 7", "vb2_buffer_done: buffer 7",
        "VIDIOC_DQBUF buffer 7", "appsink pulled buffer 7",
    };
    int64_t timestamp = 1;
    for (const auto& message : messages) run.events.push_back(MakeEvent(timestamp++, message));

    const auto reports = Diagnose(run);

    ASSERT_EQ(reports.size(), 1u);
    EXPECT_TRUE(reports[0].hypotheses.empty());
    EXPECT_FALSE(reports[0].has_root_cause_candidate);
}

TEST(DiagnosisRuleEngineTest, IntermittentHypothesisRequiresDifferentFailureStages) {
    CollectionRun run;
    const std::vector<std::string> complete_stages = {
        "driver_name: probe succeeded for buffer 1", "device initialized for buffer 1",
        "buffer allocated for buffer 1", "dma_map_single: mapped buffer 1",
        "streaming started for buffer 1", "descriptor submitted for buffer 1",
        "interrupt received for buffer 1", "dma completion for buffer 1",
        "vb2_buffer_done: buffer 1", "VIDIOC_DQBUF buffer 1", "appsink buffer 1",
        "driver_name: probe succeeded for buffer 3", "device initialized for buffer 3",
        "buffer allocated for buffer 3", "dma_map_single: mapped buffer 3",
        "streaming started for buffer 3", "descriptor submitted for buffer 3",
        "interrupt received for buffer 3", "dma completion for buffer 3",
        "vb2_buffer_done: buffer 3", "VIDIOC_DQBUF buffer 3", "appsink buffer 3",
    };
    int64_t timestamp = 1;
    for (const auto& message : complete_stages) run.events.push_back(MakeEvent(timestamp++, message));
    const std::vector<std::string> buffer_four_stages = {
        "driver_name: probe succeeded for buffer 4", "device initialized for buffer 4",
        "buffer allocated for buffer 4", "dma_map_single: mapped buffer 4",
        "streaming started for buffer 4", "descriptor submitted for buffer 4",
        "interrupt received for buffer 4", "dma completion for buffer 4",
        "vb2_buffer_done: buffer 4", "VIDIOC_DQBUF buffer 4",
    };
    for (const auto& message : buffer_four_stages) run.events.push_back(MakeEvent(timestamp++, message));
    run.events.push_back(MakeEvent(timestamp++, "descriptor submitted for buffer 2"));

    const auto reports = Diagnose(run);
    const auto* hypothesis = FindHypothesis(
        reports, "Intermittent Capture Failure (Not Systematic)");

    ASSERT_NE(hypothesis, nullptr);
    EXPECT_EQ(hypothesis->confidence, Confidence::kLow);
    EXPECT_EQ(hypothesis->rule_id, "intermittent_capture_failure");
    EXPECT_EQ(hypothesis->scope, DiagnosisScope::kCollection);
    EXPECT_FALSE(hypothesis->buffer_id.has_value());
}

TEST(DiagnosisRuleEngineTest, EveryEvidenceIndexIsInBoundsAndHypothesesAreRanked) {
    CollectionRun run;
    run.events = {
        MakeEvent(1, "device initialized"),
        MakeEvent(2, "descriptor submitted for buffer 9"),
        MakeEvent(3, "streaming started on video3"),
        MakeEvent(4, "dma completion for buffer 9"),
        MakeEvent(5, "kernel BUG at drivers/media/example.c:12"),
    };

    const auto reports = Diagnose(run);
    const auto* interrupt = FindHypothesis(reports, "Completion Interrupt Not Received");
    const auto* driver = FindHypothesis(reports, "DMA Completion Without vb2_buffer_done");

    ASSERT_NE(interrupt, nullptr);
    ASSERT_NE(driver, nullptr);
    EXPECT_EQ(interrupt->confidence, Confidence::kHigh);
    EXPECT_EQ(driver->confidence, Confidence::kMedium);
    for (const auto& report : reports) {
        for (const auto& hypothesis : report.hypotheses) {
            EXPECT_EQ(hypothesis.evidence_event_indices.size(),
                      hypothesis.evidence_descriptions.size());
            for (size_t index : hypothesis.evidence_event_indices) {
                EXPECT_LT(index, run.events.size());
            }
        }
    }
}

}  // namespace
}  // namespace kdiag