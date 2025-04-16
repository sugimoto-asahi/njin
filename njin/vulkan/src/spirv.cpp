#include "vulkan/spirv.h"

#include <cstdio>

namespace njin::vulkan {
    SpirVBinary compileShaderToSPIRV_Vulkan(glslang_stage_t stage,
                                            const char* shaderSource,
                                            const char* fileName) {
        const glslang_input_t input = {
            .language = GLSLANG_SOURCE_GLSL,
            .stage = stage,
            .client = GLSLANG_CLIENT_VULKAN,
            .client_version = GLSLANG_TARGET_VULKAN_1_3,
            .target_language = GLSLANG_TARGET_SPV,
            .target_language_version = GLSLANG_TARGET_SPV_1_5,
            .code = shaderSource,
            .default_version = 100,
            .default_profile = GLSLANG_NO_PROFILE,
            .force_default_version_and_profile = false,
            .forward_compatible = false,
            .messages = GLSLANG_MSG_DEFAULT_BIT,
            .resource = glslang_default_resource(),
        };

        glslang_shader_t* shader = glslang_shader_create(&input);

        SpirVBinary bin = {
            .words = NULL,
            .size = 0,
        };
        if (!glslang_shader_preprocess(shader, &input)) {
            printf("GLSL preprocessing failed %s\n", fileName);
            printf("%s\n", glslang_shader_get_info_log(shader));
            printf("%s\n", glslang_shader_get_info_debug_log(shader));
            printf("%s\n", input.code);
            glslang_shader_delete(shader);
            return bin;
        }

        if (!glslang_shader_parse(shader, &input)) {
            printf("GLSL parsing failed %s\n", fileName);
            printf("%s\n", glslang_shader_get_info_log(shader));
            printf("%s\n", glslang_shader_get_info_debug_log(shader));
            printf("%s\n", glslang_shader_get_preprocessed_code(shader));
            glslang_shader_delete(shader);
            return bin;
        }

        glslang_program_t* program = glslang_program_create();
        glslang_program_add_shader(program, shader);

        if (!glslang_program_link(program,
                                  GLSLANG_MSG_SPV_RULES_BIT |
                                  GLSLANG_MSG_VULKAN_RULES_BIT)) {
            printf("GLSL linking failed %s\n", fileName);
            printf("%s\n", glslang_program_get_info_log(program));
            printf("%s\n", glslang_program_get_info_debug_log(program));
            glslang_program_delete(program);
            glslang_shader_delete(shader);
            return bin;
        }

        glslang_program_SPIRV_generate(program, stage);

        bin.size = glslang_program_SPIRV_get_size(program);
        bin.words = static_cast<uint32_t*>(malloc(bin.size * sizeof(uint32_t)));
        glslang_program_SPIRV_get(program, bin.words);

        const char* spirv_messages =
        glslang_program_SPIRV_get_messages(program);
        if (spirv_messages)
            printf("(%s) %s\b", fileName, spirv_messages);

        glslang_program_delete(program);
        glslang_shader_delete(shader);

        return bin;
    }
}  // namespace njin::vulkan
