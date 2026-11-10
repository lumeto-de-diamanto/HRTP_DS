#pragma once
// Keep direct libnds background includes on the same host VRAM/register model
// as <nds.h>; do not mix real MMIO headers into the portable renderer tests.
#include <nds.h>
