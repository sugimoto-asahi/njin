#include "vulkan/RenderInfos.h"

#include "core/njTexture.h"

namespace {
    using namespace njin;

    /**
     * Update a descriptor set containing a binding for model matrices with
     * new model matrices
     * @param set Descriptor set
     * @param binding Name of binding for model matrices
     * @param models New model matrices
     */
    void update_model_matrices(njin::vulkan::DescriptorSet* set,
                               const std::string& binding,
                               const std::vector<math::njMat4f>& models) {
        set->update_descriptor_data(binding, models);
    }

    /**
     * Update a descriptor set containing a binding for vp matrices
     * new vp matrices
     * @param set Descriptor set
     * @param binding Name of binding for vp mtarices
     * @param view The new view matrix
     * @param projection The new projection matrix
     */
    void update_vp_matrices(njin::vulkan::DescriptorSet* set,
                            const std::string& binding,
                            const math::njMat4f& view,
                            const math::njMat4f& projection) {
        std::vector<math::njMat4f> vp_matrices{ { view, projection } };
        set->update_descriptor_data(binding, vp_matrices);
    }

    /**
     * Calculate the attribute data of the 6 vertices that make up a mesh's billboard.
     * @param vertices All vertices of a mesh
     * @param view View matrix of the scene
     * @return Ordered list of 6 vertices
     */
    std::array<vulkan::IsoDrawVertex, 6>
    calculate_billboard(const std::vector<core::njVertex>& vertices,
                        const math::njMat4f& view) {
        // In isometric projections, it is known that the (non-depth) coordinates
        // of objects do not change when projected onto the near plane. Therefore,
        // we can find the quad bounding box for any mesh solely based on the mesh's
        // coordinates in eye space.

        // The camera is at the origin in eye space. Relative to the camera,
        // anything to the right is +x, anything above is +y, anything in front is -z.
        // Therefore, the right bound of the quad is x_max, the left bound is x_min.
        // The top bound of the quad is y_max, the bottom bound is y_min.

        // find out the mins and maxs in eye space
        float min_x{};
        float max_x{};
        float max_y{};
        float min_y{};
        bool first{ true };

        // set the depth of the quad to the average all vertex depths
        float z{};

        for (const core::njVertex& vertex : vertices) {
            math::njVec4f position_object{ vertex.position.x,
                                           vertex.position.y,
                                           vertex.position.z,
                                           1.0f };
            math::njVec4f position_eye{ view * position_object };
            if (first) {
                min_x = position_eye.x;
                max_x = position_eye.x;
                min_y = position_eye.y;
                max_y = position_eye.y;
                z = position_eye.z;
                first = false;
                continue;
            }

            if (position_eye.x > max_x) {
                max_x = position_eye.x;
            } else if (position_eye.x < min_x) {
                min_x = position_eye.x;
            }
            if (position_eye.y > max_y) {
                max_y = position_eye.y;
            } else if (position_eye.y < min_y) {
                min_y = position_eye.y;
            }

            z += position_eye.z;
        }

        z /= static_cast<float>(vertices.size());

        // form the quad in eye space
        math::njVec4f p0_eye{ min_x, max_y, z, 1 };  // top left corner
        math::njVec4f p1_eye{ max_x, max_y, z, 1 };  // top right corner
        math::njVec4f p2_eye{ min_x, min_y, z, 1 };  // bottom left corner
        math::njVec4f p3_eye{ max_x, min_y, z, 1 };  // bottom right corner

        // first transform the coordinates back into object space
        math::njVec4f p0{ math::inverse(view) * p0_eye };
        math::njVec4f p1{ math::inverse(view) * p1_eye };
        math::njVec4f p2{ math::inverse(view) * p2_eye };
        math::njVec4f p3{ math::inverse(view) * p3_eye };

        vulkan::IsoDrawVertex v0{ .x = p0.x,
                                  .y = p0.y,
                                  .z = p0.z,
                                  .u = 0.f,
                                  .v = 0.f };
        vulkan::IsoDrawVertex v1{ .x = p1.x,
                                  .y = p1.y,
                                  .z = p1.z,
                                  .u = 1.f,
                                  .v = 0.f };
        vulkan::IsoDrawVertex v2{ .x = p2.x,
                                  .y = p2.y,
                                  .z = p2.z,
                                  .u = 0.f,
                                  .v = 1.f };
        vulkan::IsoDrawVertex v3{ .x = p3.x,
                                  .y = p3.y,
                                  .z = p3.z,
                                  .u = 1.f,
                                  .v = 1.f };

        return { v1, v0, v2, v1, v2, v3 };
    }

}  // namespace

namespace njin::vulkan {
    void RenderBuckets::add(const KeyedRenderInfo& info) {
        RenderInfo render_info{ .type = info.type, .info = info.info };
        render_infos_[info.key].push_back(render_info);
    }

    std::vector<RenderInfo> RenderBuckets::get(const RenderKey& key) const {
        return render_infos_.at(key);
    }

