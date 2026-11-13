#pragma once

// Only implemented bosses are reachable; Jigoku's last scene now includes20.
constexpr int scene_stage_count(int scene) { return scene == 0 || scene == 6 ? 5 : 4; }
constexpr bool stage_has_singyoku(int scene, int stage) { return scene == 0 && stage == 4; }
constexpr bool stage_has_konngara(int scene, int stage) { return scene == 6 && stage == 4; }
constexpr bool stage_has_boss(int scene, int stage) {
    return stage_has_singyoku(scene,stage) || stage_has_konngara(scene,stage);
}
constexpr int stage_number(int scene, int stage)
{
    return scene == 0 ? stage + 1 : 5 + ((scene - 1) / 2) * 5 + stage + 1;
}

enum RouteChoice { ROUTE_MAKAI, ROUTE_JIGOKU };
constexpr int route_first_scene(RouteChoice route) { return route == ROUTE_MAKAI ? 1 : 2; }
