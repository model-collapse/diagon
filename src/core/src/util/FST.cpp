// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/util/FST.h"

#include "diagon/util/Exceptions.h"

#include <algorithm>
#include <stdexcept>

namespace diagon {
namespace util {

// ==================== FST ====================

FST::FST()
    : root_(std::make_unique<Node>()) {}

FST::Output FST::get(const BytesRef& input) const {
    if (!root_) {
        return NO_OUTPUT;
    }

    Node* current = root_.get();
    Output accOutput = 0;

    for (size_t i = 0; i < input.length(); i++) {
        const Arc* arc = current->findArc(input[i]);
        if (!arc) {
            return NO_OUTPUT;  // No matching arc
        }
        accOutput += arc->output;
        current = arc->target;
    }

    return current->isFinal ? (accOutput + current->output) : NO_OUTPUT;
}

FST::Output FST::getLongestPrefixMatch(const BytesRef& input, int& prefixLen) const {
    if (!root_) {
        prefixLen = 0;
        return NO_OUTPUT;
    }

    Node* current = root_.get();
    Output accOutput = 0;
    Output lastFinalOutput = NO_OUTPUT;
    int lastFinalPos = 0;

    // Check if root is final (empty string match)
    if (current->isFinal) {
        lastFinalOutput = current->output;
        lastFinalPos = 0;
    }

    for (size_t i = 0; i < input.length(); i++) {
        const Arc* arc = current->findArc(input[i]);
        if (!arc) {
            break;
        }
        accOutput += arc->output;
        current = arc->target;

        // After consuming byte i, check if we're at a final state
        if (current->isFinal) {
            lastFinalOutput = accOutput + current->output;
            lastFinalPos = static_cast<int>(i + 1);  // We've consumed i+1 bytes
        }
    }

    prefixLen = lastFinalPos;
    return lastFinalOutput;
}

FST::Node::~Node() {
    // Recursively delete all child nodes
    for (Arc& arc : arcs) {
        delete arc.target;
        arc.target = nullptr;
    }
}

const FST::Arc* FST::Node::findArc(uint8_t label) const {
    // Binary search for arc with matching label
    auto it = std::lower_bound(arcs.begin(), arcs.end(), label,
                               [](const Arc& arc, uint8_t val) { return arc.label < val; });

    if (it != arcs.end() && it->label == label) {
        return &(*it);
    }
    return nullptr;
}

// ==================== FST::Builder ====================

FST::Builder::Builder()
    : root_(new Node())
    , finished_(false) {}

FST::Builder::~Builder() {
    // Clean up tree if not transferred to FST via finish()
    if (root_ != nullptr) {
        deleteNodeRecursive(root_);
        root_ = nullptr;
    }
}

void FST::Builder::deleteNodeRecursive(Node* node) {
    // Node destructor now handles recursive deletion of children
    delete node;
}

void FST::Builder::add(const BytesRef& input, Output output) {
    if (finished_) {
        throw std::runtime_error("FST already finished");
    }

    // Check sorted order (also reject duplicates)
    if (lastInput_.length() > 0 && input <= lastInput_) {
        throw std::invalid_argument("Inputs must be added in sorted order");
    }

    // Find common prefix with last input
    size_t prefixLen = 0;
    size_t minLen = std::min(lastInput_.length(), input.length());
    while (prefixLen < minLen && lastInput_[prefixLen] == input[prefixLen]) {
        prefixLen++;
    }

    // Traverse to common prefix node
    Node* current = root_;
    for (size_t i = 0; i < prefixLen; i++) {
        const Arc* arc = current->findArc(lastInput_[i]);
        if (!arc) {
            throw std::runtime_error("FST structure corrupted");
        }
        current = arc->target;
    }

    // Add new path for remaining bytes
    for (size_t i = prefixLen; i < input.length(); i++) {
        Node* newNode = new Node();
        Arc newArc(input[i], newNode);
        current->arcs.push_back(newArc);
        std::sort(current->arcs.begin(), current->arcs.end());
        current = newNode;
    }

    // Mark final node
    current->isFinal = true;
    current->output = output;

    // Record entry for serialization
    entries_.emplace_back(input, output);

    // Save last input
    lastInputData_.assign(input.data(), input.data() + input.length());
    lastInput_ = BytesRef(lastInputData_.data(), lastInputData_.size());
}

std::unique_ptr<FST> FST::Builder::finish() {
    if (finished_) {
        throw std::runtime_error("FST already finished");
    }

    finished_ = true;

    auto fst = std::make_unique<FST>();
    fst->root_.reset(root_);  // Transfer ownership
    root_ = nullptr;
    return fst;
}

// ==================== Serialization ====================

std::vector<uint8_t> FST::serialize() const {
    // TODO: Implement proper serialization
    // For Phase 2 MVP, we'll keep FST in memory
    throw std::runtime_error("FST serialization not yet implemented");
}

std::unique_ptr<FST> FST::deserialize(const std::vector<uint8_t>& data) {
    // TODO: Implement proper deserialization
    throw std::runtime_error("FST deserialization not yet implemented");
}

}  // namespace util
}  // namespace diagon
