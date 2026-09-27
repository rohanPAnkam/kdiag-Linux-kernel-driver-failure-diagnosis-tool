// Defines synthetic failure captures and scores the diagnosis pipeline.
#include "kdiag/evaluator.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

#include "kdiag/correlator.hpp"
#include "kdiag/diagnosis_rules.hpp"

namespace kdiag::eval {

namespace {

// Build a timestamped synthetic kernel-line event for a scenario fixture.
Event MakeEvent(int64_t timestamp_ns, std::string message) {
    Event event;
    event.timestamp_ns = timestamp_ns;
    event.source = Source::kDmesg;
    event.component = Component::kDriver;
    event.event = "kernel_line";
    event.message = std::move(message);
    event.raw = event.message;
    return event;
}

// Build a non-timeline event representing current snapshot state.
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

// Generate the canonical ordered event sequence for a successful buffer.
std::vector<Event> CompleteBuffer(int64_t buffer_id, int64_t* timestamp_ns) {
    const std::string id = std::to_string(buffer_id);
    const std::vector<std::string> messages = {
        "driver_name: probe succeeded for buffer " + id,
        "device initialized for buffer " + id,
        "buffer allocated for buffer " + id,
        "dma_map_single: mapped buffer " + id,
        "streaming started for buffer " + id,
        "descriptor submitted for buffer " + id,
        "interrupt received for buffer " + id,
        "dma completion for buffer " + id,
        "vb2_buffer_done: buffer " + id,
        "VIDIOC_DQBUF buffer " + id,
        "appsink pulled buffer " + id,
    };
    std::vector<Event> events;
    for (const auto& message : messages) {
        events.push_back(MakeEvent((*timestamp_ns)++, message));
    }
    return events;
}

// Append a scenario's generated events without replacing existing events.
void Append(std::vector<Event>* target, std::vector<Event> source) {
    target->insert(target->end(), source.begin(), source.end());
}

// Join scenario labels into the compact format used by the report.
std::string Join(const std::vector<std::string>& values) {
    std::ostringstream output;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i != 0) output << ", ";
        output << values[i];
    }
    return output.str();
}

}  // namespace

