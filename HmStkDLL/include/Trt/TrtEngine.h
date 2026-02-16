#pragma once

#include <string>
#include <vector>
#include <NvInfer.h>
#include <cuda_runtime.h>

class TrtEngine {
public:
    TrtEngine(const std::string& enginePath);
    ~TrtEngine();

    void infer(void** bindings, cudaStream_t stream);

    int getInputSize() const { return input_size_; }
    int getOutputSize() const { return output_size_; }

private:
    void loadEngine(const std::string& enginePath);
    void createContext();

    nvinfer1::IRuntime* runtime_{nullptr};
    nvinfer1::ICudaEngine* engine_{nullptr};
    nvinfer1::IExecutionContext* context_{nullptr};

    int input_size_{0};
    int output_size_{0};
};

