/* Copyright (c) 2015-2023 The Khronos Group Inc.
 * Copyright (c) 2015-2023 Valve Corporation
 * Copyright (c) 2015-2023 LunarG, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Author: Courtney Goeltzenleuchter <courtney@LunarG.com>
 * Author: Tobin Ehlis <tobin@lunarg.com>
 * Author: Mark Young <marky@lunarg.com>
 * Author: Dave Houlton <daveh@lunarg.com>
 *
 */

#ifndef LAYER_LOGGING_H
#define LAYER_LOGGING_H

#include <array>
#include <cstdarg>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "vk_layer_config.h"
#include "vk_layer_data.h"
#include "vk_layer_dispatch_table.h"
#include "vk_object_types.h"
#include "vk_typemap_helper.h"

#if defined __ANDROID__
#include <android/log.h>
#define LOGCONSOLE(...) ((void)__android_log_print(ANDROID_LOG_INFO, "VALIDATION", __VA_ARGS__))
[[maybe_unused]] static const char *kForceDefaultCallbackKey = "debug.vvl.forcelayerlog";
#endif

[[maybe_unused]] static const char *kVUIDUndefined = "VUID_Undefined";

typedef enum DebugCallbackStatusBits {
    DEBUG_CALLBACK_UTILS = 0x00000001,     // This struct describes a VK_EXT_debug_utils callback
    DEBUG_CALLBACK_DEFAULT = 0x00000002,   // An internally created callback, used if no user-defined callbacks are registered
    DEBUG_CALLBACK_INSTANCE = 0x00000004,  // An internally created temporary instance callback
} DebugCallbackStatusBits;
typedef VkFlags DebugCallbackStatusFlags;

struct LogObjectList {
    small_vector<VulkanTypedHandle, 4, uint32_t> object_list;

    template <typename HANDLE_T>
    void add(HANDLE_T object) {
        object_list.emplace_back(object, ConvertCoreObjectToVulkanObject(VkHandleInfo<HANDLE_T>::kVkObjectType));
    }

    void add(VulkanTypedHandle typed_handle) { object_list.emplace_back(typed_handle); }

    template <typename HANDLE_T>
    LogObjectList(HANDLE_T object) {
        add(object);
    }

    template <typename... HANDLE_T>
    LogObjectList(HANDLE_T... objects) {
        (..., add(objects));
    }

    LogObjectList(){};
};

typedef struct VkLayerDbgFunctionState {
    DebugCallbackStatusFlags callback_status;

    // Debug report related information
    VkDebugReportCallbackEXT debug_report_callback_object;
    PFN_vkDebugReportCallbackEXT debug_report_callback_function_ptr;
    VkFlags debug_report_msg_flags;

    // Debug utils related information
    VkDebugUtilsMessengerEXT debug_utils_callback_object;
    VkDebugUtilsMessageSeverityFlagsEXT debug_utils_msg_flags;
    VkDebugUtilsMessageTypeFlagsEXT debug_utils_msg_type;
    PFN_vkDebugUtilsMessengerCallbackEXT debug_utils_callback_function_ptr;

    void *pUserData;

    bool IsUtils() const { return ((callback_status & DEBUG_CALLBACK_UTILS) != 0); }
    bool IsDefault() const { return ((callback_status & DEBUG_CALLBACK_DEFAULT) != 0); }
    bool IsInstance() const { return ((callback_status & DEBUG_CALLBACK_INSTANCE) != 0); }
} VkLayerDbgFunctionState;

// TODO: Could be autogenerated for the specific handles for extra type safety...
template <typename HANDLE_T>
static inline uint64_t HandleToUint64(HANDLE_T h) {
    return CastToUint64<HANDLE_T>(h);
}

static inline uint64_t HandleToUint64(uint64_t h) { return h; }

// Data we store per label for logging
struct LoggingLabel {
    std::string name;
    std::array<float, 4> color;

    void Reset() { *this = LoggingLabel(); }
    bool Empty() const { return name.empty(); }

    VkDebugUtilsLabelEXT Export() const {
        auto out = LvlInitStruct<VkDebugUtilsLabelEXT>();
        out.pLabelName = name.c_str();
        std::copy(color.cbegin(), color.cend(), out.color);
        return out;
    };

