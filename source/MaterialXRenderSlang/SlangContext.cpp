//
// Copyright Contributors to the MaterialX Project
// SPDX-License-Identifier: Apache-2.0
//

#include <MaterialXRenderSlang/SlangContext.h>
#include <MaterialXRenderSlang/SlangTypeUtils.h>
#include <MaterialXRenderSlang/SlangBlit.h>
#include <MaterialXRenderSlang/SlangShaderCache.h>
#include <string>
#include <fstream>

#if defined(_WIN32) || defined(_WIN64)
    #pragma push_macro("NOMINMAX")
    #undef NOMINMAX
    #define NOMINMAX
    #include <d3d12.h>
    #include <comdef.h>
    #pragma pop_macro("NOMINMAX")
#endif

MATERIALX_NAMESPACE_BEGIN

namespace
{

rhi::DeviceType getDeviceType(std::string_view deviceType)
{
    using namespace rhi;

    if (deviceType == "Default")
    {
        return DeviceType::Default;
    }
    if (deviceType == "D3D12")
    {
        return DeviceType::D3D12;
    }
    if (deviceType == "Vulkan")
    {
        return DeviceType::Vulkan;
    }
    if (deviceType == "Metal")
    {
        return DeviceType::Metal;
    }
    if (deviceType == "WGPU")
    {
        return DeviceType::WGPU;
    }
    throw ExceptionRenderError("Unknown deviceType request: " + std::string(deviceType));
}

SlangContextOptions makeDefaultContextOptions(std::string_view deviceType)
{
    SlangContextOptions options;
    options.deviceType = std::string(deviceType);
    options.shaderCachePath = FilePath("./shadercache");
    return options;
}

} // namespace

SlangContext::SlangContext(std::string_view deviceType) :
    SlangContext(makeDefaultContextOptions(deviceType))
{
}

SlangContext::SlangContext(const SlangContextOptions& options)
{
    using namespace rhi;

    if (options.moduleCachePath)
    {
        throw ExceptionRenderError("Slang module cache is not yet implemented in RenderSlang.");
    }

    rhi::getRHI()->enableDebugLayers();
    slang::createGlobalSession(&_slangGlobalSession);

    _debugCallback = std::make_unique<SlangDebugCallback>();
    if (options.shaderCachePath)
    {
        _shaderCache = new SlangShaderCache(*options.shaderCachePath);
    }

    DeviceDesc deviceDesc = {};
    deviceDesc.deviceType = getDeviceType(options.deviceType);

    std::vector<const char*> searchPaths;
    std::vector<slang::PreprocessorMacroDesc> preprocessorMacros;
    std::vector<slang::CompilerOptionEntry> compilerOptions;

    slang::CompilerOptionEntry compilerOption;
    compilerOption.name = slang::CompilerOptionName::EmitSpirvDirectly;
    compilerOption.value.intValue0 = 1;
    compilerOptions.push_back(compilerOption);

    deviceDesc.slang.slangGlobalSession = _slangGlobalSession;
    deviceDesc.slang.searchPaths = searchPaths.data();
    deviceDesc.slang.searchPathCount = (uint32_t) searchPaths.size();
    deviceDesc.slang.preprocessorMacros = preprocessorMacros.data();
    deviceDesc.slang.preprocessorMacroCount = (uint32_t) preprocessorMacros.size();
    deviceDesc.slang.compilerOptionEntries = compilerOptions.data();
    deviceDesc.slang.compilerOptionEntryCount = (uint32_t) compilerOptions.size();
    deviceDesc.debugCallback = _debugCallback.get();
    deviceDesc.persistentShaderCache = _shaderCache.get();
    deviceDesc.persistentPipelineCache = _shaderCache.get();

#ifdef _DEBUG
    deviceDesc.enableValidation = true;
    rhi::getRHI()->enableDebugLayers();
#endif

    rhi::getRHI()->createDevice(deviceDesc, _device.writeRef());

    _queue = _device->getQueue(rhi::QueueType::Graphics);
    _blitter = std::make_shared<SlangBlit>(this);
}

SlangContext::~SlangContext()
{
    _blitter.reset();
}

SlangContextPtr SlangContext::create(std::string_view deviceType)
{
    return std::make_shared<SlangContext>(deviceType);
}

SlangContextPtr SlangContext::create(const SlangContextOptions& options)
{
    return std::make_shared<SlangContext>(options);
}

MATERIALX_NAMESPACE_END
