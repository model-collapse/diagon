// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/util/FST.h"

#include "diagon/util/Exceptions.h"

#include <algorithm>
#include <stdexcept>
#include <map>
#include <functional>

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

/**
 * Simplified FST serialization format:
 *
 * [numNodes:vint]
 * For each node (depth-first traversal):
 *   [isFinal:byte]
 *   [output:vlong] (if isFinal)
 *   [numArcs:vint]
 *   For each arc:
 *     [label:byte]
 *     [arcOutput:vlong]
 *     [targetNodeId:vint]
 *
 * Node IDs are assigned during depth-first traversal.
 * Target node IDs refer to nodes that will be serialized later.
 */

namespace {
    // Helper class for serialization
    class FSTSerializer {
    public:
        std::vector<uint8_t> data;
        std::map<const FST::Node*, int> nodeIds;
        int nextNodeId = 0;

        void writeVInt(int value) {
            // VByte encoding for int
            while (value > 0x7F) {
                data.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
                value >>= 7;
            }
            data.push_back(static_cast<uint8_t>(value));
        }

        void writeVLong(int64_t value) {
            // VByte encoding for int64
            while (value > 0x7F) {
                data.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
                value >>= 7;
            }
            data.push_back(static_cast<uint8_t>(value));
        }

        void writeByte(uint8_t value) {
            data.push_back(value);
        }

        // Assign node IDs via depth-first traversal
        void assignNodeIds(FST::Node* node) {
            if (nodeIds.find(node) != nodeIds.end()) {
                return; // Already visited
            }
            nodeIds[node] = nextNodeId++;

            for (const auto& arc : node->arcs) {
                assignNodeIds(arc.target);
            }
        }

        // Serialize node and its children
        void serializeNode(const FST::Node* node) {
            writeByte(node->isFinal ? 1 : 0);
            if (node->isFinal) {
                writeVLong(node->output);
            }

            writeVInt(static_cast<int>(node->arcs.size()));
            for (const auto& arc : node->arcs) {
                writeByte(arc.label);
                writeVLong(arc.output);
                writeVInt(nodeIds.at(arc.target));
            }
        }
    };

    // Helper class for deserialization
    class FSTDeserializer {
    public:
        const uint8_t* data;
        size_t pos = 0;
        size_t size;
        std::vector<FST::Node*> nodes;

        FSTDeserializer(const std::vector<uint8_t>& bytes)
            : data(bytes.data()), size(bytes.size()) {}

        int readVInt() {
            int result = 0;
            int shift = 0;
            uint8_t b;
            do {
                if (pos >= size) throw std::runtime_error("FST deserialization failed: unexpected end");
                b = data[pos++];
                result |= (b & 0x7F) << shift;
                shift += 7;
            } while (b & 0x80);
            return result;
        }

        int64_t readVLong() {
            int64_t result = 0;
            int shift = 0;
            uint8_t b;
            do {
                if (pos >= size) throw std::runtime_error("FST deserialization failed: unexpected end");
                b = data[pos++];
                result |= static_cast<int64_t>(b & 0x7F) << shift;
                shift += 7;
            } while (b & 0x80);
            return result;
        }

        uint8_t readByte() {
            if (pos >= size) throw std::runtime_error("FST deserialization failed: unexpected end");
            return data[pos++];
        }
    };
}

std::vector<uint8_t> FST::serialize() const {
    if (!root_) {
        return std::vector<uint8_t>();
    }

    FSTSerializer serializer;

    // First pass: assign node IDs
    serializer.assignNodeIds(root_.get());

    // Write number of nodes
    serializer.writeVInt(serializer.nextNodeId);

    // Second pass: serialize all nodes in ID order
    std::vector<const Node*> nodesByID(serializer.nextNodeId);
    for (const auto& [node, id] : serializer.nodeIds) {
        nodesByID[id] = node;
    }

    for (const auto* node : nodesByID) {
        serializer.serializeNode(node);
    }

    return serializer.data;
}

std::unique_ptr<FST> FST::deserialize(const std::vector<uint8_t>& data) {
    if (data.empty()) {
        return std::make_unique<FST>();
    }

    FSTDeserializer deserializer(data);

    // Read number of nodes
    int numNodes = deserializer.readVInt();
    if (numNodes == 0) {
        return std::make_unique<FST>();
    }

    // Pre-allocate all nodes
    deserializer.nodes.resize(numNodes);
    for (int i = 0; i < numNodes; i++) {
        deserializer.nodes[i] = new Node();
    }

    // Read each node's data
    for (int i = 0; i < numNodes; i++) {
        Node* node = deserializer.nodes[i];

        // Read isFinal and output
        node->isFinal = (deserializer.readByte() != 0);
        if (node->isFinal) {
            node->output = deserializer.readVLong();
        }

        // Read arcs
        int numArcs = deserializer.readVInt();
        node->arcs.reserve(numArcs);
        for (int j = 0; j < numArcs; j++) {
            uint8_t label = deserializer.readByte();
            Output arcOutput = deserializer.readVLong();
            int targetId = deserializer.readVInt();

            if (targetId < 0 || targetId >= numNodes) {
                // Clean up allocated nodes
                for (auto* n : deserializer.nodes) {
                    delete n;
                }
                throw std::runtime_error("FST deserialization failed: invalid target node ID");
            }

            node->arcs.emplace_back(label, deserializer.nodes[targetId], arcOutput);
        }

        // Arcs should already be sorted from serialization, but ensure it
        std::sort(node->arcs.begin(), node->arcs.end());
    }

    // Create FST and transfer ownership of root
    auto fst = std::make_unique<FST>();
    fst->root_.reset(deserializer.nodes[0]); // Root is always node 0

    // Nodes are now owned by the FST (root owns the tree)
    // Don't delete them in deserializer cleanup

    return fst;
}

std::vector<std::pair<std::vector<uint8_t>, FST::Output>> FST::getAllEntries() const {
    std::vector<std::pair<std::vector<uint8_t>, Output>> entries;

    if (!root_) {
        return entries;
    }

    // Depth-first traversal to collect all final states
    std::vector<uint8_t> currentPath;
    std::function<void(Node*, Output)> traverse = [&](Node* node, Output accOutput) {
        if (node->isFinal) {
            // Found a final state - record this entry
            entries.emplace_back(currentPath, accOutput + node->output);
        }

        // Recurse on all arcs
        for (const auto& arc : node->arcs) {
            currentPath.push_back(arc.label);
            traverse(arc.target, accOutput + arc.output);
            currentPath.pop_back();
        }
    };

    traverse(root_.get(), 0);
    return entries;
}

}  // namespace util
}  // namespace diagon