    LoggingLabel() : name(), color({{0.f, 0.f, 0.f, 0.f}}) {}
    LoggingLabel(const VkDebugUtilsLabelEXT *label_info) {
        if (label_info && label_info->pLabelName) {
            name = label_info->pLabelName;
            std::copy_n(std::begin(label_info->color), 4, color.begin());
        } else {
            Reset();
        }
    }

    LoggingLabel(const LoggingLabel &) = default;
    LoggingLabel &operator=(const LoggingLabel &) = default;
    LoggingLabel &operator=(LoggingLabel &&) = default;
    LoggingLabel(LoggingLabel &&) = default;

    template <typename Name, typename Vec>
    LoggingLabel(Name &&name_, Vec &&vec_) : name(std::forward<Name>(name_)), color(std::forward<Vec>(vec_)) {}
};

struct LoggingLabelState {
    std::vector<LoggingLabel> labels;
    LoggingLabel insert_label;

    // Export the labels, but in reverse order since we want the most recent at the top.
    std::vector<VkDebugUtilsLabelEXT> Export() const {
        size_t count = labels.size() + (insert_label.Empty() ? 0 : 1);
        std::vector<VkDebugUtilsLabelEXT> out(count);

        if (!count) return out;

        size_t index = count - 1;
        if (!insert_label.Empty()) {
            out[index--] = insert_label.Export();
        }
        for (const auto &label : labels) {
            out[index--] = label.Export();
        }
        return out;
    }
};

typedef struct _debug_report_data {
    std::vector<VkLayerDbgFunctionState> debug_callback_list;
    VkDebugUtilsMessageSeverityFlagsEXT active_severities{0};
    VkDebugUtilsMessageTypeFlagsEXT active_types{0};
    bool queueLabelHasInsert{false};
    bool cmdBufLabelHasInsert{false};
    vvl::unordered_map<uint64_t, std::string> debugObjectNameMap;
    vvl::unordered_map<uint64_t, std::string> debugUtilsObjectNameMap;
    vvl::unordered_map<VkQueue, std::unique_ptr<LoggingLabelState>> debugUtilsQueueLabels;
    vvl::unordered_map<VkCommandBuffer, std::unique_ptr<LoggingLabelState>> debugUtilsCmdBufLabels;
    std::vector<uint32_t> filter_message_ids{};
    // This mutex is defined as mutable since the normal usage for a debug report object is as 'const'. The mutable keyword allows
    // the layers to continue this pattern, but also allows them to use/change this specific member for synchronization purposes.
    mutable std::mutex debug_output_mutex;
    int32_t duplicate_message_limit = 0;
    mutable vvl::unordered_map<uint32_t, int32_t> duplicate_message_count_map{};
    const void *instance_pnext_chain{};
    bool forceDefaultLogCallback{false};

    void DebugReportSetUtilsObjectName(const VkDebugUtilsObjectNameInfoEXT *pNameInfo) {
        std::unique_lock<std::mutex> lock(debug_output_mutex);
        if (pNameInfo->pObjectName) {
            debugUtilsObjectNameMap[pNameInfo->objectHandle] = pNameInfo->pObjectName;
        } else {
            debugUtilsObjectNameMap.erase(pNameInfo->objectHandle);
        }
    }

    void DebugReportSetMarkerObjectName(const VkDebugMarkerObjectNameInfoEXT *pNameInfo) {
        std::unique_lock<std::mutex> lock(debug_output_mutex);
        if (pNameInfo->pObjectName) {
            debugObjectNameMap[pNameInfo->object] = pNameInfo->pObjectName;
        } else {
            debugObjectNameMap.erase(pNameInfo->object);
        }
    }

    std::string DebugReportGetUtilsObjectName(const uint64_t object) const {
        std::string label = "";
        const auto utils_name_iter = debugUtilsObjectNameMap.find(object);
        if (utils_name_iter != debugUtilsObjectNameMap.end()) {
            label = utils_name_iter->second;
        }
        return label;
    }

    std::string DebugReportGetMarkerObjectName(const uint64_t object) const {
        std::string label = "";
        const auto marker_name_iter = debugObjectNameMap.find(object);
        if (marker_name_iter != debugObjectNameMap.end()) {
            label = marker_name_iter->second;
        }
        return label;
    }

    std::string FormatHandle(const char *handle_type_name, uint64_t handle) const {
        std::string handle_name = DebugReportGetUtilsObjectName(handle);
        if (handle_name.empty()) {
            handle_name = DebugReportGetMarkerObjectName(handle);
        }

        std::ostringstream str;
        str << handle_type_name << " 0x" << std::hex << handle << "[" << handle_name.c_str() << "]";
        return str.str();
    }

    std::string FormatHandle(const VulkanTypedHandle &handle) const {
        return FormatHandle(object_string[handle.type], handle.handle);
    }

    template <typename HANDLE_T>
    std::string FormatHandle(HANDLE_T handle) const {
        return FormatHandle(VkHandleInfo<HANDLE_T>::Typename(), HandleToUint64(handle));
    }

} debug_report_data;

