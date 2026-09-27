// Covers local fallback, mocked provider parsing, and evidence-citation validation.
#include "kdiag/llm_explainer.hpp"

#include <gtest/gtest.h>

namespace kdiag {
namespace {

TEST(LlmExplainerTest, UsesLocalFallbackWithoutAnApiKey) {
    CollectionRun run;
    DiagnosisReport report;
    report.hypotheses.push_back({
        "DMA Completion Interrupt Not Observed",
        "No completion was recorded after descriptor submission.",
        Confidence::kHigh,
        {},
        {},
        {"Check /proc/interrupts."},
        std::nullopt,
        "",
        DiagnosisScope::kBuffer,
    });

    const auto explanation = LlmExplainer().Explain({report, run, false});

    EXPECT_TRUE(explanation.used_local_fallback);
    EXPECT_NE(explanation.executive_summary.find("DMA Completion"), std::string::npos);
    EXPECT_NE(explanation.remediation_steps.find("/proc/interrupts"), std::string::npos);
}

TEST(LlmExplainerTest, InjectedTransportParsesAnthropicResponse) {
    CollectionRun run;
    run.pci_bdf = "0000:03:00.0";
    run.events.push_back({1, Source::kDmesg, Component::kDriver, "dma_descriptor",
                          "descriptor submitted", "raw descriptor", nlohmann::json::object()});
    DiagnosisReport report;
    report.buffer_id = 1832;
    report.hypotheses.push_back({
        "DMA timeout", "No completion was observed.", Confidence::kHigh,
        {0}, {"Descriptor submitted."}, {"Inspect dmesg."},
        std::nullopt,
        "",
        DiagnosisScope::kBuffer,
    });

    std::string sent_url;
    std::string sent_key;
    std::string sent_body;
    LlmPostRequest fake_post = [&](const std::string& url, const std::string& key,
                                   const std::string& body) {
        sent_url = url;
        sent_key = key;
        sent_body = body;
        return R"({"content":[{"type":"text","text":"{\"executive_summary\":\"Frame 1832 stopped [event 0].\",\"technical_explanation\":\"The descriptor is the cited observation [event 0].\",\"remediation_steps\":\"Inspect the DMA and IRQ logs.\",\"references\":\"None\"}"}]})";
    };

    const auto explanation = LlmExplainer("test-secret", "test-model", "https://example.invalid",
                                         fake_post).Explain({report, run, true});

    EXPECT_EQ(sent_url, "https://example.invalid");
    EXPECT_EQ(sent_key, "test-secret");
    EXPECT_NE(sent_body.find("raw descriptor"), std::string::npos);
    EXPECT_NE(sent_body.find("index"), std::string::npos);
    EXPECT_NE(sent_body.find("rule_id"), std::string::npos);
    EXPECT_NE(sent_body.find("scope"), std::string::npos);
    EXPECT_NE(sent_body.find("untrusted data"), std::string::npos);
    EXPECT_EQ(explanation.executive_summary, "Frame 1832 stopped [event 0].");
    EXPECT_FALSE(explanation.used_local_fallback);
}

TEST(LlmExplainerTest, RejectsEvidenceReferencesOutsideCollectionRun) {
    CollectionRun run;
    DiagnosisReport report;
    report.hypotheses.push_back(
        {"bad", "bad", Confidence::kLow, {9}, {"invalid"}, {}, std::nullopt,
         "", DiagnosisScope::kBuffer});
    bool transport_called = false;
    LlmPostRequest fake_post = [&](const std::string&, const std::string&,
                                   const std::string&) {
        transport_called = true;
        return std::string();
    };

    EXPECT_THROW(LlmExplainer("key", "model", "endpoint", fake_post)
                     .Explain({report, run, false}), std::invalid_argument);
    EXPECT_FALSE(transport_called);
}

TEST(LlmExplainerTest, RejectsMalformedExplanationPayload) {
    CollectionRun run;
    run.events.push_back(Event{});
    DiagnosisReport report;
    report.hypotheses.push_back(
        {"test", "test", Confidence::kMedium, {0}, {"event"}, {}, std::nullopt,
         "", DiagnosisScope::kBuffer});
    LlmPostRequest fake_post = [](const std::string&, const std::string&,
                                  const std::string&) {
        return R"({"content":[{"type":"text","text":"not json"}]})";
    };

    EXPECT_THROW(LlmExplainer("key", "model", "endpoint", fake_post)
                     .Explain({report, run, false}), nlohmann::json::exception);
}

TEST(LlmExplainerTest, RejectsCitationToUnprovidedEvent) {
    CollectionRun run;
    run.events.push_back(Event{});
    DiagnosisReport report;
    report.hypotheses.push_back(
        {"test", "test", Confidence::kMedium, {0}, {"event"}, {}, std::nullopt,
         "", DiagnosisScope::kBuffer});
    LlmPostRequest fake_post = [](const std::string&, const std::string&,
                                  const std::string&) {
        return R"({"content":[{"type":"text","text":"{\"executive_summary\":\"Failure [event 9].\",\"technical_explanation\":\"Event [event 0] is the only evidence.\",\"remediation_steps\":\"Review the event.\",\"references\":\"None\"}"}]})";
    };

    EXPECT_THROW(LlmExplainer("key", "model", "endpoint", fake_post)
                     .Explain({report, run, false}), std::runtime_error);
}

}  // namespace
}  // namespace kdiag