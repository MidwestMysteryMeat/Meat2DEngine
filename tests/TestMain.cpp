#include "TestSupport.hpp"

#include <iostream>

int main() {
    using namespace meat2d_tests;
    test_cell_layout_and_protocol();
    test_compression_blocks();
    test_crypto_blocks();
    test_c_api_world_surface();
    test_fixed_timestep_accumulator();
    test_deterministic_rng();
    test_scene_stack_transitions();
    test_scene_history_undo_redo();
    test_scene_diffs();
    test_scene_snapshots();
    test_scene_entity_components_and_hashing();
    test_scene_hierarchy_and_tags();
    test_tile_map_content_and_serialization();
    test_scene_collision_queries();
    test_kinematic_scene_motion();
    test_rigid_body_step_and_particles();
    test_collision_layers_and_debug_draw();
    test_sprite_batch();
    test_static_mesh_instance_batch();
    test_scene_editor_model();
    test_neural_network_and_learning_agents();
    test_deterministic_crowds();
    test_bounded_learning_environment();
    test_mcp_gateway_safety_and_discovery();
    test_input_state_and_action_map();
    test_camera_transforms_and_clamping();
    test_animation_playback_and_camera_source();
    test_packet_codec();
    test_reliable_sequence_window();
    test_chunk_delta_fragmentation();
    test_udp_loopback();
    test_discovery_codec();
    test_lan_discovery();
    test_public_directory_session();
    test_directory_pagination_identity_and_expiry();
    test_public_browser_distrusts_directory_results();
    test_project_browser_safety_and_editing();
    test_project_manager_validation_and_templates();
    test_sprite_sheet_metadata();
    test_texture_atlas_cache();
    test_authoritative_client_server_session();
    test_prediction_and_reconciliation();
    test_security_budget_disconnects_abusive_client();
    test_incompatible_client_build_is_rejected();
    test_client_lifecycle_budgets_are_configurable();
    test_organism_genome_and_ecology();
    test_organism_determinism_and_reproduction();
    test_material_catalog();
    test_sand_falls_and_stone_stays();
    test_water_conserves_cells();
    test_temperature_phase_changes();
    test_lava_water_reaction();
    test_chemical_and_electrical_reactions();
    test_tick_ordered_entity_commands();
    test_grazer_predator_and_worker_ai();
    test_living_simulation_determinism();
    test_cross_chunk_motion();
    test_determinism();
    test_chunks_sleep();
    test_dirty_region_rasterization();
    test_raster_output();
    test_raycast_and_line_of_sight();
    test_projectile_system_destroys_terrain();
    test_projectile_expires_without_impact();
    test_projectile_leaves_world_without_impact();
    test_replay_round_trip_and_divergence();
    test_replay_decode_sorts_out_of_order_paint_events();
    test_chunk_store_persistence_across_worlds();
    test_world_snapshot_round_trip_and_bounds();
    test_session_snapshot_composition_and_hashes();
    test_parallel_step_deterministic_across_thread_counts();
    test_parallel_step_reproducible_across_runs();
    test_parallel_step_conserves_water_and_settles_sand();
    test_parallel_step_records_dirty_regions();

    if (failures != 0) {
        std::cerr << failures << " test assertion(s) failed\n";
        return 1;
    }
    std::cout << "MEAT2D TESTS PASS\n";
    return 0;
}