template debug_report_data *GetLayerDataPtr<debug_report_data>(void *data_key,
                                                               std::unordered_map<void *, debug_report_data *> &data_map);

static inline VkDebugReportFlagsEXT DebugAnnotFlagsToReportFlags(VkDebugUtilsMessageSeverityFlagBitsEXT da_severity,
                                                                 VkDebugUtilsMessageTypeFlagsEXT da_type) {
    if (da_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) return VK_DEBUG_REPORT_ERROR_BIT_EXT;
    if (da_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        if (da_type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)
            return VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
        else
            return VK_DEBUG_REPORT_WARNING_BIT_EXT;
    }
    if (da_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) return VK_DEBUG_REPORT_INFORMATION_BIT_EXT;
    if (da_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) return VK_DEBUG_REPORT_DEBUG_BIT_EXT;

    return 0;
}

static inline void DebugReportFlagsToAnnotFlags(VkDebugReportFlagsEXT dr_flags, bool default_flag_is_spec,
                                                VkDebugUtilsMessageSeverityFlagsEXT *da_severity,
                                                VkDebugUtilsMessageTypeFlagsEXT *da_type) {
    *da_severity = 0;
    *da_type = 0;
    // If it's explicitly listed as a performance warning, treat it as a performance message. Otherwise, treat it as a validation
    // issue.
    if ((dr_flags & kPerformanceWarningBit) != 0) {
        *da_type |= VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        *da_severity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
    }
    if ((dr_flags & kDebugBit) != 0) {
        *da_type |= VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
        *da_severity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;
    }
    if ((dr_flags & kInformationBit) != 0) {
        *da_type |= VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
        *da_severity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
    }
    if ((dr_flags & kWarningBit) != 0) {
        *da_type |= VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
        *da_severity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
    }
    if ((dr_flags & kErrorBit) != 0) {
        *da_type |= VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
        *da_severity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    }
}

