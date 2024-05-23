/* Copyright (c) 2023-2024 Nintendo
 * Copyright (c) 2023-2024 LunarG, Inc.
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

#include "core_validation.h"
#include "state_tracker/shader_object_state.h"
#include "state_tracker/shader_module.h"
#include "state_tracker/render_pass_state.h"
#include "generated/spirv_grammar_helper.h"
#include "drawdispatch/drawdispatch_vuids.h"

VkShaderStageFlags FindNextStage(uint32_t createInfoCount, const VkShaderCreateInfoEXT* pCreateInfos, VkShaderStageFlagBits stage) {
    constexpr uint32_t graphicsStagesCount = 5;
    constexpr uint32_t meshStagesCount = 3;
    const VkShaderStageFlagBits graphicsStages[graphicsStagesCount] = {
        VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
        VK_SHADER_STAGE_GEOMETRY_BIT, VK_SHADER_STAGE_FRAGMENT_BIT};
    const VkShaderStageFlagBits meshStages[meshStagesCount] = {VK_SHADER_STAGE_TASK_BIT_EXT, VK_SHADER_STAGE_MESH_BIT_EXT,
                                                               VK_SHADER_STAGE_FRAGMENT_BIT};

    uint32_t graphicsIndex = graphicsStagesCount;
    uint32_t meshIndex = meshStagesCount;
    for (uint32_t i = 0; i < graphicsStagesCount; ++i) {
        if (graphicsStages[i] == stage) {
            graphicsIndex = i;
            break;
        }
        if (i < meshStagesCount && meshStages[i] == stage) {
            meshIndex = i;
            break;
        }
    }

    if (graphicsIndex < graphicsStagesCount) {
        while (++graphicsIndex < graphicsStagesCount) {
            for (uint32_t i = 0; i < createInfoCount; ++i) {
                if (pCreateInfos[i].stage == graphicsStages[graphicsIndex]) {
                    return graphicsStages[graphicsIndex];
                }
            }
        }
    } else {
        while (++meshIndex < meshStagesCount) {
            for (uint32_t i = 0; i < createInfoCount; ++i) {
                if (pCreateInfos[i].stage == meshStages[meshIndex]) {
                    return meshStages[meshIndex];
                }
            }
        }
    }

    return 0;
}

bool CoreChecks::ValidateCreateShadersLinking(uint32_t createInfoCount, const VkShaderCreateInfoEXT* pCreateInfos,
                                              const Location& loc) const {
    bool skip = false;

    const uint32_t invalid = createInfoCount;
    uint32_t linked_stage = invalid;
    uint32_t non_linked_graphics_stage = invalid;
    uint32_t non_linked_task_mesh_stage = invalid;
    uint32_t linked_task_mesh_stage = invalid;
    uint32_t linked_vert_stage = invalid;
    uint32_t linked_task_stage = invalid;
    uint32_t linked_mesh_no_task_stage = invalid;
    uint32_t linked_spirv_index = invalid;
    uint32_t linked_binary_index = invalid;
    for (uint32_t i = 0; i < createInfoCount; ++i) {
        const Location create_info_loc = loc.dot(Field::pCreateInfos, i);
        const VkShaderCreateInfoEXT& create_info = pCreateInfos[i];
        if (create_info.stage == VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT ||
            create_info.stage == VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT) {
            if (enabled_features.tessellationShader == VK_FALSE) {
                skip |= LogError("VUID-VkShaderCreateInfoEXT-stage-08419", device, create_info_loc.dot(Field::stage),
                                 "is %s, but the tessellationShader feature was not enabled.",
                                 string_VkShaderStageFlagBits(create_info.stage));
            }
        } else if (create_info.stage == VK_SHADER_STAGE_GEOMETRY_BIT) {
            if (enabled_features.geometryShader == VK_FALSE) {
                skip |= LogError("VUID-VkShaderCreateInfoEXT-stage-08420", device, create_info_loc.dot(Field::stage),
                                 "is VK_SHADER_STAGE_GEOMETRY_BIT, but the geometryShader feature was not enabled.");
            }
        } else if (create_info.stage == VK_SHADER_STAGE_TASK_BIT_EXT) {
            if (enabled_features.taskShader == VK_FALSE) {
                skip |= LogError("VUID-VkShaderCreateInfoEXT-stage-08421", device, create_info_loc.dot(Field::stage),
                                 "is VK_SHADER_STAGE_TASK_BIT_EXT, but the taskShader feature was not enabled.");
            }
        } else if (create_info.stage == VK_SHADER_STAGE_MESH_BIT_EXT) {
            if (enabled_features.meshShader == VK_FALSE) {
                skip |= LogError("VUID-VkShaderCreateInfoEXT-stage-08422", device, create_info_loc.dot(Field::stage),
                                 "is VK_SHADER_STAGE_MESH_BIT_EXT, but the meshShader feature was not enabled.");
            }
        }

        if ((create_info.flags & VK_SHADER_CREATE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_EXT) != 0 &&
            enabled_features.attachmentFragmentShadingRate == VK_FALSE) {
            skip |= LogError("VUID-VkShaderCreateInfoEXT-flags-08487", device, create_info_loc.dot(Field::flags),
                             "is %s, but the attachmentFragmentShadingRate feature was not enabled.",
                             string_VkShaderCreateFlagsEXT(create_info.flags).c_str());
        }
        if ((create_info.flags & VK_SHADER_CREATE_FRAGMENT_DENSITY_MAP_ATTACHMENT_BIT_EXT) != 0 &&
            enabled_features.fragmentDensityMap == VK_FALSE) {
            skip |= LogError("VUID-VkShaderCreateInfoEXT-flags-08489", device, create_info_loc.dot(Field::flags),
                             "is %s, but the fragmentDensityMap feature was not enabled.",
                             string_VkShaderCreateFlagsEXT(create_info.flags).c_str());
        }

        if ((create_info.flags & VK_SHADER_CREATE_LINK_STAGE_BIT_EXT) != 0) {
            const auto nextStage = FindNextStage(createInfoCount, pCreateInfos, create_info.stage);
            if (nextStage != 0 && create_info.nextStage != nextStage) {
                skip |= LogError("VUID-vkCreateShadersEXT-pCreateInfos-08409", device, create_info_loc.dot(Field::flags),
                                 "is %s, but nextStage (%s) does not equal the "
                                 "logically next stage (%s) which also has the VK_SHADER_CREATE_LINK_STAGE_BIT_EXT bit.",
                                 string_VkShaderCreateFlagsEXT(create_info.flags).c_str(),
                                 string_VkShaderStageFlags(create_info.nextStage).c_str(),
                                 string_VkShaderStageFlags(nextStage).c_str());
            }
            for (uint32_t j = i; j < createInfoCount; ++j) {
                if (i != j && create_info.stage == pCreateInfos[j].stage) {
                    skip |= LogError("VUID-vkCreateShadersEXT-pCreateInfos-08410", device, create_info_loc,
                                     "and pCreateInfos[%" PRIu32
                                     "] both contain VK_SHADER_CREATE_LINK_STAGE_BIT_EXT and have the stage %s.",
                                     j, string_VkShaderStageFlagBits(create_info.stage));
                }
            }

            linked_stage = i;
            if ((create_info.stage & VK_SHADER_STAGE_VERTEX_BIT) != 0) {
                linked_vert_stage = i;
            } else if ((create_info.stage & VK_SHADER_STAGE_TASK_BIT_EXT) != 0) {
                linked_task_mesh_stage = i;
                linked_task_stage = i;
            } else if ((create_info.stage & VK_SHADER_STAGE_MESH_BIT_EXT) != 0) {
                linked_task_mesh_stage = i;
                if ((create_info.flags & VK_SHADER_CREATE_NO_TASK_SHADER_BIT_EXT) != 0) {
                    linked_mesh_no_task_stage = i;
                }
            }
            if (create_info.codeType == VK_SHADER_CODE_TYPE_SPIRV_EXT) {
                linked_spirv_index = i;
            } else if (create_info.codeType == VK_SHADER_CODE_TYPE_BINARY_EXT) {
                linked_binary_index = i;
            }
        } else if ((create_info.stage & (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
                                         VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT | VK_SHADER_STAGE_GEOMETRY_BIT |
                                         VK_SHADER_STAGE_FRAGMENT_BIT)) != 0) {
            non_linked_graphics_stage = i;
        } else if ((create_info.stage & (VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT)) != 0) {
            non_linked_task_mesh_stage = i;
        }

        if (enabled_features.tessellationShader == VK_FALSE &&
            (create_info.nextStage == VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT ||
             create_info.nextStage == VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT)) {
            skip |= LogError("VUID-VkShaderCreateInfoEXT-nextStage-08428", device, create_info_loc.dot(Field::nextStage),
                             "is %s, but tessellationShader feature was not enabled.",
                             string_VkShaderStageFlags(create_info.nextStage).c_str());
        }
        if (enabled_features.geometryShader == VK_FALSE && create_info.nextStage == VK_SHADER_STAGE_GEOMETRY_BIT) {
            skip |= LogError("VUID-VkShaderCreateInfoEXT-nextStage-08429", device, create_info_loc.dot(Field::nextStage),
                             "is VK_SHADER_STAGE_GEOMETRY_BIT, but tessellationShader feature was not enabled.");
        }
        if (create_info.stage == VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT &&
            (create_info.nextStage & ~VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT) > 0) {
            skip |= LogError("VUID-VkShaderCreateInfoEXT-nextStage-08430", device, create_info_loc.dot(Field::stage),
                             "is VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, but nextStage is %s.",
                             string_VkShaderStageFlags(create_info.nextStage).c_str());
        }
        if (create_info.stage == VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT &&
            (create_info.nextStage & ~(VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)) > 0) {
            skip |= LogError("VUID-VkShaderCreateInfoEXT-nextStage-08431", device, create_info_loc.dot(Field::stage),
                             "is VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, but nextStage is %s.",
                             string_VkShaderStageFlags(create_info.nextStage).c_str());
        }
        if (create_info.stage == VK_SHADER_STAGE_GEOMETRY_BIT && (create_info.nextStage & ~VK_SHADER_STAGE_FRAGMENT_BIT) > 0) {
            skip |= LogError("VUID-VkShaderCreateInfoEXT-nextStage-08433", device, create_info_loc.dot(Field::stage),
                             "is VK_SHADER_STAGE_GEOMETRY_BIT, but nextStage is %s.",
                             string_VkShaderStageFlags(create_info.nextStage).c_str());
        }
        if ((create_info.stage == VK_SHADER_STAGE_FRAGMENT_BIT || create_info.stage == VK_SHADER_STAGE_COMPUTE_BIT) &&
            create_info.nextStage > 0) {
            skip |= LogError("VUID-VkShaderCreateInfoEXT-nextStage-08434", device, create_info_loc.dot(Field::stage),
                             "is %s, but nextStage is %s.", string_VkShaderStageFlagBits(create_info.stage),
                             string_VkShaderStageFlags(create_info.nextStage).c_str());
        }
        if (create_info.stage == VK_SHADER_STAGE_TASK_BIT_EXT && (create_info.nextStage & ~VK_SHADER_STAGE_MESH_BIT_EXT) > 0) {
            skip |= LogError("VUID-VkShaderCreateInfoEXT-nextStage-08435", device, create_info_loc.dot(Field::stage),
                             "is VK_SHADER_STAGE_TASK_BIT_EXT, but nextStage is %s.",
                             string_VkShaderStageFlags(create_info.nextStage).c_str());
        }
        if (create_info.stage == VK_SHADER_STAGE_MESH_BIT_EXT && (create_info.nextStage & ~VK_SHADER_STAGE_FRAGMENT_BIT) > 0) {
            skip |= LogError("VUID-VkShaderCreateInfoEXT-nextStage-08436", device, create_info_loc.dot(Field::stage),
                             "is VK_SHADER_STAGE_MESH_BIT_EXT, but nextStage is %s.",
                             string_VkShaderStageFlags(create_info.nextStage).c_str());
        }

        if ((create_info.flags & VK_SHADER_CREATE_ALLOW_VARYING_SUBGROUP_SIZE_BIT_EXT) != 0 &&
            enabled_features.subgroupSizeControl == VK_FALSE) {
            skip |= LogError(
                "VUID-VkShaderCreateInfoEXT-flags-09404", device, create_info_loc.dot(Field::flags),
                "contains VK_SHADER_CREATE_ALLOW_VARYING_SUBGROUP_SIZE_BIT_EXT, but subgroupSizeControl feature is not enabled.");
        }
        if ((create_info.flags & VK_SHADER_CREATE_REQUIRE_FULL_SUBGROUPS_BIT_EXT) != 0 &&
            enabled_features.computeFullSubgroups == VK_FALSE) {
            skip |= LogError(
                "VUID-VkShaderCreateInfoEXT-flags-09405", device, create_info_loc.dot(Field::flags),
                "contains VK_SHADER_CREATE_REQUIRE_FULL_SUBGROUPS_BIT_EXT, but computeFullSubgroups feature is not enabled.");
        }
    }

    if (linked_stage != invalid && non_linked_graphics_stage != invalid) {
        skip |= LogError("VUID-vkCreateShadersEXT-pCreateInfos-08402", device,
                         loc.dot(Field::pCreateInfos, linked_stage).dot(Field::flags),
                         "contains VK_SHADER_CREATE_LINK_STAGE_BIT_EXT, but pCreateInfos[%" PRIu32
                         "].stage is %s and does not have VK_SHADER_CREATE_LINK_STAGE_BIT_EXT.",
                         non_linked_graphics_stage, string_VkShaderStageFlagBits(pCreateInfos[non_linked_graphics_stage].stage));
    }
    if (linked_stage != invalid && non_linked_task_mesh_stage != invalid) {
        skip |= LogError("VUID-vkCreateShadersEXT-pCreateInfos-08403", device,
                         loc.dot(Field::pCreateInfos, linked_stage).dot(Field::flags),
                         "contains VK_SHADER_CREATE_LINK_STAGE_BIT_EXT, but pCreateInfos[%" PRIu32
                         "] stage is %s and does not have VK_SHADER_CREATE_LINK_STAGE_BIT_EXT.",
                         non_linked_task_mesh_stage, string_VkShaderStageFlagBits(pCreateInfos[non_linked_task_mesh_stage].stage));
    }
    if (linked_vert_stage != invalid && linked_task_mesh_stage != invalid) {
        skip |= LogError("VUID-vkCreateShadersEXT-pCreateInfos-08404", device,
                         loc.dot(Field::pCreateInfos, linked_vert_stage).dot(Field::stage),
                         "is %s and pCreateInfos[%" PRIu32 "].stage is %s, but both contain VK_SHADER_CREATE_LINK_STAGE_BIT_EXT.",
                         string_VkShaderStageFlagBits(pCreateInfos[linked_vert_stage].stage), linked_task_mesh_stage,
                         string_VkShaderStageFlagBits(pCreateInfos[linked_task_mesh_stage].stage));
    }
    if (linked_task_stage != invalid && linked_mesh_no_task_stage != invalid) {
        skip |= LogError("VUID-vkCreateShadersEXT-pCreateInfos-08405", device, loc.dot(Field::pCreateInfos, linked_task_stage),
                         "is a linked task shader, but pCreateInfos[%" PRIu32
                         "] is a linked mesh shader with VK_SHADER_CREATE_NO_TASK_SHADER_BIT_EXT flag.",
                         linked_mesh_no_task_stage);
    }
    if (linked_spirv_index != invalid && linked_binary_index != invalid) {
        skip |= LogError("VUID-vkCreateShadersEXT-pCreateInfos-08411", device, loc.dot(Field::pCreateInfos, linked_spirv_index),
                         "is a linked shader with codeType VK_SHADER_CODE_TYPE_SPIRV_EXT, but pCreateInfos[%" PRIu32
                         "] is a linked shader with codeType VK_SHADER_CODE_TYPE_BINARY_EXT.",
                         linked_binary_index);
    }

    return skip;
}

bool CoreChecks::PreCallValidateCreateShadersEXT(VkDevice device, uint32_t createInfoCount,
                                                 const VkShaderCreateInfoEXT* pCreateInfos, const VkAllocationCallbacks* pAllocator,
                                                 VkShaderEXT* pShaders, const ErrorObject& error_obj) const {
    bool skip = false;

    if (enabled_features.shaderObject == VK_FALSE) {
        skip |=
            LogError("VUID-vkCreateShadersEXT-None-08400", device, error_obj.location, "the shaderObject feature was not enabled.");
    }

    skip |= ValidateCreateShadersLinking(createInfoCount, pCreateInfos, error_obj.location);

    uint32_t tesc_linked_subdivision = 0u;
    uint32_t tese_linked_subdivision = 0u;
    uint32_t tesc_linked_orientation = 0u;
    uint32_t tese_linked_orientation = 0u;
    bool tesc_linked_point_mode = false;
    bool tese_linked_point_mode = false;
    uint32_t tesc_linked_spacing = 0u;
    uint32_t tese_linked_spacing = 0u;

    // Currently we don't provide a way for apps to supply their own cache for shader object
    // https://gitlab.khronos.org/vulkan/vulkan/-/issues/3570
    ValidationCache* cache = CastFromHandle<ValidationCache*>(core_validation_cache);

    for (uint32_t i = 0; i < createInfoCount; ++i) {
        const VkShaderCreateInfoEXT& create_info = pCreateInfos[i];
        if (create_info.codeType != VK_SHADER_CODE_TYPE_SPIRV_EXT) {
            continue;
        }
        const Location create_info_loc = error_obj.location.dot(Field::pCreateInfos, i);

        spv_const_binary_t binary{static_cast<const uint32_t*>(create_info.pCode), create_info.codeSize / sizeof(uint32_t)};
        skip |= RunSpirvValidation(binary, create_info_loc, cache);

        const StageCreateInfo stage_create_info(pCreateInfos[i]);
        const auto spirv = std::make_shared<spirv::Module>(create_info.codeSize, static_cast<const uint32_t*>(create_info.pCode));
        vku::safe_VkShaderCreateInfoEXT safe_create_info = vku::safe_VkShaderCreateInfoEXT(&pCreateInfos[i]);
        const PipelineStageState stage_state(nullptr, &safe_create_info, nullptr, spirv);
        skip |= ValidatePipelineShaderStage(stage_create_info, stage_state, create_info_loc);

        // Validate tessellation stages
        if (stage_state.entrypoint && (create_info.stage == VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT ||
                                       create_info.stage == VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT)) {
            if (create_info.stage == VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT) {
                if (stage_state.entrypoint->execution_mode.tessellation_subdivision == 0) {
                    skip |= LogError("VUID-VkShaderCreateInfoEXT-codeType-08872", device, create_info_loc.dot(Field::stage),
                                     "is VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, but subdivision is not specified.");
                }
                if (stage_state.entrypoint->execution_mode.tessellation_orientation == 0) {
                    skip |= LogError("VUID-VkShaderCreateInfoEXT-codeType-08873", device, create_info_loc.dot(Field::stage),
                                     "is VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, but orientation is not specified.");
                }
                if (stage_state.entrypoint->execution_mode.tessellation_spacing == 0) {
                    skip |= LogError("VUID-VkShaderCreateInfoEXT-codeType-08874", device, create_info_loc.dot(Field::stage),
                                     "is VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, but spacing is not specified.");
                }
            }

            if (stage_state.entrypoint->execution_mode.output_vertices != vvl::kU32Max &&
                (stage_state.entrypoint->execution_mode.output_vertices == 0u ||
                 stage_state.entrypoint->execution_mode.output_vertices > phys_dev_props.limits.maxTessellationPatchSize)) {
                skip |= LogError(
                    "VUID-VkShaderCreateInfoEXT-pCode-08453", device, create_info_loc.dot(Field::pCode),
                    "is using patch size %" PRIu32 ", which is not between 1 and maxTessellationPatchSize (%" PRIu32 ").",
                    stage_state.entrypoint->execution_mode.output_vertices, phys_dev_props.limits.maxTessellationPatchSize);
            }

            if ((create_info.flags & VK_SHADER_CREATE_LINK_STAGE_BIT_EXT) != 0u) {
                if (create_info.stage == VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT) {
                    tesc_linked_subdivision = stage_state.entrypoint->execution_mode.tessellation_subdivision;
                    tesc_linked_orientation = stage_state.entrypoint->execution_mode.tessellation_orientation;
                    tesc_linked_point_mode = stage_state.entrypoint->execution_mode.flags & spirv::ExecutionModeSet::point_mode_bit;
                    tesc_linked_spacing = stage_state.entrypoint->execution_mode.tessellation_spacing;
                } else if (create_info.stage == VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT) {
                    tese_linked_subdivision = stage_state.entrypoint->execution_mode.tessellation_subdivision;
                    tese_linked_orientation = stage_state.entrypoint->execution_mode.tessellation_orientation;
                    tese_linked_point_mode = stage_state.entrypoint->execution_mode.flags & spirv::ExecutionModeSet::point_mode_bit;
                    tese_linked_spacing = stage_state.entrypoint->execution_mode.tessellation_spacing;
                }
            }
        }
    }

    if (tesc_linked_subdivision != 0 && tese_linked_subdivision != 0 && tesc_linked_subdivision != tese_linked_subdivision) {
        skip |= LogError("VUID-vkCreateShadersEXT-pCreateInfos-08867", device, error_obj.location,
                         "The subdivision specified in tessellation control shader (%s) does not match the subdivision in "
                         "tessellation evaluation shader (%s).",
                         string_SpvExecutionMode(tesc_linked_subdivision), string_SpvExecutionMode(tese_linked_subdivision));
    }
    if (tesc_linked_orientation != 0 && tese_linked_orientation != 0 && tesc_linked_orientation != tese_linked_orientation) {
        skip |= LogError("VUID-vkCreateShadersEXT-pCreateInfos-08868", device, error_obj.location,
                         "The orientation specified in tessellation control shader (%s) does not match the orientation in "
                         "tessellation evaluation shader (%s).",
                         string_SpvExecutionMode(tesc_linked_orientation), string_SpvExecutionMode(tese_linked_orientation));
    }
    if (tesc_linked_point_mode && !tese_linked_point_mode) {
        skip |= LogError("VUID-vkCreateShadersEXT-pCreateInfos-08869", device, error_obj.location,
                         "The tessellation control shader specifies execution mode point mode, but the tessellation evaluation "
                         "shader does not.");
    }
    if (tesc_linked_spacing != 0 && tese_linked_spacing != 0 && tesc_linked_spacing != tese_linked_spacing) {
        skip |= LogError("VUID-vkCreateShadersEXT-pCreateInfos-08870", device, error_obj.location,
                         "The spacing specified in tessellation control shader (%s) does not match the spacing in "
                         "tessellation evaluation shader (%s).",
                         string_SpvExecutionMode(tesc_linked_spacing), string_SpvExecutionMode(tese_linked_spacing));
    }

    return skip;
}

bool CoreChecks::PreCallValidateDestroyShaderEXT(VkDevice device, VkShaderEXT shader, const VkAllocationCallbacks* pAllocator,
                                                 const ErrorObject& error_obj) const {
    bool skip = false;

    if (enabled_features.shaderObject == VK_FALSE) {
        skip |=
            LogError("VUID-vkDestroyShaderEXT-None-08481", device, error_obj.location, "the shaderObject feature was not enabled.");
    }

    if (const auto shader_state = Get<vvl::ShaderObject>(shader)) {
        skip |= ValidateObjectNotInUse(shader_state.get(), error_obj.location.dot(Field::shader),
                                       "VUID-vkDestroyShaderEXT-shader-08482");
    }

    return skip;
}

bool CoreChecks::PreCallValidateCmdBindShadersEXT(VkCommandBuffer commandBuffer, uint32_t stageCount,
                                                  const VkShaderStageFlagBits* pStages, const VkShaderEXT* pShaders,
                                                  const ErrorObject& error_obj) const {
    bool skip = false;

    const auto cb_state = GetRead<vvl::CommandBuffer>(commandBuffer);

    if (enabled_features.shaderObject == VK_FALSE) {
        skip |= LogError("VUID-vkCmdBindShadersEXT-None-08462", commandBuffer, error_obj.location,
                         "the shaderObject feature was not enabled.");
    }

    uint32_t vertexStageIndex = stageCount;
    uint32_t taskStageIndex = stageCount;
    uint32_t meshStageIndex = stageCount;
    for (uint32_t i = 0; i < stageCount; ++i) {
        const Location stage_loc = error_obj.location.dot(Field::pStages, i);
        const VkShaderStageFlagBits& stage = pStages[i];
        VkShaderEXT shader = pShaders ? pShaders[i] : VK_NULL_HANDLE;

        for (uint32_t j = i; j < stageCount; ++j) {
            if (i != j && stage == pStages[j]) {
                skip |= LogError("VUID-vkCmdBindShadersEXT-pStages-08463", commandBuffer, stage_loc,
                                 "and pStages[%" PRIu32 "] are both %s.", j, string_VkShaderStageFlagBits(stage));
            }
        }

        if (stage == VK_SHADER_STAGE_VERTEX_BIT && shader != VK_NULL_HANDLE) {
            vertexStageIndex = i;
        } else if (stage == VK_SHADER_STAGE_TASK_BIT_EXT && shader != VK_NULL_HANDLE) {
            taskStageIndex = i;
        } else if (stage == VK_SHADER_STAGE_MESH_BIT_EXT && shader != VK_NULL_HANDLE) {
            meshStageIndex = i;
        } else if (enabled_features.tessellationShader == VK_FALSE &&
                   (stage == VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT || stage == VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT) &&
                   shader != VK_NULL_HANDLE) {
            skip |=
                LogError("VUID-vkCmdBindShadersEXT-pShaders-08474", commandBuffer, stage_loc,
                         "is %s and pShaders[%" PRIu32 "] is not VK_NULL_HANDLE, but tessellationShader feature was not enabled.",
                         string_VkShaderStageFlagBits(stage), i);
        } else if (enabled_features.geometryShader == VK_FALSE && stage == VK_SHADER_STAGE_GEOMETRY_BIT &&
                   shader != VK_NULL_HANDLE) {
            skip |= LogError("VUID-vkCmdBindShadersEXT-pShaders-08475", commandBuffer, stage_loc,
                             "is VK_SHADER_STAGE_GEOMETRY_BIT and pShaders[%" PRIu32
                             "] is not VK_NULL_HANDLE, but geometryShader feature was not enabled.",
                             i);
        } else if (stage == VK_SHADER_STAGE_COMPUTE_BIT) {
            if ((cb_state->command_pool->queue_flags & VK_QUEUE_COMPUTE_BIT) == 0) {
                const LogObjectList objlist(commandBuffer, cb_state->command_pool->Handle());
                skip |=
                    LogError("VUID-vkCmdBindShadersEXT-pShaders-08476", objlist, stage_loc,
                             "is VK_SHADER_STAGE_COMPUTE_BIT, but the command pool the command buffer (%s) was allocated from "
                             "does not support compute operations (%s).",
                             FormatHandle(commandBuffer).c_str(), string_VkQueueFlags(cb_state->command_pool->queue_flags).c_str());
            }
        }
        if ((stage & (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
                      VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)) >
            0) {
            if ((cb_state->command_pool->queue_flags & VK_QUEUE_GRAPHICS_BIT) == 0) {
                const LogObjectList objlist(commandBuffer, cb_state->command_pool->Handle());
                skip |= LogError("VUID-vkCmdBindShadersEXT-pShaders-08477", objlist, stage_loc,
                                 "is %s, but the command pool the command buffer %s was allocated from "
                                 "does not support graphics operations (%s).",
                                 string_VkShaderStageFlagBits(stage), FormatHandle(commandBuffer).c_str(),
                                 string_VkQueueFlags(cb_state->command_pool->queue_flags).c_str());
            }
        }
        if ((stage & (VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_TASK_BIT_EXT)) > 0) {
            if ((cb_state->command_pool->queue_flags & VK_QUEUE_GRAPHICS_BIT) == 0) {
                const LogObjectList objlist(commandBuffer, cb_state->command_pool->Handle());
                skip |= LogError("VUID-vkCmdBindShadersEXT-pShaders-08478", objlist, stage_loc,
                                 "is %s, but the command pool the command buffer %s was allocated from "
                                 "does not support graphics operations (%s).",
                                 string_VkShaderStageFlagBits(stage), FormatHandle(commandBuffer).c_str(),
                                 string_VkQueueFlags(cb_state->command_pool->queue_flags).c_str());
            }
        }
        if (stage == VK_SHADER_STAGE_TASK_BIT_EXT && enabled_features.taskShader == VK_FALSE && shader != VK_NULL_HANDLE) {
            skip |= LogError("VUID-vkCmdBindShadersEXT-pShaders-08490", commandBuffer, stage_loc,
                             "is %s and pShaders[%" PRIu32 "] is not VK_NULL_HANDLE, but taskShader feature was not enabled.",
                             string_VkShaderStageFlagBits(stage), i);
        } else if (stage == VK_SHADER_STAGE_MESH_BIT_EXT && enabled_features.meshShader == VK_FALSE && shader != VK_NULL_HANDLE) {
            skip |= LogError("VUID-vkCmdBindShadersEXT-pShaders-08491", commandBuffer, stage_loc,
                             "is %s and pShaders[%" PRIu32 "] is not VK_NULL_HANDLE, but meshShader feature was not enabled.",
                             string_VkShaderStageFlagBits(stage), i);
        }
        if (stage == VK_SHADER_STAGE_ALL_GRAPHICS || stage == VK_SHADER_STAGE_ALL) {
            skip |= LogError("VUID-vkCmdBindShadersEXT-pStages-08464", commandBuffer, stage_loc, "is %s.",
                             string_VkShaderStageFlagBits(stage));
        }
        if (stage & kShaderStageAllRayTracing) {
            skip |= LogError("VUID-vkCmdBindShadersEXT-pStages-08465", commandBuffer, stage_loc, "is %s.",
                             string_VkShaderStageFlagBits(stage));
        }
        if (stage == VK_SHADER_STAGE_SUBPASS_SHADING_BIT_HUAWEI) {
            skip |= LogError("VUID-vkCmdBindShadersEXT-pStages-08467", commandBuffer, stage_loc, "is %s.",
                             string_VkShaderStageFlagBits(stage));
        }
        if (stage == VK_SHADER_STAGE_CLUSTER_CULLING_BIT_HUAWEI) {
            skip |= LogError("VUID-vkCmdBindShadersEXT-pStages-08468", commandBuffer, stage_loc, "is %s.",
                             string_VkShaderStageFlagBits(stage));
        }
        if (shader != VK_NULL_HANDLE) {
            const auto shader_state = Get<vvl::ShaderObject>(shader);
            if (shader_state && shader_state->create_info.stage != stage) {
                skip |=
                    LogError("VUID-vkCmdBindShadersEXT-pShaders-08469", commandBuffer, stage_loc,
                             "is %s, but pShaders[%" PRIu32 "] was created with shader stage %s.",
                             string_VkShaderStageFlagBits(stage), i, string_VkShaderStageFlagBits(shader_state->create_info.stage));
            }
        }
    }

    if (vertexStageIndex != stageCount && taskStageIndex != stageCount) {
        skip |= LogError("VUID-vkCmdBindShadersEXT-pShaders-08470", commandBuffer, error_obj.location,
                         "pStages[%" PRIu32 "] is VK_SHADER_STAGE_VERTEX_BIT and pStages[%" PRIu32
                         "] is VK_SHADER_STAGE_TASK_BIT_EXT, but neither of pShaders[%" PRIu32 "] and pShaders[%" PRIu32
                         "] are VK_NULL_HANDLE.",
                         vertexStageIndex, taskStageIndex, vertexStageIndex, taskStageIndex);
    }
    if (vertexStageIndex != stageCount && meshStageIndex != stageCount) {
        skip |= LogError("VUID-vkCmdBindShadersEXT-pShaders-08471", commandBuffer, error_obj.location,
                         "pStages[%" PRIu32 "] is VK_SHADER_STAGE_VERTEX_BIT and pStages[%" PRIu32
                         "] is VK_SHADER_STAGE_MESH_BIT_EXT, but neither of pShaders[%" PRIu32 "] and pShaders[%" PRIu32
                         "] are VK_NULL_HANDLE.",
                         vertexStageIndex, meshStageIndex, vertexStageIndex, meshStageIndex);
    }

    return skip;
}

bool CoreChecks::PreCallValidateGetShaderBinaryDataEXT(VkDevice device, VkShaderEXT shader, size_t* pDataSize, void* pData,
                                                       const ErrorObject& error_obj) const {
    bool skip = false;

    if (enabled_features.shaderObject == VK_FALSE) {
        skip |= LogError("VUID-vkGetShaderBinaryDataEXT-None-08461", device, error_obj.location,
                         "the shaderObject feature was not enabled.");
    }

    return skip;
}

bool CoreChecks::ValidateShaderObjectBoundShader(const LastBound& last_bound_state, const VkPipelineBindPoint bind_point,
                                                 const vvl::DrawDispatchVuid& vuid) const {
    bool skip = false;
    const vvl::CommandBuffer& cb_state = last_bound_state.cb_state;

    if (!last_bound_state.ValidShaderObjectCombination(bind_point, enabled_features)) {
        skip |= LogError(vuid.pipeline_or_shaders_bound_08607, cb_state.Handle(), vuid.loc(),
                         "A valid %s pipeline must be bound with vkCmdBindPipeline or shader objects with "
                         "vkCmdBindShadersEXT before calling this command.",
                         string_VkPipelineBindPoint(bind_point));
    }

    if (bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS) {
        if (!last_bound_state.IsValidShaderOrNullBound(ShaderObjectStage::VERTEX)) {
            skip |= LogError(vuid.vertex_shader_08684, cb_state.Handle(), vuid.loc(),
                             "There is no graphics pipeline bound and vkCmdBindShadersEXT() was not called with stage "
                             "VK_SHADER_STAGE_VERTEX_BIT and either VK_NULL_HANDLE or a valid VK_SHADER_STAGE_VERTEX_BIT shader.");
        }
        if (enabled_features.tessellationShader &&
            !last_bound_state.IsValidShaderOrNullBound(ShaderObjectStage::TESSELLATION_CONTROL)) {
            skip |= LogError(vuid.tessellation_control_shader_08685, cb_state.Handle(), vuid.loc(),
                             "There is no graphics pipeline bound and vkCmdBindShadersEXT() was not called with stage "
                             "VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT and either VK_NULL_HANDLE or a valid "
                             "VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT shader.");
        }
        if (enabled_features.tessellationShader &&
            !last_bound_state.IsValidShaderOrNullBound(ShaderObjectStage::TESSELLATION_EVALUATION)) {
            skip |= LogError(vuid.tessellation_evaluation_shader_08686, cb_state.Handle(), vuid.loc(),
                             "There is no graphics pipeline bound and vkCmdBindShadersEXT() was not called with stage "
                             "VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT and either VK_NULL_HANDLE or a valid "
                             "VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT shader.");
        }
        if (enabled_features.geometryShader && !last_bound_state.IsValidShaderOrNullBound(ShaderObjectStage::GEOMETRY)) {
            skip |=
                LogError(vuid.geometry_shader_08687, cb_state.Handle(), vuid.loc(),
                         "There is no graphics pipeline bound and vkCmdBindShadersEXT() was not called with stage "
                         "VK_SHADER_STAGE_GEOMETRY_BIT and either VK_NULL_HANDLE or a valid VK_SHADER_STAGE_GEOMETRY_BIT shader.");
        }
        if (!last_bound_state.IsValidShaderOrNullBound(ShaderObjectStage::FRAGMENT)) {
            skip |=
                LogError(vuid.fragment_shader_08688, cb_state.Handle(), vuid.loc(),
                         "There is no graphics pipeline bound and vkCmdBindShadersEXT() was not called with stage "
                         "VK_SHADER_STAGE_FRAGMENT_BIT and either VK_NULL_HANDLE or a valid VK_SHADER_STAGE_FRAGMENT_BIT shader.");
        }
        if (enabled_features.taskShader && !last_bound_state.IsValidShaderOrNullBound(ShaderObjectStage::TASK)) {
            skip |= LogError(vuid.task_shader_08689, cb_state.Handle(), vuid.loc(),
                             "There is no graphics pipeline bound and vkCmdBindShadersEXT() was not called with stage "
                             "VK_SHADER_STAGE_TASK_BIT and either VK_NULL_HANDLE or a valid VK_SHADER_STAGE_TASK_BIT shader.");
        }
        if (enabled_features.meshShader && !last_bound_state.IsValidShaderOrNullBound(ShaderObjectStage::MESH)) {
            skip |= LogError(vuid.mesh_shader_08690, cb_state.Handle(), vuid.loc(),
                             "There is no graphics pipeline bound and vkCmdBindShadersEXT() was not called with stage "
                             "VK_SHADER_STAGE_MESH_BIT and either VK_NULL_HANDLE or a valid VK_SHADER_STAGE_MESH_BIT shader.");
        }
    }

    return skip;
}

bool CoreChecks::ValidateDrawShaderObject(const LastBound& last_bound_state, const vvl::DrawDispatchVuid& vuid) const {
    bool skip = false;
    const vvl::CommandBuffer& cb_state = last_bound_state.cb_state;
    const LogObjectList objlist(cb_state.Handle());

    if (!cb_state.activeRenderPass->UsesDynamicRendering()) {
        skip |= LogError(vuid.render_pass_began_08876, cb_state.Handle(), vuid.loc(),
                         "Shader objects must be used with dynamic rendering, but VkRenderPass %s is active.",
                         FormatHandle(cb_state.activeRenderPass->Handle()).c_str());
    }

    bool validVertShader = last_bound_state.GetShader(ShaderObjectStage::VERTEX);
    bool validTaskShader = last_bound_state.GetShader(ShaderObjectStage::TASK);
    bool validMeshShader = last_bound_state.GetShader(ShaderObjectStage::MESH);

    if (enabled_features.taskShader || enabled_features.meshShader) {
        if ((validVertShader && validMeshShader) || (!validVertShader && !validMeshShader)) {
            const std::string msg = validVertShader ? "Both vertex shader and mesh shader are bound"
                                                    : "Neither vertex shader nor mesh shader are bound";
            skip |= LogError(vuid.vert_mesh_shader_08693, objlist, vuid.loc(), "%s.", msg.c_str());
        }
    }
    if (enabled_features.taskShader && enabled_features.meshShader) {
        if (validMeshShader &&
            (last_bound_state.GetShaderState(ShaderObjectStage::MESH)->create_info.flags &
             VK_SHADER_CREATE_NO_TASK_SHADER_BIT_EXT) == 0 &&
            !validTaskShader) {
            skip |=
                LogError(vuid.task_mesh_shader_08694, objlist, vuid.loc(),
                         "Mesh shader %s was created without VK_SHADER_CREATE_NO_TASK_SHADER_BIT_EXT, but no task shader is bound.",
                         debug_report->FormatHandle(last_bound_state.GetShader(ShaderObjectStage::MESH)).c_str());
        } else if (validMeshShader &&
                   (last_bound_state.GetShaderState(ShaderObjectStage::MESH)->create_info.flags &
                    VK_SHADER_CREATE_NO_TASK_SHADER_BIT_EXT) != 0 &&
                   validTaskShader) {
            skip |= LogError(vuid.task_mesh_shader_08695, objlist, vuid.loc(),
                             "Mesh shader %s was created with VK_SHADER_CREATE_NO_TASK_SHADER_BIT_EXT, but a task shader is bound.",
                             debug_report->FormatHandle(last_bound_state.GetShader(ShaderObjectStage::MESH)).c_str());
        }
    }
    if (validVertShader && (validTaskShader || validMeshShader)) {
        std::stringstream msg;
        if (validTaskShader && validMeshShader) {
            msg << "task shader " << debug_report->FormatHandle(last_bound_state.GetShader(ShaderObjectStage::TASK))
                << "and mesh shader " << debug_report->FormatHandle(last_bound_state.GetShader(ShaderObjectStage::MESH))
                << " are bound as well";
        } else if (validTaskShader) {
            msg << "task shader " << debug_report->FormatHandle(last_bound_state.GetShader(ShaderObjectStage::TASK))
                << " is bound as well";
        } else if (validMeshShader) {
            msg << "mesh shader " << debug_report->FormatHandle(last_bound_state.GetShader(ShaderObjectStage::MESH))
                << " is bound as well";
        }
        skip |=
            LogError(vuid.vert_task_mesh_shader_08696, objlist, vuid.loc(), "Vertex shader %s is bound, but %s.",
                     debug_report->FormatHandle(last_bound_state.GetShader(ShaderObjectStage::MESH)).c_str(), msg.str().c_str());
    }
    for (uint32_t i = 0; i < kShaderObjectStageCount; ++i) {
        if (i != static_cast<uint32_t>(ShaderObjectStage::COMPUTE) && last_bound_state.shader_object_states[i]) {
            for (const auto& linkedShader : last_bound_state.shader_object_states[i]->linked_shaders) {
                bool found = false;
                for (uint32_t j = 0; j < kShaderObjectStageCount; ++j) {
                    if (linkedShader == last_bound_state.GetShader(static_cast<ShaderObjectStage>(j))) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    const auto missingShader = Get<vvl::ShaderObject>(linkedShader);
                    skip |=
                        LogError(vuid.linked_shaders_08698, objlist, vuid.loc(),
                                 "Shader %s (%s) was created with VK_SHADER_CREATE_LINK_STAGE_BIT_EXT, but the linked %s "
                                 "shader (%s) is not bound.",
                                 debug_report->FormatHandle(last_bound_state.GetShader(static_cast<ShaderObjectStage>(i))).c_str(),
                                 string_VkShaderStageFlagBits(last_bound_state.shader_object_states[i]->create_info.stage),
                                 debug_report->FormatHandle(linkedShader).c_str(),
                                 string_VkShaderStageFlagBits(missingShader->create_info.stage));
                    break;
                }
            }
        }
    }
    const VkShaderStageFlagBits graphics_stages[] = {VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
                                                     VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, VK_SHADER_STAGE_GEOMETRY_BIT,
                                                     VK_SHADER_STAGE_FRAGMENT_BIT};
    VkShaderStageFlagBits prev_stage = VK_SHADER_STAGE_ALL;
    VkShaderStageFlagBits next_stage = VK_SHADER_STAGE_ALL;
    for (const auto stage : graphics_stages) {
        const auto state = last_bound_state.GetShaderState(VkShaderStageToShaderObjectStage(stage));
        if (state && next_stage != VK_SHADER_STAGE_ALL && state->create_info.stage != next_stage) {
            skip |= LogError(vuid.linked_shaders_08699, objlist, vuid.loc(),
                             "Shaders %s and %s were created with VK_SHADER_CREATE_LINK_STAGE_BIT_EXT without intermediate "
                             "stage %s linked, but %s shader is bound.",
                             string_VkShaderStageFlagBits(prev_stage), string_VkShaderStageFlagBits(next_stage),
                             string_VkShaderStageFlagBits(stage), string_VkShaderStageFlagBits(stage));
            break;
        }
        if (state) {
            next_stage = VK_SHADER_STAGE_ALL;
            if (!state->linked_shaders.empty()) {
                prev_stage = stage;
                for (const auto& linked_shader : state->linked_shaders) {
                    const auto& linked_state = Get<vvl::ShaderObject>(linked_shader);
                    if (linked_state && linked_state->create_info.stage == state->create_info.nextStage) {
                        next_stage = static_cast<VkShaderStageFlagBits>(state->create_info.nextStage);
                        break;
                    }
                }
            }
        }
    }

    const vvl::ShaderObject* first = nullptr;
    for (const auto shader_state : last_bound_state.shader_object_states) {
        if (!shader_state || !shader_state->IsGraphicsShaderState()) {
            continue;
        }
        if (!first) {
            first = shader_state;
        } else {
            bool pushConstsDifferent =
                first->create_info.pushConstantRangeCount != shader_state->create_info.pushConstantRangeCount;
            if (!pushConstsDifferent) {
                bool found = false;
                for (uint32_t i = 0; i < shader_state->create_info.pushConstantRangeCount; ++i) {
                    for (uint32_t j = 0; j < first->create_info.pushConstantRangeCount; ++j) {
                        if (shader_state->create_info.pPushConstantRanges[i] == first->create_info.pPushConstantRanges[j]) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        pushConstsDifferent = true;
                        break;
                    }
                }
            }
            if (pushConstsDifferent) {
                skip |= LogError(vuid.shaders_push_constants_08878, objlist, vuid.loc(),
                                 "Shaders %s and %s have different push constant ranges.",
                                 string_VkShaderStageFlagBits(first->create_info.stage),
                                 string_VkShaderStageFlagBits(shader_state->create_info.stage));
            }
            bool descriptorLayoutsDifferent = first->create_info.setLayoutCount != shader_state->create_info.setLayoutCount;
            if (!descriptorLayoutsDifferent) {
                bool found = false;
                for (uint32_t i = 0; i < shader_state->create_info.setLayoutCount; ++i) {
                    for (uint32_t j = 0; j < first->create_info.setLayoutCount; ++j) {
                        if (shader_state->create_info.pSetLayouts[i] == first->create_info.pSetLayouts[j]) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        descriptorLayoutsDifferent = true;
                        break;
                    }
                }
            }
            if (descriptorLayoutsDifferent) {
                skip |= LogError(vuid.shaders_descriptor_layouts_08879, objlist, vuid.loc(),
                                 "Shaders %s and %s have different descriptor set layouts.",
                                 string_VkShaderStageFlagBits(first->create_info.stage),
                                 string_VkShaderStageFlagBits(shader_state->create_info.stage));
            }
        }
    }

    skip |= ValidateDrawShaderObjectMesh(last_bound_state, vuid);

    return skip;
}

bool CoreChecks::ValidateDrawShaderObjectMesh(const LastBound& last_bound_state, const vvl::DrawDispatchVuid& vuid) const {
    bool skip = false;
    if (IsValueIn(vuid.function,
                  {Func::vkCmdDrawMeshTasksNV, Func::vkCmdDrawMeshTasksIndirectNV, Func::vkCmdDrawMeshTasksIndirectCountNV,
                   Func::vkCmdDrawMeshTasksEXT, Func::vkCmdDrawMeshTasksIndirectEXT, Func::vkCmdDrawMeshTasksIndirectCountEXT})) {
        return skip;
    }

    const bool task_shader = last_bound_state.GetShader(ShaderObjectStage::TASK);
    const bool mesh_shader = last_bound_state.GetShader(ShaderObjectStage::MESH);
    if (task_shader || mesh_shader) {
        std::stringstream msg;
        if (task_shader && mesh_shader) {
            msg << "Task and mesh shaders are bound.";
        } else if (mesh_shader) {
            msg << "Task shader is bound.";
        } else {
            msg << "Mesh shader is bound.";
        }
        skip |=
            LogError(vuid.draw_shaders_no_task_mesh_08885, last_bound_state.cb_state.Handle(), vuid.loc(), "%s", msg.str().c_str());
    }

    return skip;
}