    RenderInfos::RenderInfos(
    const core::njRegistry<core::njMesh>& mesh_registry,
    const core::njRegistry<core::njTexture>& texture_registry,
    vulkan::RenderResources& render_resources,
    const core::RenderBuffer& render_buffer) :
        render_buffer_{ &render_buffer } {
        process_textures(render_resources, texture_registry);
        write_data(mesh_registry,
                   texture_registry,
                   render_resources,
                   render_buffer.get_view_matrix(),
                   render_buffer.get_projection_matrix());
    }

    std::vector<RenderInfo>
    RenderInfos::get_render_infos(const RenderKey& key) const {
        return render_infos_.get(key);
    }

    void
    RenderInfos::write_data(const core::njRegistry<core::njMesh>& mesh_registry,
                            const core::njRegistry<core::njTexture>&
                            texture_registry,
                            vulkan::RenderResources& render_resources,
                            const math::njMat4f& view_matrix,
                            const math::njMat4f& projection_matrix) {
        // serialize all the vertices of required meshes into an array
        std::vector<vulkan::MainDrawVertex> main_vertices{};

        // serialize all the corresponding model matrices into an array
        std::vector<vulkan::MainDrawModel> model_matrices{};

        // serialize all the billboard quads of required meshes into an array
        std::vector<vulkan::IsoDrawVertex> iso_vertices{};

        std::vector<math::njMat4f> view_projection{ view_matrix,
                                                    projection_matrix };

        // vertex offset of current mesh into vertex buffer
        uint32_t current_mesh_offset{ 0 };
        uint32_t current_model_index{ 0 };
        // vertex offset of current billboard into vertex buffer
        uint32_t current_billboard_offset{ 0 };
        for (const core::Renderable& renderable :
             render_buffer_->get_renderables()) {
            RenderKey main_draw_key{ "main", "draw" };
            KeyedRenderInfo main_draw_info{ .key = main_draw_key };

            RenderKey iso_draw_key{ "iso", "draw" };
            KeyedRenderInfo iso_draw_info{ .key = iso_draw_key };

            if (renderable.type == RenderType::Mesh) {
                main_draw_info.type = RenderType::Mesh;
                iso_draw_info.type = RenderType::Billboard;
                // add the vertices
                auto data{ std::get<core::MeshData>(renderable.data) };
                const core::njMesh* mesh{ mesh_registry.get(data.mesh_name) };
                std::vector<core::njPrimitive> primitives{
                    mesh->get_primitives()
                };
                for (const core::njPrimitive& primitive : primitives) {
                    std::vector<core::njVertex> vertices{
                        primitive.get_vertices()
                    };
                    // object space positions of mesh
                    for (const core::njVertex& vertex : vertices) {
                        vulkan::MainDrawVertex main_draw_vertex{
                            .x = vertex.position.x,
                            .y = vertex.position.y,
                            .z = vertex.position.z
                        };
                        main_vertices.push_back(main_draw_vertex);
                    }
                }

                std::array<IsoDrawVertex, 6> quad_vertices{
                    calculate_billboard(mesh->get_vertices(), view_matrix)
                };
                iso_vertices.insert(iso_vertices.end(),
                                    quad_vertices.begin(),
                                    quad_vertices.end());

                // add the model matrices
                vulkan::MainDrawModel main_draw_model{
                    .model = data.global_transform
                };
                model_matrices.push_back(main_draw_model);

                // add RenderInfo for main_draw to queue
                MeshRenderInfo mesh_info{
                    .model_index = current_model_index,
                    .mesh_offset = current_mesh_offset,
                    .vertex_count = mesh->get_vertex_count()
                };
                main_draw_info.info = mesh_info;

                render_infos_.add(main_draw_info);

                // add RenderInfo for iso_draw to queue
                BillboardRenderInfo billboard_info{
                    .billboard_offset = current_billboard_offset,
                    .model_index = current_model_index,
                    .texture_index = texture_indices_.at(data.texture_name)
                };
                iso_draw_info.info = billboard_info;
                render_infos_.add(iso_draw_info);

                // update counters
                current_mesh_offset += mesh->get_vertex_count();
                ++current_model_index;
                current_billboard_offset += 6;  // each billboard is 6 vertices
            }

            // write the data to the actual resources
            render_resources.descriptor_sets
            .write_descriptor_data("mvp", "model", model_matrices);

            render_resources.descriptor_sets
            .write_descriptor_data("mvp", "view_projection", view_projection);

            render_resources.vertex_buffers.load_into_buffer("main_draw",
                                                             main_vertices);

            render_resources.vertex_buffers.load_into_buffer("iso_draw",
                                                             iso_vertices);
        }
    }

    void RenderInfos::process_textures(vulkan::RenderResources& resources,
                                       const core::njRegistry<core::njTexture>&
                                       texture_registry) {
        std::vector<const core::njTexture*> textures{
            texture_registry.get_records()
        };
        std::map<std::string, const core::njTexture*> texture_map{
            texture_registry.get_map()
        };
        resources.descriptor_sets.write_descriptor_data("textures",
                                                        "image",
                                                        textures);

        // map the texture name to the texture index
        int counter{ 0 };
        for (auto it{ texture_map.begin() }; it != texture_map.end(); ++it) {
            texture_indices_[it->first] = counter;
            ++counter;
        }
    }

}  // namespace njin::vulkan