std::vector<FailureScenario> Evaluator::InitializeScenarios() const {
    std::vector<FailureScenario> scenarios;

    FailureScenario dma_timeout;
    dma_timeout.name = "dma_timeout";
    dma_timeout.description = "A submitted descriptor has no observed IRQ or DMA completion.";
    dma_timeout.expected_failure_stage = Stage::kIrq;
    dma_timeout.expected_root_causes = {"DMA Completion Interrupt Not Observed"};
    dma_timeout.expected_confidence = Confidence::kHigh;
    dma_timeout.injected_events = {
        MakeEvent(1, "streaming started on video3"),
        MakeEvent(2, "descriptor submitted for buffer 1832"),
    };
    scenarios.push_back(std::move(dma_timeout));

    FailureScenario irq_gap;
    irq_gap.name = "irq_missing_with_completion";
    irq_gap.description = "DMA completion is logged but no IRQ-handler marker is present.";
    irq_gap.expected_root_causes = {"Completion Interrupt Not Received"};
    irq_gap.expected_confidence = Confidence::kHigh;
    irq_gap.injected_events = {
        MakeEvent(1, "descriptor submitted for buffer 12"),
        MakeEvent(2, "dma completion for buffer 12"),
    };
    scenarios.push_back(std::move(irq_gap));

    FailureScenario buffer_starvation;
    buffer_starvation.name = "buffer_starvation";
    buffer_starvation.description = "Device initialized, but buffer setup was not observed.";
    buffer_starvation.expected_failure_stage = Stage::kBufferAlloc;
    buffer_starvation.expected_root_causes = {"Buffer Allocation May Have Failed"};
    buffer_starvation.expected_confidence = Confidence::kMedium;
    buffer_starvation.injected_events = {MakeEvent(1, "device initialized")};
    scenarios.push_back(std::move(buffer_starvation));

    FailureScenario pcie_degraded;
    pcie_degraded.name = "pcie_link_degraded";
    pcie_degraded.description = "A degraded PCIe link accompanies a descriptor with no completion.";
    pcie_degraded.expected_failure_stage = Stage::kIrq;
    pcie_degraded.expected_root_causes = {"PCIe Link Degraded"};
    pcie_degraded.expected_confidence = Confidence::kHigh;
    pcie_degraded.injected_events = {
        MakeEvent(1, "streaming started on video3"),
        MakeEvent(2, "descriptor submitted for buffer 24"),
    };
    Event link_capability = MakeSnapshot(Source::kLspci, "pci_link_capability", "",
                                         "\tLnkCap: Speed 8GT/s, Width x4");
    link_capability.fields["device_header"] = "0000:03:00.0 Multimedia controller";
    Event link_status = MakeSnapshot(Source::kLspci, "pci_link_status", "",
                                     "\tLnkSta: Speed 2.5GT/s, Width x1");
    link_status.fields["device_header"] = "0000:03:00.0 Multimedia controller";
    pcie_degraded.injected_events.push_back(std::move(link_capability));
    pcie_degraded.injected_events.push_back(std::move(link_status));
    scenarios.push_back(std::move(pcie_degraded));

    FailureScenario gstreamer_stall;
    gstreamer_stall.name = "gstreamer_pipeline_stall";
    gstreamer_stall.description = "A V4L2 dequeue is logged without GStreamer consumption.";
    gstreamer_stall.expected_failure_stage = Stage::kGstreamerConsume;
    gstreamer_stall.expected_root_causes = {"GStreamer Pipeline May Be Stalled"};
    gstreamer_stall.expected_confidence = Confidence::kMedium;
    gstreamer_stall.injected_events = {
        MakeEvent(1, "VIDIOC_DQBUF buffer 31"),
    };
    scenarios.push_back(std::move(gstreamer_stall));

    FailureScenario intermittent;
    intermittent.name = "intermittent_capture_failure";
    intermittent.description = "Some buffers complete while other buffers stop at different stages.";
    intermittent.expected_root_causes = {"Intermittent Capture Failure (Not Systematic)"};
    intermittent.expected_confidence = Confidence::kLow;
    int64_t timestamp = 1;
    Append(&intermittent.injected_events, CompleteBuffer(1, &timestamp));
    Append(&intermittent.injected_events, CompleteBuffer(3, &timestamp));
    intermittent.injected_events.push_back(
        MakeEvent(timestamp++, "descriptor submitted for buffer 2"));
    const std::vector<std::string> dequeue_chain = {
        "driver_name: probe succeeded for buffer 4",
        "device initialized for buffer 4",
        "buffer allocated for buffer 4",
        "dma_map_single: mapped buffer 4",
        "streaming started for buffer 4",
        "descriptor submitted for buffer 4",
        "interrupt received for buffer 4",
        "dma completion for buffer 4",
        "vb2_buffer_done: buffer 4",
        "VIDIOC_DQBUF buffer 4",
    };
    for (const auto& message : dequeue_chain) {
        intermittent.injected_events.push_back(MakeEvent(timestamp++, message));
    }
    scenarios.push_back(std::move(intermittent));

    FailureScenario no_nodes;
    no_nodes.name = "device_node_missing";
    no_nodes.description = "The V4L2 sysfs snapshot contains no video nodes.";
    no_nodes.expected_root_causes = {"V4L2 Device Node Never Appeared"};
    no_nodes.expected_confidence = Confidence::kHigh;
    no_nodes.injected_events.push_back(MakeSnapshot(
        Source::kSysfsV4l2, "v4l2_no_nodes_found",
        "no /sys/class/video4linux/videoN nodes exist right now", "no nodes found"));
    scenarios.push_back(std::move(no_nodes));

    FailureScenario kernel_bug;
    kernel_bug.name = "kernel_bug_or_panic";
    kernel_bug.description = "Kernel logs contain a BUG marker.";
    kernel_bug.expected_root_causes = {"Kernel BUG or Panic Detected"};
    kernel_bug.expected_confidence = Confidence::kHigh;
    kernel_bug.injected_events = {
        MakeEvent(1, "kernel BUG at drivers/media/videobuf2-core.c:123"),
    };
    scenarios.push_back(std::move(kernel_bug));

    FailureScenario multiple_causes;
    multiple_causes.name = "multiple_contributing_hypotheses";
    multiple_causes.description = "A missing DMA completion coincides with a degraded PCIe link.";
    multiple_causes.expected_failure_stage = Stage::kIrq;
    multiple_causes.expected_root_causes = {
        "DMA Completion Interrupt Not Observed", "PCIe Link Degraded"};
    multiple_causes.expected_confidence = Confidence::kHigh;
    multiple_causes.injected_events = {
        MakeEvent(1, "streaming started on video3"),
        MakeEvent(2, "descriptor submitted for buffer 49"),
    };
    Event multi_capability = MakeSnapshot(Source::kLspci, "pci_link_capability", "",
                                          "\tLnkCap: Speed 8GT/s, Width x4");
    multi_capability.fields["device_header"] = "0000:03:00.0 Multimedia controller";
    Event multi_status = MakeSnapshot(Source::kLspci, "pci_link_status", "",
                                      "\tLnkSta: Speed 2.5GT/s, Width x1");
    multi_status.fields["device_header"] = "0000:03:00.0 Multimedia controller";
    multiple_causes.injected_events.push_back(std::move(multi_capability));
    multiple_causes.injected_events.push_back(std::move(multi_status));
    scenarios.push_back(std::move(multiple_causes));

    FailureScenario clean;
    clean.name = "clean_complete_capture";
    clean.description = "Every expected stage is observed for the buffer.";
    clean.injected_events = CompleteBuffer(99, &timestamp);
    scenarios.push_back(std::move(clean));

    return scenarios;
}

