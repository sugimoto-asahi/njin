#pragma once
#include "ecs/Components.h"
#include "ecs/njArchetype.h"

namespace njin::ecs {

    struct njObjectArchetypeCreateInfo {
        std::string name;
        njTransformComponent transform;
        njMeshComponent mesh;
    };

    /**
    * A simple object in the world, a mesh + collision box
    */
    class njObjectArchetype final : public njArchetype {
        public:
        explicit njObjectArchetype(const njObjectArchetypeCreateInfo& info);
        EntityId
        make_archetype(ecs::njEntityManager& entity_manager) const override;

        private:
        njObjectArchetypeCreateInfo info_{};
    };

}  // namespace njin::ecs