static inline LogMessageTypeFlags DebugAnnotFlagsToMsgTypeFlags(VkDebugUtilsMessageSeverityFlagBitsEXT da_severity,
                                                                VkDebugUtilsMessageTypeFlagsEXT da_type) {
    LogMessageTypeFlags msg_type_flags = 0;
    if ((da_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) {
        msg_type_flags |= kErrorBit;
    } else if ((da_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0) {
        if ((da_type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) != 0) {
            msg_type_flags |= kPerformanceWarningBit;
        } else {
            msg_type_flags |= kWarningBit;
        }
    } else if ((da_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) != 0) {
        msg_type_flags |= kInformationBit;
    } else if ((da_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) != 0) {
        msg_type_flags |= kDebugBit;
    }
    return msg_type_flags;
}

VKAPI_ATTR bool LogMsg(const debug_report_data *debug_data, VkFlags msg_flags, const LogObjectList &objects,
                       const std::string &vuid_text, const char *format, va_list argptr);

VKAPI_ATTR VkResult LayerCreateMessengerCallback(debug_report_data *debug_data, bool default_callback,
                                                 const VkDebugUtilsMessengerCreateInfoEXT *create_info,
                                                 const VkAllocationCallbacks *allocator, VkDebugUtilsMessengerEXT *messenger);

VKAPI_ATTR VkResult LayerCreateReportCallback(debug_report_data *debug_data, bool default_callback,
                                              const VkDebugReportCallbackCreateInfoEXT *create_info,
                                              const VkAllocationCallbacks *allocator, VkDebugReportCallbackEXT *callback);

template <typename T>
static inline void LayerDestroyCallback(debug_report_data *debug_data, T callback, const VkAllocationCallbacks *allocator) {
    std::unique_lock<std::mutex> lock(debug_data->debug_output_mutex);
    RemoveDebugUtilsCallback(debug_data, debug_data->debug_callback_list, CastToUint64(callback));
}

VKAPI_ATTR void ActivateInstanceDebugCallbacks(debug_report_data *debug_data);

VKAPI_ATTR void DeactivateInstanceDebugCallbacks(debug_report_data *debug_data);

VKAPI_ATTR void SetDebugUtilsSeverityFlags(std::vector<VkLayerDbgFunctionState> &callbacks, debug_report_data *debug_data);

VKAPI_ATTR void RemoveDebugUtilsCallback(debug_report_data *debug_data, std::vector<VkLayerDbgFunctionState> &callbacks,
                                         uint64_t callback);

VKAPI_ATTR void LayerDebugUtilsDestroyInstance(debug_report_data *debug_data);

VKAPI_ATTR VkBool32 VKAPI_CALL ReportLogCallback(VkFlags msg_flags, VkDebugReportObjectTypeEXT obj_type, uint64_t src_object,
                                                 size_t location, int32_t msg_code, const char *layer_prefix, const char *message,
                                                 void *user_data);

VKAPI_ATTR VkBool32 VKAPI_CALL ReportWin32DebugOutputMsg(VkFlags msg_flags, VkDebugReportObjectTypeEXT obj_type,
                                                         uint64_t src_object, size_t location, int32_t msg_code,
                                                         const char *layer_prefix, const char *message, void *user_data);

VKAPI_ATTR VkBool32 VKAPI_CALL DebugBreakCallback(VkFlags msgFlags, VkDebugReportObjectTypeEXT obj_type, uint64_t src_object,
                                                  size_t location, int32_t msg_code, const char *layer_prefix, const char *message,
                                                  void *user_data);

VKAPI_ATTR VkBool32 VKAPI_CALL MessengerBreakCallback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                                                      VkDebugUtilsMessageTypeFlagsEXT message_type,
                                                      const VkDebugUtilsMessengerCallbackDataEXT *callback_data, void *user_data);

VKAPI_ATTR VkBool32 VKAPI_CALL MessengerLogCallback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                                                    VkDebugUtilsMessageTypeFlagsEXT message_type,
                                                    const VkDebugUtilsMessengerCallbackDataEXT *callback_data, void *user_data);

VKAPI_ATTR VkBool32 VKAPI_CALL MessengerWin32DebugOutputMsg(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                                                            VkDebugUtilsMessageTypeFlagsEXT message_type,
                                                            const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
                                                            void *user_data);

template <typename Map>
static LoggingLabelState *GetLoggingLabelState(Map *map, typename Map::key_type key, bool insert) {
    auto iter = map->find(key);
    LoggingLabelState *label_state = nullptr;
    if (iter == map->end()) {
        if (insert) {
            // Add a label state if not present
            auto inserted = map->emplace(key, std::unique_ptr<LoggingLabelState>(new LoggingLabelState()));
            assert(inserted.second);
            iter = inserted.first;
            label_state = iter->second.get();
        }
    } else {
        label_state = iter->second.get();
    }
    return label_state;
}

static inline void BeginQueueDebugUtilsLabel(debug_report_data *report_data, VkQueue queue,
                                             const VkDebugUtilsLabelEXT *label_info) {
    std::unique_lock<std::mutex> lock(report_data->debug_output_mutex);
    if (nullptr != label_info && nullptr != label_info->pLabelName) {
        auto *label_state = GetLoggingLabelState(&report_data->debugUtilsQueueLabels, queue, /* insert */ true);
        assert(label_state);
        label_state->labels.emplace_back(label_info);

        // TODO: Determine if this is the correct semantics for insert label vs. begin/end, perserving existing semantics for now
        label_state->insert_label.Reset();
    }
}

