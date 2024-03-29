#include "scene.hpp"
#include "event_storage.hpp"
#include "ovis/runtime/basic_types.h"
#include "ovis/runtime/entity_descriptor.h"
#include "ovis/runtime/error.h"
#include "ovis/runtime/resource.h"
#include "resource.hpp"
#include "entity_descriptor.hpp"

#include <alloca.h>
#include <cassert>
#include <string>
#include <unordered_map>

struct Scene* ovis_scene_create() {
    return new Scene();
}

void ovis_scene_tick(struct Scene* scene, float delta_time) {
    scene->tick(delta_time);
}

void* ovis_scene_get_scene_component(struct Scene* scene, ResourceId resource_id) {
    auto storage = scene->get_scene_component_storage(resource_id);
    return storage != nullptr ? storage->get() : nullptr;
}

int32_t ovis_scene_get_entity_count(const struct Scene* scene) {
    return scene->entity_count();
}

void ovis_scene_destroy(struct Scene* scene) {
    delete scene;
}

Scene::Scene() : m_update_scheduler(JOB_KIND_UPDATE) {
    m_resource_storages.resize(RESOURCES.size());

    EntitySpawnList->initialize(EntitySpawnList, &m_entities_to_spawn);

    for (const auto& resource : RESOURCES) {
        switch (resource.kind) {
            case RESOURCE_KIND_ENTITY_SPAWN_LIST:
                m_resource_storages[ResourceIdType::index(resource.id)] = std::make_unique<ResourceStorage>(&resource);
                break;

            case RESOURCE_KIND_ENTITY_DESPAWN_LIST:
                m_resource_storages[ResourceIdType::index(resource.id)] = std::make_unique<ResourceStorage>(&resource);
                break;

            case RESOURCE_KIND_EVENT:
                m_resource_storages[ResourceIdType::index(resource.id)] = std::make_unique<EventStorage>(&resource);
                break;

            case RESOURCE_KIND_SCENE_COMPONENT:
                m_resource_storages[ResourceIdType::index(resource.id)] = std::make_unique<SceneComponentStorage>(&resource);
                break;

            case RESOURCE_KIND_VIEWPORT_COMPONENT:
                m_resource_storages[ResourceIdType::index(resource.id)] = std::make_unique<IndexedComponentStorage>(&resource);
                break;

            case RESOURCE_KIND_ENTITY_COMPONENT:
                m_resource_storages[ResourceIdType::index(resource.id)] = std::make_unique<IndexedComponentStorage>(&resource);
                break;
        }
    }

    Scheduler setup_scheduler(JOB_KIND_SETUP);
    setup_scheduler.run_jobs(this);
}

void Scene::tick(float delta_time) {
    for (auto entity_id : m_entities_to_despawn) {
        if (m_entities.contains(entity_id)) {
            m_entities.remove(entity_id);
        }
    }
    m_entities_to_despawn.clear();

    for (int32_t i = 0; i < m_entities_to_spawn.size; ++i) {
        EntityDescriptor entity_desc;
        if (TYPE_FUNCTION(TYPE(ovis, runtime, List), get)(EntitySpawnList, &m_entities_to_spawn, &i, &entity_desc)) {
            m_entities.emplace();
        }
    }
    TYPE_FUNCTION(TYPE(ovis, runtime, List), clear)(EntitySpawnList, &m_entities_to_spawn);

    assert(get_scene_component_storage(RESOURCE_ID(TYPE(ovis, runtime, DeltaTime))));
    get_scene_component_storage(RESOURCE_ID(TYPE(ovis, runtime, DeltaTime)))->emplace(&delta_time);

    m_game_time += delta_time * (1 / 1000.0f);
    assert(get_scene_component_storage(RESOURCE_ID(TYPE(ovis, runtime, GameTime))));
    get_scene_component_storage(RESOURCE_ID(TYPE(ovis, runtime, GameTime)))->emplace(&m_game_time);

    m_update_scheduler.run_jobs(this);

    for (auto& resource_storage : m_resource_storages) {
        if (resource_storage->resource()->kind == RESOURCE_KIND_EVENT) {
            static_cast<EventStorage*>(resource_storage.get())->clear();
        }
    }
}

bool ovis_scene_iterate(struct Scene* scene, int32_t input_resouces_ids_count, const int32_t* input_resouces_ids, int32_t output_resouces_ids_count, const int32_t* output_resouces_ids, IterateCallback callback) {
    return scene->iterate(
        input_resouces_ids_count, input_resouces_ids,
        output_resouces_ids_count, output_resouces_ids,
        callback
    );
}

