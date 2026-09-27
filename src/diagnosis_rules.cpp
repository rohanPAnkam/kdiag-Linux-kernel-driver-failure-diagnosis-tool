// Implements deterministic diagnosis predicates and confidence-ranked hypotheses.
#include "kdiag/diagnosis_rules.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <set>
#include <utility>

namespace kdiag {

std::string ToString(Confidence confidence) {
    switch (confidence) {
        case Confidence::kLow: return "LOW";
        case Confidence::kMedium: return "MEDIUM";
        case Confidence::kHigh: return "HIGH";
    }
    return "LOW";
}

std::string ToString(DiagnosisScope scope) {
    switch (scope) {
        case DiagnosisScope::kBuffer: return "buffer";
        case DiagnosisScope::kCollection: return "collection";
    }
    return "buffer";
}

namespace {

// Locate a stage occurrence in a chain without changing pipeline order.
const StageOccurrence* FindStage(const ChainReport& report, Stage stage) {
    for (const auto& occurrence : report.reached_stages) {
        if (occurrence.stage == stage) return &occurrence;
    }
    return nullptr;
}

// Find the first collection event satisfying a rule-specific predicate.
const Event* FindEvent(const CollectionRun& run,
                       const std::function<bool(const Event&)>& predicate) {
    for (const auto& event : run.events) {
        if (predicate(event)) return &event;
    }
    return nullptr;
}

// Normalize text for case-insensitive marker checks.
std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

// Extract negotiated PCIe speed/width values from a link status line.
bool ParseLinkValue(const std::string& text, double* speed, int* width) {
    static const std::regex kSpeed(R"(Speed\s+([0-9]+(?:\.[0-9]+)?)GT/s)",
                                   std::regex::icase);
    static const std::regex kWidth(R"(Width\s+x([0-9]+))", std::regex::icase);
    std::smatch match;
    bool found = false;
    if (std::regex_search(text, match, kSpeed)) {
        *speed = std::stod(match[1].str());
        found = true;
    }
    if (std::regex_search(text, match, kWidth)) {
        *width = std::stoi(match[1].str());
        found = true;
    }
    return found;
}

// Find a link-status snapshot that is below its matching capability snapshot.
const Event* FindDegradedLink(const CollectionRun& run) {
    for (const auto& status : run.events) {
        if (status.event != "pci_link_status") continue;
        const std::string status_text = status.raw + " " + status.message;
        if (Lower(status_text).find("downgraded") != std::string::npos) return &status;

        double status_speed = 0.0;
        int status_width = 0;
        if (!ParseLinkValue(status_text, &status_speed, &status_width)) continue;

        const std::string device = status.fields.value("device_header", "");
        for (const auto& capability : run.events) {
            if (capability.event != "pci_link_capability") continue;
            if (capability.fields.value("device_header", "") != device) continue;

            double capability_speed = 0.0;
            int capability_width = 0;
            if (!ParseLinkValue(capability.raw + " " + capability.message,
                                &capability_speed, &capability_width)) continue;
            if ((status_speed > 0.0 && capability_speed > status_speed) ||
                (status_width > 0 && capability_width > status_width)) {
                return &status;
            }
        }
    }
    return nullptr;
}

// Assemble a hypothesis and transfer its evidence and investigation data.
DiagnosisHypothesis MakeHypothesis(
    std::string title, std::string description, Confidence confidence,
    std::vector<size_t> evidence_indices,
    std::vector<std::string> evidence_descriptions,
    std::vector<std::string> investigation) {
    DiagnosisHypothesis hypothesis;
    hypothesis.title = std::move(title);
    hypothesis.description = std::move(description);
    hypothesis.confidence = confidence;
    hypothesis.evidence_event_indices = std::move(evidence_indices);
    hypothesis.evidence_descriptions = std::move(evidence_descriptions);
    hypothesis.suggested_investigation = std::move(investigation);
    return hypothesis;
}

// Add an observed pipeline stage as a citation when it exists.
void AddStageEvidence(const StageOccurrence* occurrence, const std::string& description,
                      std::vector<size_t>* indices,
                      std::vector<std::string>* descriptions) {
    if (!occurrence) return;
    indices->push_back(occurrence->event_index);
    descriptions->push_back(description);
}

// Convert an event pointer from this run into its stable vector index.
void AddEventEvidence(const Event* event, const CollectionRun& run,
                      const std::string& description, std::vector<size_t>* indices,
                      std::vector<std::string>* descriptions) {
    if (!event) return;
    indices->push_back(static_cast<size_t>(event - run.events.data()));
    descriptions->push_back(description);
}

// Map confidence values to their ranking order.
int ConfidenceRank(Confidence confidence) {
    switch (confidence) {
        case Confidence::kHigh: return 2;
        case Confidence::kMedium: return 1;
        case Confidence::kLow: return 0;
    }
    return 0;
}

// Stable-sort hypotheses from highest to lowest confidence.
void SortHypotheses(std::vector<DiagnosisHypothesis>* hypotheses) {
    std::stable_sort(hypotheses->begin(), hypotheses->end(),
        [](const DiagnosisHypothesis& left, const DiagnosisHypothesis& right) {
            return ConfidenceRank(left.confidence) > ConfidenceRank(right.confidence);
        });
}

}  // namespace

DiagnosisRuleEngine::DiagnosisRuleEngine(const CollectionRun& run) : run_(run) {
    InitializeRules();
}

void DiagnosisRuleEngine::InitializeRules() {
    rules_ = {
        {
            "dma_no_completion",
            [](const ChainReport& report, const CollectionRun&) {
                return FindStage(report, Stage::kStreamOn) &&
                       FindStage(report, Stage::kDescriptorSubmission) &&
                       !FindStage(report, Stage::kIrq) &&
                       !FindStage(report, Stage::kDmaCompletion);
            },
            [](const ChainReport& report, const CollectionRun&) {
                const auto* stream = FindStage(report, Stage::kStreamOn);
                const auto* descriptor = FindStage(report, Stage::kDescriptorSubmission);
                std::vector<size_t> indices;
                std::vector<std::string> descriptions;
                AddStageEvidence(stream, "Capture stream start was observed.", &indices, &descriptions);
                AddStageEvidence(descriptor, "A frame descriptor was submitted.", &indices, &descriptions);
                return std::vector<DiagnosisHypothesis>{MakeHypothesis(
                    "DMA Completion Interrupt Not Observed",
                    "Capture was started and a descriptor was submitted, but no IRQ or DMA completion was recorded for this chain. Possible causes include a DMA timeout, interrupt delivery failure, or missing driver instrumentation.",
                    Confidence::kHigh, std::move(indices), std::move(descriptions),
                    {"Check the device's IRQ line and counts in /proc/interrupts.",
                     "Check PCIe link status with lspci -vvv.",
                     "Inspect dmesg for DMA timeout or PCIe AER messages.",
                     "Trace DMA and interrupt activity with ftrace."})};
            },
        },
        {
            "stream_on_missing",
            [](const ChainReport& report, const CollectionRun&) {
                return FindStage(report, Stage::kDeviceInit) &&
                       !FindStage(report, Stage::kStreamOn);
            },
            [](const ChainReport& report, const CollectionRun&) {
                const auto* init = FindStage(report, Stage::kDeviceInit);
                std::vector<size_t> indices;
                std::vector<std::string> descriptions;
                AddStageEvidence(init, "Device initialization was observed.", &indices, &descriptions);
                return std::vector<DiagnosisHypothesis>{MakeHypothesis(
                    "Capture Stream Was Never Started",
                    "Device initialization was observed, but no stream-on event was found. The application may not have started capture, the call may have failed, or this driver may not log stream-on.",
                    Confidence::kMedium, std::move(indices), std::move(descriptions),
                    {"Check application logs and the VIDIOC_STREAMON return value.",
                     "Inspect the device with v4l2-ctl --device /dev/videoN --info.",
                     "Check application logs for EINVAL or ENOBUFS."})};
            },
        },
        {
            "descriptor_without_irq",
            [](const ChainReport& report, const CollectionRun&) {
                return FindStage(report, Stage::kDescriptorSubmission) &&
                       !FindStage(report, Stage::kIrq) &&
                       FindStage(report, Stage::kDmaCompletion);
            },
            [](const ChainReport& report, const CollectionRun&) {
                const auto* descriptor = FindStage(report, Stage::kDescriptorSubmission);
                const auto* completion = FindStage(report, Stage::kDmaCompletion);
                std::vector<size_t> indices;
                std::vector<std::string> descriptions;
                AddStageEvidence(descriptor, "A frame descriptor was submitted.", &indices, &descriptions);
                AddStageEvidence(completion, "DMA completion was recorded despite no classified IRQ event.", &indices, &descriptions);
                return std::vector<DiagnosisHypothesis>{MakeHypothesis(
                    "Completion Interrupt Not Received",
                    "DMA completion was recorded, but no IRQ-handler event was classified in the chain. The interrupt may not be logged by this driver, or IRQ handling may be misconfigured.",
                    Confidence::kHigh, std::move(indices), std::move(descriptions),
                    {"Check IRQ affinity under /proc/irq/.",
                     "Check MSI/MSI-X configuration with lspci -vvv.",
                     "Compare /proc/interrupts snapshots before and during capture."})};
            },
        },
        {
            "v4l2_dequeue_missing",
            [](const ChainReport& report, const CollectionRun&) {
                return FindStage(report, Stage::kVb2BufferDone) &&
                       !FindStage(report, Stage::kV4l2Dequeue);
            },
            [](const ChainReport& report, const CollectionRun&) {
                const auto* done = FindStage(report, Stage::kVb2BufferDone);
                std::vector<size_t> indices;
                std::vector<std::string> descriptions;
                AddStageEvidence(done, "The kernel marked the V4L2 buffer done.", &indices, &descriptions);
                return std::vector<DiagnosisHypothesis>{MakeHypothesis(
                    "Kernel Completed Buffer But Userspace Did Not Dequeue It",
                    "The kernel completion callback was observed, but no userspace dequeue event was recorded. The application may be blocked, may not be issuing DQBUF, or the dequeue event may not be instrumented.",
                    Confidence::kMedium, std::move(indices), std::move(descriptions),
                    {"Check whether the capture process is blocked using ps and strace.",
                     "Inspect queued buffers with v4l2-ctl --device /dev/videoN --list-buffers."})};
            },
        },
        {
            "completion_without_vb2_done",
            [](const ChainReport& report, const CollectionRun&) {
                return FindStage(report, Stage::kDmaCompletion) &&
                       !FindStage(report, Stage::kVb2BufferDone);
            },
            [](const ChainReport& report, const CollectionRun&) {
                const auto* completion = FindStage(report, Stage::kDmaCompletion);
                std::vector<size_t> indices;
                std::vector<std::string> descriptions;
                AddStageEvidence(completion, "DMA completion was observed.", &indices, &descriptions);
                return std::vector<DiagnosisHypothesis>{MakeHypothesis(
                    "DMA Completion Without vb2_buffer_done",
                    "DMA completion was recorded, but the V4L2 buffer completion callback was not. The driver's completion path may be stuck or the callback may not be logged.",
                    Confidence::kMedium, std::move(indices), std::move(descriptions),
                    {"Inspect driver logs around the DMA completion callback.",
                     "Check videobuf2 queue state and callback return paths.",
                     "Enable targeted driver and videobuf2 tracing."})};
            },
        },
        {
            "pcie_link_degraded",
            [](const ChainReport& report, const CollectionRun& run) {
                return FindDegradedLink(run) &&
                       FindStage(report, Stage::kDescriptorSubmission) &&
                       !FindStage(report, Stage::kDmaCompletion);
            },
            [](const ChainReport& report, const CollectionRun& run) {
                const auto* descriptor = FindStage(report, Stage::kDescriptorSubmission);
                const auto* link = FindDegradedLink(run);
                std::vector<size_t> indices;
                std::vector<std::string> descriptions;
                AddStageEvidence(descriptor, "A descriptor was submitted without a recorded DMA completion.", &indices, &descriptions);
                AddEventEvidence(link, run, "PCIe link status is below the reported link capability.", &indices, &descriptions);
                return std::vector<DiagnosisHypothesis>{MakeHypothesis(
                    "PCIe Link Degraded",
                    "The PCIe link is operating below the device's reported capability, and this chain also lacks a DMA completion. The link condition may contribute to capture failures but does not establish causation.",
                    Confidence::kHigh, std::move(indices), std::move(descriptions),
                    {"Check physical seating, cabling, and device temperature.",
                     "Review BIOS PCIe generation settings.",
                     "Inspect dmesg for PCIe AER errors."})};
            },
        },
        {
            "buffer_allocation_missing",
            [](const ChainReport& report, const CollectionRun&) {
                return FindStage(report, Stage::kDeviceInit) &&
                       !FindStage(report, Stage::kBufferAlloc) &&
                       !FindStage(report, Stage::kStreamOn);
            },
            [](const ChainReport& report, const CollectionRun&) {
                const auto* init = FindStage(report, Stage::kDeviceInit);
                std::vector<size_t> indices;
                std::vector<std::string> descriptions;
                AddStageEvidence(init, "Device initialization was observed.", &indices, &descriptions);
                return std::vector<DiagnosisHypothesis>{MakeHypothesis(
                    "Buffer Allocation May Have Failed",
                    "Device initialization was observed, but neither buffer allocation nor stream-on was logged. Buffer setup may have failed, or the driver may omit these log markers.",
                    Confidence::kMedium, std::move(indices), std::move(descriptions),
                    {"Check available memory with free -h.",
                     "Inspect dmesg for allocation failures or ENOMEM.",
                     "Review requested buffer count and size with v4l2-ctl."})};
            },
        },
        {
            "gstreamer_stalled",
            [](const ChainReport& report, const CollectionRun&) {
                return FindStage(report, Stage::kV4l2Dequeue) &&
                       !FindStage(report, Stage::kGstreamerConsume);
            },
            [](const ChainReport& report, const CollectionRun&) {
                const auto* dequeue = FindStage(report, Stage::kV4l2Dequeue);
                std::vector<size_t> indices;
                std::vector<std::string> descriptions;
                AddStageEvidence(dequeue, "A V4L2 buffer dequeue was observed.", &indices, &descriptions);
                return std::vector<DiagnosisHypothesis>{MakeHypothesis(
                    "GStreamer Pipeline May Be Stalled",
                    "V4L2 dequeue was observed, but no GStreamer consumption event was recorded. This is consistent with a userspace pipeline stall or absent GStreamer logging.",
                    Confidence::kMedium, std::move(indices), std::move(descriptions),
                    {"Enable GStreamer diagnostics with GST_DEBUG=3.",
                     "Check appsink queue limits and backpressure.",
                     "Inspect pipeline state and source/sink configuration."})};
            },
        },
        {
            "v4l2_nodes_missing",
            [](const ChainReport&, const CollectionRun& run) {
                return FindEvent(run, [](const Event& event) {
                    return event.event == "v4l2_no_nodes_found";
                }) != nullptr;
            },
            [](const ChainReport&, const CollectionRun& run) {
                const auto* no_nodes = FindEvent(run, [](const Event& event) {
                    return event.event == "v4l2_no_nodes_found";
                });
                std::vector<size_t> indices;
                std::vector<std::string> descriptions;
                AddEventEvidence(no_nodes, run, "The sysfs snapshot found no V4L2 video nodes.", &indices, &descriptions);
                return std::vector<DiagnosisHypothesis>{MakeHypothesis(
                    "V4L2 Device Node Never Appeared",
                    "The collection snapshot found no /sys/class/video4linux nodes. The device may not have probed successfully, or the snapshot may have been taken before the node appeared.",
                    Confidence::kHigh, std::move(indices), std::move(descriptions),
                    {"Check dmesg for probe failures.",
                     "Confirm the device appears in lspci and the driver is bound.",
                     "Check /sys/class/video4linux again after driver initialization."})};
            },
            DiagnosisScope::kCollection,
        },
        {
            "kernel_bug_or_panic",
            [](const ChainReport&, const CollectionRun& run) {
                return FindEvent(run, [](const Event& event) {
                    const std::string text = Lower(event.message + " " + event.raw);
                    return text.find("kernel panic") != std::string::npos ||
                           text.find("oops:") != std::string::npos ||
                           text.find("bug:") != std::string::npos ||
                           text.find("kernel bug at") != std::string::npos ||
                           text.find("general protection fault") != std::string::npos;
                }) != nullptr;
            },
            [](const ChainReport&, const CollectionRun& run) {
                const auto* bug = FindEvent(run, [](const Event& event) {
                    const std::string text = Lower(event.message + " " + event.raw);
                    return text.find("kernel panic") != std::string::npos ||
                           text.find("oops:") != std::string::npos ||
                           text.find("bug:") != std::string::npos ||
                           text.find("kernel bug at") != std::string::npos ||
                           text.find("general protection fault") != std::string::npos;
                });
                std::vector<size_t> indices;
                std::vector<std::string> descriptions;
                AddEventEvidence(bug, run, "A kernel BUG, Oops, panic, or protection fault marker was found.", &indices, &descriptions);
                return std::vector<DiagnosisHypothesis>{MakeHypothesis(
                    "Kernel BUG or Panic Detected",
                    "The captured events contain a kernel crash or BUG marker. Preserve the complete kernel log and stack trace before interpreting other missing pipeline stages.",
                    Confidence::kHigh, std::move(indices), std::move(descriptions),
                    {"Preserve the complete dmesg output and stack trace.",
                     "Record the running kernel version and loaded driver modules.",
                     "Compare the reported call trace with the matching kernel source."})};
            },
            DiagnosisScope::kCollection,
        },
    };
}

std::vector<DiagnosisHypothesis> DiagnosisRuleEngine::Diagnose(
    const ChainReport& report) const {
    std::vector<DiagnosisHypothesis> hypotheses;
    for (const auto& rule : rules_) {
        if (!rule.matches(report, run_)) continue;
        auto generated = rule.generate(report, run_);
        for (auto& hypothesis : generated) {
            hypothesis.rule_id = rule.name;
            hypothesis.scope = rule.scope;
        }
        hypotheses.insert(hypotheses.end(), generated.begin(), generated.end());
    }
    SortHypotheses(&hypotheses);
    return hypotheses;
}

std::vector<DiagnosisReport> DiagnosisRuleEngine::DiagnoseAll(
    const std::vector<ChainReport>& chains) const {
    std::vector<DiagnosisReport> reports;
    DiagnosisReport run_wide;
    std::set<std::string> run_wide_rule_ids;

    for (const auto& chain : chains) {
        DiagnosisReport report;
        report.buffer_id = chain.buffer_id;
        auto hypotheses = Diagnose(chain);
        for (auto& hypothesis : hypotheses) {
            if (hypothesis.scope == DiagnosisScope::kCollection) {
                hypothesis.buffer_id.reset();
                if (run_wide_rule_ids.insert(hypothesis.rule_id).second) {
                    run_wide.hypotheses.push_back(std::move(hypothesis));
                }
            } else {
                hypothesis.buffer_id = chain.buffer_id;
                report.hypotheses.push_back(std::move(hypothesis));
            }
        }
        SortHypotheses(&report.hypotheses);
        report.has_root_cause_candidate = std::any_of(
            report.hypotheses.begin(), report.hypotheses.end(),
            [](const DiagnosisHypothesis& hypothesis) {
                return hypothesis.confidence == Confidence::kHigh;
            });
        reports.push_back(std::move(report));
    }

    bool has_complete = false;
    bool has_incomplete = false;
    std::set<Stage> failure_stages;
    std::vector<size_t> intermittent_indices;
    std::vector<std::string> intermittent_descriptions;
    for (const auto& chain : chains) {
        if (!chain.buffer_id) continue;
        if (chain.complete) {
            has_complete = true;
            if (!chain.reached_stages.empty()) {
                const auto& last = chain.reached_stages.back();
                intermittent_indices.push_back(last.event_index);
                intermittent_descriptions.push_back(
                    "Buffer " + std::to_string(*chain.buffer_id) + " reached " +
                    ToString(last.stage) + ".");
            }
        } else {
            has_incomplete = true;
            if (chain.first_missing_stage) failure_stages.insert(*chain.first_missing_stage);
            if (chain.last_reached_stage) {
                const auto* last = FindStage(chain, *chain.last_reached_stage);
                AddStageEvidence(last, "Buffer " + std::to_string(*chain.buffer_id) +
                                 " stopped after " + ToString(*chain.last_reached_stage) + ".",
                                 &intermittent_indices, &intermittent_descriptions);
            }
        }
    }
    if (has_complete && has_incomplete && failure_stages.size() > 1) {
           DiagnosisHypothesis intermittent_hypothesis = MakeHypothesis(
            "Intermittent Capture Failure (Not Systematic)",
            "Some buffers completed the pipeline while others stopped at different stages. This pattern is consistent with an intermittent condition, but does not identify its cause.",
            Confidence::kLow, std::move(intermittent_indices),
            std::move(intermittent_descriptions),
            {"Monitor device and system temperature during capture.",
             "Check dmesg for power or thermal throttling messages.",
               "Observe CPU and memory pressure while reproducing the issue."});
           intermittent_hypothesis.rule_id = "intermittent_capture_failure";
           intermittent_hypothesis.scope = DiagnosisScope::kCollection;
           run_wide.hypotheses.push_back(std::move(intermittent_hypothesis));
    }

    SortHypotheses(&run_wide.hypotheses);
    run_wide.has_root_cause_candidate = std::any_of(
        run_wide.hypotheses.begin(), run_wide.hypotheses.end(),
        [](const DiagnosisHypothesis& hypothesis) {
            return hypothesis.confidence == Confidence::kHigh;
        });
    if (!run_wide.hypotheses.empty()) reports.push_back(std::move(run_wide));
    return reports;
}

}  // namespace kdiag