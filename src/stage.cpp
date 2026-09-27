// Defines pipeline order, stage names, and message-to-stage classification rules.
#include "kdiag/stage.hpp"

namespace kdiag {

const std::vector<Stage>& PipelineOrder() {
    static const std::vector<Stage> kOrder = {
        Stage::kPciProbe,       Stage::kDeviceInit,          Stage::kBufferAlloc,
        Stage::kDmaMapping,     Stage::kStreamOn,            Stage::kDescriptorSubmission,
        Stage::kIrq,            Stage::kDmaCompletion,       Stage::kVb2BufferDone,
        Stage::kV4l2Dequeue,    Stage::kGstreamerConsume,
    };
    return kOrder;
}

std::string ToString(Stage s) {
    switch (s) {
        case Stage::kPciProbe:            return "pci_probe";
        case Stage::kDeviceInit:          return "device_init";
        case Stage::kBufferAlloc:         return "buffer_alloc";
        case Stage::kDmaMapping:          return "dma_mapping";
        case Stage::kStreamOn:            return "stream_on";
        case Stage::kDescriptorSubmission:return "descriptor_submission";
        case Stage::kIrq:                 return "irq";
        case Stage::kDmaCompletion:       return "dma_completion";
        case Stage::kVb2BufferDone:       return "vb2_buffer_done";
        case Stage::kV4l2Dequeue:         return "v4l2_dequeue";
        case Stage::kGstreamerConsume:    return "gstreamer_consume";
    }
    return "unknown_stage";
}

// Ordered most-specific-first, same convention as KernelLineClassifier:
// the first substring match wins. Every substring here was chosen to
// match the literal log wording used in the project's own worked
// example plus common Linux V4L2/DMA driver phrasing (v4l2-core,
// videobuf2-core use very similar wording across drivers).
StageClassifier::StageClassifier() {
    rules_ = {
        // --- PCI probe / device init ---
        {"probe failed",             Stage::kPciProbe},
        {": probe",                  Stage::kPciProbe},
        {"probing device",           Stage::kPciProbe},
        {"device initialized",       Stage::kDeviceInit},
        {"device_init",              Stage::kDeviceInit},
        {"chip init",                Stage::kDeviceInit},

        // --- buffer allocation ---
        {"buffer allocated",         Stage::kBufferAlloc},
        {"buffer queued",            Stage::kBufferAlloc},
        {"vb2_queue_setup",          Stage::kBufferAlloc},
        {"buf alloc",                Stage::kBufferAlloc},

        // --- DMA mapping ---
        {"dma_map_single",           Stage::kDmaMapping},
        {"dma_map_sg",               Stage::kDmaMapping},
        {"DMA mapped",               Stage::kDmaMapping},
        {"dma mapped",               Stage::kDmaMapping},

        // --- streaming start ---
        {"streaming started",        Stage::kStreamOn},
        {"stream on",                Stage::kStreamOn},
        {"STREAMON",                 Stage::kStreamOn},
        {"vidioc_streamon",          Stage::kStreamOn},

        // --- descriptor submission ---
        {"descriptor submitted",     Stage::kDescriptorSubmission},
        {"descriptor queued",        Stage::kDescriptorSubmission},
        {"submitted for buffer",     Stage::kDescriptorSubmission},

        // --- interrupt handling (must come after the more specific DMA
        //     mapping/descriptor rules above, since some of those
        //     messages also contain the word "interrupt") ---
        {"interrupt received",       Stage::kIrq},
        {"irq_handler",              Stage::kIrq},
        {"ISR fired",                Stage::kIrq},

        // --- DMA completion ---
        {"dma completion",           Stage::kDmaCompletion},
        {"DMA complete",             Stage::kDmaCompletion},
        {"dma_complete",             Stage::kDmaCompletion},
        {"completion interrupt",     Stage::kDmaCompletion},

        // --- vb2 / V4L2 dequeue ---
        {"vb2_buffer_done",          Stage::kVb2BufferDone},
        {"VIDIOC_DQBUF",             Stage::kV4l2Dequeue},
        {"vidioc_dqbuf",             Stage::kV4l2Dequeue},
        {"buffer dequeued",          Stage::kV4l2Dequeue},

        // --- GStreamer consumption (only ever seen in gstreamer_log
        //     source events, but the pattern lives here so a future
        //     collector can reuse it without duplicating the table) ---
        {"appsink",                  Stage::kGstreamerConsume},
        {"new-sample",               Stage::kGstreamerConsume},
    };
}

std::optional<Stage> StageClassifier::Classify(const std::string& message) const {
    for (const auto& rule : rules_) {
        if (message.find(rule.substring) != std::string::npos) return rule.stage;
    }
    return std::nullopt;
}

}  // namespace kdiag