bool Scene::iterate(
    int32_t input_resource_ids_count, const int32_t* input_resource_ids,
    int32_t output_resource_ids_count, const int32_t* output_resource_ids,
    IterateCallback callback
) {
    EventStorage* event_storage = nullptr;
    intptr_t event_input_index = 0;

    int input_viewport_components_count = 0;
    IndexedComponentStorage* input_viewport_component_storages[input_resource_ids_count];
    int input_viewport_component_indices[input_resource_ids_count];

    int input_entity_components_count = 0;
    IndexedComponentStorage* input_entity_component_storages[input_resource_ids_count];
    int input_entity_component_indices[input_resource_ids_count];

    void* input_resources[input_resource_ids_count];
    void* output_resources[output_resource_ids_count];

    for (int i = 0; i < input_resource_ids_count; ++i) {
        auto resource = get_resource(input_resource_ids[i]);

        switch (resource->kind) {
            case RESOURCE_KIND_ENTITY_SPAWN_LIST:
            case RESOURCE_KIND_ENTITY_DESPAWN_LIST: {
                RETURN_ERROR("Invalid job input");
            }

            case RESOURCE_KIND_EVENT: {
                assert(event_storage == nullptr && "We currently can only handle one event as input");
                event_storage = get_event_storage(resource->id);
                event_input_index = i;
                break;
            }

            case RESOURCE_KIND_SCENE_COMPONENT: {
                auto storage = get_scene_component_storage(resource->id);
                assert(storage);
                if (auto component = storage->get(); component != nullptr) {
                    input_resources[i] = component;
                } else {
                    // Scene component does not exist, we can stop the iteration early
                    return true;
                }
                break;
            }

            case RESOURCE_KIND_VIEWPORT_COMPONENT: {
                input_viewport_component_storages[input_viewport_components_count] = get_indexed_component_storage(resource->id);
                input_viewport_component_indices[input_viewport_components_count] = i;
                ++input_viewport_components_count;
                break;
            }

            case RESOURCE_KIND_ENTITY_COMPONENT: {
                input_entity_component_storages[input_entity_components_count] = get_indexed_component_storage(resource->id);
                input_entity_component_indices[input_entity_components_count] = i;
                ++input_entity_components_count;
                break;
            }
        }
    }
    for (int i = 0; i < output_resource_ids_count; ++i) {
        output_resources[i] = alloca(get_resource(output_resource_ids[i])->type->size);
    }

    for (int event_index = 0; (event_index == 0 && !event_storage) || event_index < event_storage->count(); ++event_index) {
        if (event_storage) {
            input_resources[event_input_index] = event_storage->event_ptr(event_input_index);
        }

        // TODO: iterator over viewport ids of the storage with least components
        for (int viewport_index = 0; (viewport_index == 0 && input_viewport_components_count == 0) || viewport_index < m_viewports.count(); ++viewport_index) {
            const auto viewport_id = m_viewports[viewport_index];
            bool skip_viewport = false;
            for (int viewport_component_index = 0; viewport_component_index < input_viewport_components_count && !skip_viewport; ++viewport_component_index) {
                if (auto component = input_viewport_component_storages[viewport_component_index]->component_ptr(viewport_id); component != nullptr) {
                    input_resources[input_viewport_component_indices[viewport_index]] = component;
                } else {
                    skip_viewport = true;
                }
            }
            if (skip_viewport) {
                continue;
            }

            // TODO: iterator over entity ids of the storage with least components
            for (int entity_index = 0; (entity_index == 0 && input_entity_components_count == 0) || entity_index < m_entities.count(); ++entity_index) {
                const auto entity_id = m_entities[entity_index];
                bool skip_entity = false;
                for (int entity_component_index = 0; entity_component_index < input_entity_components_count && !skip_entity; ++entity_component_index) {
                    if (auto component = input_entity_component_storages[entity_component_index]->component_ptr(entity_id); component != nullptr) {
                        input_resources[input_entity_component_indices[entity_index]] = component;
                    } else {
                        skip_entity = true;
                    }
                }
                if (skip_entity) {
                    continue;
                }

                if (callback(input_resources, output_resources)) {
                    for (int i = 0; i < output_resource_ids_count; ++i) {
                        auto resource = get_resource(output_resource_ids[i]);
                        switch (resource->kind) {
                            case RESOURCE_KIND_ENTITY_SPAWN_LIST: {
                                auto entities_to_spawn = (List*)output_resources[i];
                                if (TYPE_FUNCTION(TYPE(ovis, runtime, List), append)(EntitySpawnList, &m_entities_to_spawn, entities_to_spawn)) {
                                    printf("spawning %d entities before next frame\n", entities_to_spawn->size);
                                }
                                break;
                            }

                            case RESOURCE_KIND_ENTITY_DESPAWN_LIST:
                                assert(false && "not implemented yet");
                                break;

                            case RESOURCE_KIND_EVENT:
                                assert(false && "not implemented yet");
                                break;

                            case RESOURCE_KIND_SCENE_COMPONENT: {
                                auto scene_component_storage = get_scene_component_storage(output_resource_ids[i]);
                                assert(scene_component_storage);
                                // TODO: move the value into the storage instead of copying it
                                scene_component_storage->emplace(output_resources[i]);
                                break;
                            }

                            case RESOURCE_KIND_VIEWPORT_COMPONENT: {
                                auto storage = get_indexed_component_storage(resource->id);
                                assert(storage);
                                storage->emplace(viewport_id, output_resources[i]);
                                break;
                            }
                            case RESOURCE_KIND_ENTITY_COMPONENT: {
                                auto storage = get_indexed_component_storage(resource->id);
                                assert(storage);
                                storage->emplace(entity_id, output_resources[i]);
                                break;
                            }
                        }
                        resource->type->destroy(resource->type, output_resources[i]);
                    }
                } else {
                    return false;
                }
            }
        }
    }

    return true;
}

RESOURCE_IMPL_WITH_INFO(ovis, runtime, DeltaTime, RESOURCE_KIND_SCENE_COMPONENT, &TYPE_INFO(TYPE(ovis, runtime, Float)));
RESOURCE_IMPL_WITH_INFO(ovis, runtime, GameTime, RESOURCE_KIND_SCENE_COMPONENT, &TYPE_INFO(TYPE(ovis, runtime, Float)));
