// SPDX-License-Identifier: AGPL-3.0-or-later
#include "MatrixStore.h"

#include "tg/preset.h"

namespace tg {

namespace {
const juce::Identifier kMatrixNode{"TorusGlideMatrix"};
const juce::Identifier kJsonProp{"json"};
} // namespace

MatrixStore::MatrixStore(juce::AudioProcessorValueTreeState& state) : state_(state) { writeToTree(matrixForPack(0)); }

void MatrixStore::set(const SceneMatrix& next) {
    mailbox_.set(next);
    writeToTree(next);
}

void MatrixStore::loadPack(int packIndex) { set(matrixForPack(packIndex)); }

void MatrixStore::writeToTree(const SceneMatrix& m) {
    auto node = state_.state.getOrCreateChildWithName(kMatrixNode, nullptr);
    node.setProperty(kJsonProp, juce::String(serialiseMatrixJson(m)), nullptr);
}

void MatrixStore::reloadFromState() {
    const auto node = state_.state.getChildWithName(kMatrixNode);
    if (node.isValid() && node.hasProperty(kJsonProp)) {
        if (const auto parsed = parseMatrixJson(node.getProperty(kJsonProp).toString().toStdString())) {
            mailbox_.set(*parsed);
            return;
        }
    }
    mailbox_.set(matrixForPack(0));
}

} // namespace tg