static inline void EndQueueDebugUtilsLabel(debug_report_data *report_data, VkQueue queue) {
    std::unique_lock<std::mutex> lock(report_data->debug_output_mutex);
    auto *label_state = GetLoggingLabelState(&report_data->debugUtilsQueueLabels, queue, /* insert */ false);
    if (label_state) {
        // Pop the normal item
        if (!label_state->labels.empty()) {
            label_state->labels.pop_back();
        }

        // TODO: Determine if this is the correct semantics for insert label vs. begin/end, perserving existing semantics for now
        label_state->insert_label.Reset();
    }
}

static inline void InsertQueueDebugUtilsLabel(debug_report_data *report_data, VkQueue queue,
                                              const VkDebugUtilsLabelEXT *label_info) {
    std::unique_lock<std::mutex> lock(report_data->debug_output_mutex);
    auto *label_state = GetLoggingLabelState(&report_data->debugUtilsQueueLabels, queue, /* insert */ true);

    // TODO: Determine if this is the correct semantics for insert label vs. begin/end, perserving existing semantics for now
    label_state->insert_label = LoggingLabel(label_info);
}

static inline void BeginCmdDebugUtilsLabel(debug_report_data *report_data, VkCommandBuffer command_buffer,
                                           const VkDebugUtilsLabelEXT *label_info) {
    std::unique_lock<std::mutex> lock(report_data->debug_output_mutex);
    if (nullptr != label_info && nullptr != label_info->pLabelName) {
        auto *label_state = GetLoggingLabelState(&report_data->debugUtilsCmdBufLabels, command_buffer, /* insert */ true);
        assert(label_state);
        label_state->labels.push_back(LoggingLabel(label_info));

        // TODO: Determine if this is the correct semantics for insert label vs. begin/end, perserving existing semantics for now
        label_state->insert_label.Reset();
    }
}

static inline void EndCmdDebugUtilsLabel(debug_report_data *report_data, VkCommandBuffer command_buffer) {
    std::unique_lock<std::mutex> lock(report_data->debug_output_mutex);
    auto *label_state = GetLoggingLabelState(&report_data->debugUtilsCmdBufLabels, command_buffer, /* insert */ false);
    if (label_state) {
        // Pop the normal item
        if (!label_state->labels.empty()) {
            label_state->labels.pop_back();
        }

        // TODO: Determine if this is the correct semantics for insert label vs. begin/end, perserving existing semantics for now
        label_state->insert_label.Reset();
    }
}

static inline void InsertCmdDebugUtilsLabel(debug_report_data *report_data, VkCommandBuffer command_buffer,
                                            const VkDebugUtilsLabelEXT *label_info) {
    std::unique_lock<std::mutex> lock(report_data->debug_output_mutex);
    auto *label_state = GetLoggingLabelState(&report_data->debugUtilsCmdBufLabels, command_buffer, /* insert */ true);
    assert(label_state);

    // TODO: Determine if this is the correct semantics for insert label vs. begin/end, perserving existing semantics for now
    label_state->insert_label = LoggingLabel(label_info);
}

// Current tracking beyond a single command buffer scope is incorrect, and even when it is we need to be able to clean up
static inline void ResetCmdDebugUtilsLabel(debug_report_data *report_data, VkCommandBuffer command_buffer) {
    std::unique_lock<std::mutex> lock(report_data->debug_output_mutex);
    auto *label_state = GetLoggingLabelState(&report_data->debugUtilsCmdBufLabels, command_buffer, /* insert */ false);
    if (label_state) {
        label_state->labels.clear();
        label_state->insert_label.Reset();
    }
}

static inline void EraseCmdDebugUtilsLabel(debug_report_data *report_data, VkCommandBuffer command_buffer) {
    report_data->debugUtilsCmdBufLabels.erase(command_buffer);
}

#endif  // LAYER_LOGGING_H
