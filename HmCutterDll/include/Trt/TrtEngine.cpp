#include "TrtEngine.h"
#include <fstream>
#include <iostream>

TrtEngine::TrtEngine(const std::string& enginePath) {
    loadEngine(enginePath);
    createContext();
}

TrtEngine::~TrtEngine() {
    if (context_) {
        context_->destroy();
    }
    if (engine_) {
        engine_->destroy();
    }
    if (runtime_) {
        runtime_->destroy();
    }
}

void TrtEngine::loadEngine(const std::string& enginePath) {
    std::ifstream file(enginePath, std::ios::binary);
    if (!file.good()) {
        throw std::runtime_error("Failed to open engine file: " + enginePath);
    }

    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> engineData(size);
    file.read(engineData.data(), size);
    file.close();

    runtime_ = nvinfer1::createInferRuntime(nullptr);
    if (!runtime_) {
        throw std::runtime_error("Failed to create TensorRT runtime");
    }

    engine_ = runtime_->deserializeCudaEngine(engineData.data(), size, nullptr);
    if (!engine_) {
        throw std::runtime_error("Failed to deserialize CUDA engine");
    }

    // Get input/output sizes
    if (engine_->getNbBindings() >= 2) {
        input_size_ = engine_->getBindingDimensions(0).d[0] *
                      engine_->getBindingDimensions(0).d[1] *
                      engine_->getBindingDimensions(0).d[2] *
                      engine_->getBindingDimensions(0).d[3];
        output_size_ = engine_->getBindingDimensions(1).d[0] *
                       engine_->getBindingDimensions(1).d[1] *
                       engine_->getBindingDimensions(1).d[2] *
                       engine_->getBindingDimensions(1).d[3];
    }
}

void TrtEngine::createContext() {
    if (!engine_) {
        throw std::runtime_error("Engine not loaded");
    }
    context_ = engine_->createExecutionContext();
    if (!context_) {
        throw std::runtime_error("Failed to create execution context");
    }
}

void TrtEngine::infer(void** bindings, cudaStream_t stream) {
    if (!context_) {
        throw std::runtime_error("Context not initialized");
    }
    context_->enqueueV2(bindings, stream, nullptr);
}