std::vector<EvaluationResult> Evaluator::EvaluateAll() const {
    std::vector<EvaluationResult> results;
    for (const auto& scenario : InitializeScenarios()) {
        CollectionRun run;
        run.events = scenario.injected_events;
        const Correlator correlator(run.events);
        const auto chains = correlator.Correlate();
        const DiagnosisRuleEngine engine(run);
        const auto reports = engine.DiagnoseAll(chains);

        std::vector<std::string> actual_titles;
        std::vector<const DiagnosisHypothesis*> actual_hypotheses;
        for (const auto& report : reports) {
            for (const auto& hypothesis : report.hypotheses) {
                actual_titles.push_back(hypothesis.title);
                actual_hypotheses.push_back(&hypothesis);
            }
        }

        EvaluationResult result;
        result.scenario_name = scenario.name;
        result.expected_failure_stage = scenario.expected_failure_stage;
        result.detected = !actual_titles.empty();
        result.root_cause_expected = Join(scenario.expected_root_causes);
        result.root_cause_detected = Join(actual_titles);
        if (scenario.expected_root_causes.empty()) {
            result.root_cause_match = actual_titles.empty();
        } else {
            result.root_cause_match = std::all_of(
                scenario.expected_root_causes.begin(), scenario.expected_root_causes.end(),
                [&actual_titles](const std::string& expected) {
                    return std::find(actual_titles.begin(), actual_titles.end(), expected) !=
                           actual_titles.end();
                });
        }

        for (const auto& chain : chains) {
            if (chain.first_missing_stage) {
                result.actual_failure_stage = chain.first_missing_stage;
                break;
            }
        }
        if (scenario.expected_failure_stage) {
            const bool stage_found = std::any_of(
                chains.begin(), chains.end(), [&scenario](const ChainReport& chain) {
                    return chain.first_missing_stage == scenario.expected_failure_stage;
                });
            result.localization_correct = stage_found;
        }

        result.confidence_correct = !scenario.expected_confidence.has_value();
        if (scenario.expected_confidence) {
            result.confidence_correct = std::all_of(
                scenario.expected_root_causes.begin(), scenario.expected_root_causes.end(),
                [&actual_hypotheses, &scenario](const std::string& expected_title) {
                    const auto it = std::find_if(
                        actual_hypotheses.begin(), actual_hypotheses.end(),
                        [&expected_title](const DiagnosisHypothesis* hypothesis) {
                            return hypothesis->title == expected_title;
                        });
                    return it != actual_hypotheses.end() &&
                           (*it)->confidence == *scenario.expected_confidence;
                });
        }

        result.passed = result.root_cause_match && result.confidence_correct &&
            (!result.localization_correct.has_value() || *result.localization_correct);
        results.push_back(std::move(result));
    }
    return results;
}

std::string Evaluator::GenerateReport(const std::vector<EvaluationResult>& results) const {
    const size_t passed = static_cast<size_t>(std::count_if(
        results.begin(), results.end(), [](const EvaluationResult& result) {
            return result.passed;
        }));
    std::ostringstream output;
    output << "KDIAG EVALUATION REPORT\n"
           << "=======================\n\n"
           << "Scenarios: " << results.size() << "\n\n";
    for (const auto& result : results) {
        output << (result.passed ? "[PASS] " : "[FAIL] ") << result.scenario_name << '\n'
               << "  Root cause expected: "
               << (result.root_cause_expected.empty() ? "none" : result.root_cause_expected) << '\n'
               << "  Root cause observed: "
               << (result.root_cause_detected.empty() ? "none" : result.root_cause_detected) << '\n';
        if (result.expected_failure_stage) {
            output << "  Localization: "
                   << (result.localization_correct.value_or(false) ? "correct" : "incorrect")
                   << " (expected " << ToString(*result.expected_failure_stage)
                   << ", observed "
                   << (result.actual_failure_stage ? ToString(*result.actual_failure_stage) : "none")
                   << ")\n";
        } else {
            output << "  Localization: not assessed by the correlator for this scenario\n";
        }
    }
    output << "\nPassed: " << passed << '/' << results.size() << '\n';
    return output.str();
}

}  // namespace kdiag::eval