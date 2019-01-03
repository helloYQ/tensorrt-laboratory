/* Copyright (c) 2018, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#pragma once

#include <map>
#include <memory>
#include <mutex>
// #include <shared_mutex> /* C++17 - not found in g++ 5.4 */

#include <NvInfer.h>

#include "tensorrt/playground/core/pool.h"
#include "tensorrt/playground/core/thread_pool.h"
#include "tensorrt/playground/core/resources.h"
#include "tensorrt/playground/common.h"
#include "tensorrt/playground/model.h"
#include "tensorrt/playground/buffers.h"
#include "tensorrt/playground/execution_context.h"

namespace yais
{
namespace TensorRT
{

template<typename HostMemoryType, typename DeviceMemoryType>
class CyclicBuffers : public Buffers
{
  public:
    using HostAllocatorType = std::unique_ptr<Memory::CyclicAllocator<HostMemoryType>>;
    using DeviceAllocatorType = std::unique_ptr<Memory::CyclicAllocator<DeviceMemoryType>>;

    using HostDescriptor = typename Memory::CyclicAllocator<HostMemoryType>::Descriptor;
    using DeviceDescriptor = typename Memory::CyclicAllocator<DeviceMemoryType>::Descriptor;

    CyclicBuffers(HostAllocatorType host, DeviceAllocatorType device)
        : m_HostAllocator{std::move(host)}, m_DeviceAllocator{std::move(device)}
    {
    }
    ~CyclicBuffers() override {}

    HostDescriptor AllocateHost(size_t size)
    {
        return m_HostAllocator->Allocate(size);
    }

    DeviceDescriptor AllocateDevice(size_t size)
    {
        return m_DeviceAllocator->Allocate(size);
    }

    void Reset(bool writeZeros = false) final override {}
    void ConfigureBindings(const std::shared_ptr<Model>& model, std::shared_ptr<Bindings> bindings) override
    {
        for(uint32_t i = 0; i < model->GetBindingsCount(); i++)
        {
            auto binding_size = model->GetBinding(i).bytesPerBatchItem * model->GetMaxBatchSize();
            DLOG(INFO) << "Configuring Binding " << i << ": pushing " << binding_size
                       << " to host/device stacks";
            bindings->SetHostAddress(i, m_HostAllocator->Allocate(binding_size));
            bindings->SetDeviceAddress(i, m_DeviceAllocator->Allocate(binding_size));
        }
    }

  private:
    HostAllocatorType m_HostAllocator;
    DeviceAllocatorType m_DeviceAllocator;
};


/**
 * @brief TensorRT Resource Manager
 */
class InferenceManager : public ::yais::Resources
{
  public:
    InferenceManager(int max_executions, int max_buffers);
    virtual ~InferenceManager();

    void RegisterModel(const std::string& name, std::shared_ptr<Model> model);
    void RegisterModel(const std::string& name, std::shared_ptr<Model> model, uint32_t max_concurrency);

    // void RegisterModel(const std::string& name, const std::string& model_path, uint32_t max_concurrency);
    // void RegisterModel(const std::string& name, const std::string& model_path, uint32_t max_concurrency);

    void AllocateResources();

    auto GetBuffers() -> std::shared_ptr<Buffers>;
    auto GetModel(std::string model_name) -> std::shared_ptr<Model>;
    auto GetExecutionContext(const Model *model) -> std::shared_ptr<ExecutionContext>;
    auto GetExecutionContext(const std::shared_ptr<Model> &model) -> std::shared_ptr<ExecutionContext>;

    auto AcquireThreadPool(const std::string&) -> ThreadPool&;
    void RegisterThreadPool(const std::string&, std::unique_ptr<ThreadPool> threads);
    bool HasThreadPool(const std::string&) const;
    void JoinAllThreads();

  private:
    int m_MaxExecutions;
    int m_MaxBuffers;
    size_t m_HostStackSize;
    size_t m_DeviceStackSize;
    size_t m_ActivationsSize;
    std::shared_ptr<Pool<Buffers>> m_Buffers;
    std::shared_ptr<Pool<ExecutionContext>> m_ExecutionContexts;
    std::map<std::string, std::shared_ptr<Model>> m_Models;
    std::map<std::string, std::unique_ptr<ThreadPool>> m_ThreadPools;
    std::map<const Model *, std::shared_ptr<Pool<::nvinfer1::IExecutionContext>>> m_ModelExecutionContexts;
    // mutable std::shared_mutex m_ThreadPoolMutex;

    std::size_t Align(std::size_t size, std::size_t alignment)
    {
        std::size_t remainder = size % alignment;
        size = (remainder == 0) ? size : size + alignment - remainder;
        return size;
    }
};

} // namespace TensorRT
} // namespace yais
