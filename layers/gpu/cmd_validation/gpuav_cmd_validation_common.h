/* Copyright (c) 2018-2024 The Khronos Group Inc.
 * Copyright (c) 2018-2024 Valve Corporation
 * Copyright (c) 2018-2024 LunarG, Inc.
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
 */

#pragma once

#include "state_tracker/cmd_buffer_state.h"

#include <utility>
#include <vector>

namespace gpuav {
class RestorablePipelineState {
  public:
    RestorablePipelineState(vvl::CommandBuffer& cb_state, VkPipelineBindPoint bind_point) { Create(cb_state, bind_point); }

    void Create(vvl::CommandBuffer& cb_state, VkPipelineBindPoint bind_point);
    void Restore(VkCommandBuffer command_buffer) const;

  private:
    VkPipelineBindPoint pipeline_bind_point_ = VK_PIPELINE_BIND_POINT_MAX_ENUM;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    std::vector<std::pair<VkDescriptorSet, uint32_t>> descriptor_sets_;
    std::vector<std::vector<uint32_t>> dynamic_offsets_;
    uint32_t push_descriptor_set_index_ = 0;
    std::vector<vku::safe_VkWriteDescriptorSet> push_descriptor_set_writes_;
    std::vector<uint8_t> push_constants_data_;
    PushConstantRangesId push_constants_ranges_;
    std::vector<vvl::ShaderObject*> shader_objects_;
};
}  // namespace gpuav
