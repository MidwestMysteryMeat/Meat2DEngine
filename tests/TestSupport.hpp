#pragma once

#include <string>

namespace meat2d_tests {

extern int failures;
void check(bool condition, const std::string& message);

void test_cell_layout_and_protocol();
void test_compression_blocks();
void test_crypto_blocks();
void test_asset_packs();
void test_ui_context();
void test_c_api_world_surface();
void test_fixed_timestep_accumulator();
void test_deterministic_rng();
void test_scene_stack_transitions();
void test_scene_history_undo_redo();
void test_scene_diffs();
void test_scene_snapshots();
void test_scene_entity_components_and_hashing();
void test_scene_collision_queries();
void test_scene_hierarchy_and_tags();
void test_tile_map_content_and_serialization();
void test_kinematic_scene_motion();
void test_rigid_body_step_and_particles();
void test_collision_layers_and_debug_draw();
void test_sprite_batch();
void test_static_mesh_instance_batch();
void test_scene_editor_model();
void test_neural_network_and_learning_agents();
void test_deterministic_crowds();
void test_bounded_learning_environment();
void test_mcp_gateway_safety_and_discovery();
void test_input_state_and_action_map();
void test_camera_transforms_and_clamping();
void test_animation_playback_and_camera_source();
void test_packet_codec();
void test_reliable_sequence_window();
void test_chunk_delta_fragmentation();
void test_udp_loopback();
void test_discovery_codec();
void test_lan_discovery();
void test_public_directory_session();
void test_directory_pagination_identity_and_expiry();
void test_public_browser_distrusts_directory_results();
void test_authoritative_client_server_session();
void test_prediction_and_reconciliation();
void test_security_budget_disconnects_abusive_client();
void test_incompatible_client_build_is_rejected();
void test_client_lifecycle_budgets_are_configurable();
void test_project_browser_safety_and_editing();
void test_project_manager_validation_and_templates();
void test_sprite_sheet_metadata();
void test_texture_atlas_cache();
void test_organism_genome_and_ecology();
void test_organism_determinism_and_reproduction();
void test_material_catalog();
void test_sand_falls_and_stone_stays();
void test_water_conserves_cells();
void test_temperature_phase_changes();
void test_lava_water_reaction();
void test_chemical_and_electrical_reactions();
void test_tick_ordered_entity_commands();
void test_grazer_predator_and_worker_ai();
void test_living_simulation_determinism();
void test_cross_chunk_motion();
void test_determinism();
void test_chunks_sleep();
void test_dirty_region_rasterization();
void test_raster_output();
void test_raycast_and_line_of_sight();
void test_projectile_system_destroys_terrain();
void test_projectile_expires_without_impact();
void test_projectile_leaves_world_without_impact();
void test_replay_round_trip_and_divergence();
void test_replay_decode_sorts_out_of_order_paint_events();
void test_chunk_store_persistence_across_worlds();
void test_world_snapshot_round_trip_and_bounds();
void test_session_snapshot_composition_and_hashes();
void test_parallel_step_deterministic_across_thread_counts();
void test_parallel_step_reproducible_across_runs();
void test_parallel_step_conserves_water_and_settles_sand();
void test_parallel_step_records_dirty_regions();

} // namespace meat2d_tests
