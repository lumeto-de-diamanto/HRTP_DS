#pragma once

// Until later bosses land, only the Shrine exposes its fifth (boss) stage.
constexpr int scene_stage_count(int scene) { return scene == 0 ? 5 : 4; }
constexpr bool stage_has_singyoku(int scene, int stage) { return scene == 0 && stage == 4; }
constexpr int stage_number(int scene, int stage)
{
    return scene == 0 ? stage + 1 : 5 + ((scene - 1) / 2) * 5 + stage + 1;
}

enum RouteChoice { ROUTE_MAKAI, ROUTE_JIGOKU };
constexpr int route_first_scene(RouteChoice route) { return route == ROUTE_MAKAI ? 1 : 2; }
