#ifndef TH01_MAIN_PLAYFLD_HPP
#define TH01_MAIN_PLAYFLD_HPP

static const int PLAYFIELD_LEFT = 0;
// Original PC-98 gameplay geometry. The DS viewport is render-only.
static const int PLAYFIELD_TOP = 64;
static const int PLAYFIELD_RIGHT = 640;
static const int PLAYFIELD_BOTTOM = 400;

static const int PLAYFIELD_W = (PLAYFIELD_RIGHT - PLAYFIELD_LEFT);
static const int PLAYFIELD_H = (PLAYFIELD_BOTTOM - PLAYFIELD_TOP);

static const int PLAYFIELD_CENTER_X = (
	((PLAYFIELD_RIGHT - PLAYFIELD_LEFT) / 2) + PLAYFIELD_LEFT
);

static const int PLAYFIELD_CENTER_Y = (
	((PLAYFIELD_BOTTOM - PLAYFIELD_TOP) / 2) + PLAYFIELD_TOP
);

static inline int playfield_fraction_x(float fraction = 1.0f) {
	// Adding a small value helps with rounding inaccuracies.
	return static_cast<int>(PLAYFIELD_W * fraction + 0.0001f);
}

static inline int playfield_fraction_y(float fraction = 1.0f) {
	// Adding a small value helps with rounding inaccuracies.
	return static_cast<int>(PLAYFIELD_H * fraction + 0.0001f);
}

#include "random.hpp"

// Calculates a random X position between the given minimum and maximum
// fractions of the playfield width.
static inline int playfield_rand_x(
	float fraction_min = 0.0f, float fraction_max = 1.0f
) {
	return (PLAYFIELD_LEFT + playfield_fraction_x(fraction_min) + (
		(irand() % playfield_fraction_x(fraction_max - fraction_min))
	));
}

// Calculates a random Y position between the given minimum and maximum
// fractions of the playfield height.
static inline int playfield_rand_y(
	float fraction_min = 0.0f, float fraction_max = 1.0f
) {
	return (PLAYFIELD_TOP + playfield_fraction_y(fraction_min) + (
		(irand() % playfield_fraction_y(fraction_max - fraction_min))
	));
}

#endif /* TH01_MAIN_PLAYFLD_HPP */
