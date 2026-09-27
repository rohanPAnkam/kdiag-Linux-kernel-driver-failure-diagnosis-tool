// Verifies subsystem/event classification and configured driver-name matching.
#include "kdiag/kernel_line_classifier.hpp"

#include <gtest/gtest.h>

namespace kdiag {
namespace {

class KernelLineClassifierTest : public ::testing::Test {
protected:
    KernelLineClassifier classifier_;
};

TEST_F(KernelLineClassifierTest, ClassifiesDmaTimeout) {
    const auto c = classifier_.Classify("driver_name: DMA timeout waiting for descriptor completion");
    // "driver_name" also matches, but "DMA timeout" is listed first, so it wins.
    EXPECT_EQ(c.component, Component::kDma);
    EXPECT_EQ(c.event, "dma_timeout");
}

TEST_F(KernelLineClassifierTest, ClassifiesUnhandledIrq) {
    const auto c = classifier_.Classify("irq 47: nobody cared (try booting with the \"irqpoll\" option)");
    EXPECT_EQ(c.component, Component::kIrq);
    EXPECT_EQ(c.event, "irq_unhandled");
}

TEST_F(KernelLineClassifierTest, ClassifiesPcieLinkDown) {
    const auto c = classifier_.Classify("pcieport 0000:00:1c.0: pciehp: Slot(0): Link Down");
    EXPECT_EQ(c.component, Component::kPci);
    EXPECT_EQ(c.event, "pci_link_down");
}

TEST_F(KernelLineClassifierTest, ClassifiesVb2BufferDone) {
    const auto c = classifier_.Classify("driver_name 0000:03:00.0: vb2_buffer_done: buffer 3 marked DONE");
    EXPECT_EQ(c.component, Component::kV4l2);
    EXPECT_EQ(c.event, "vb2_buffer_done");
}

TEST_F(KernelLineClassifierTest, FallsBackToGenericKernelLine) {
    const auto c = classifier_.Classify("systemd[1]: Started Daily apt upgrade and clean activities.");
    EXPECT_EQ(c.component, Component::kKernel);
    EXPECT_EQ(c.event, "generic_kernel_line");
}

TEST_F(KernelLineClassifierTest, DriverSpecificLineWithoutOtherKeywordsIsDriverComponent) {
    const KernelLineClassifier driver_classifier("driver_name");
    const auto c = driver_classifier.Classify("driver_name: probe failed with -ENODEV");
    EXPECT_EQ(c.component, Component::kDriver);
    EXPECT_EQ(c.event, "driver_log");
}

TEST_F(KernelLineClassifierTest, UsesConfiguredCustomDriverName) {
    const KernelLineClassifier driver_classifier("my_camera");
    const auto c = driver_classifier.Classify("my_camera: firmware state changed");

    EXPECT_EQ(c.component, Component::kDriver);
    EXPECT_EQ(c.event, "driver_log");
}

TEST_F(KernelLineClassifierTest, EmptyDriverNameDoesNotMatchEveryLine) {
    const KernelLineClassifier no_driver_classifier("");
    const auto c = no_driver_classifier.Classify("arbitrary kernel message");

    EXPECT_EQ(c.component, Component::kKernel);
    EXPECT_EQ(c.event, "generic_kernel_line");
}

}  // namespace
}  // namespace kdiag
