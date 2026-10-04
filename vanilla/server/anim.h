/*
 * Project Tsukasa — Display Server Animation Easing Library
 *
 * Copyright (C) 2025-2026 frosty (@enafrosty) and Project Tsukasa contributors.
 *
 * Project Tsukasa was created and is maintained by frosty (@enafrosty).
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. See the top-level LICENSE file.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef _VANILLA_ANIM_H
#define _VANILLA_ANIM_H

static inline float anim_ease_out_cubic(float t)
{
    if (t <= 0.0f)
        return 0.0f;
    if (t >= 1.0f)
        return 1.0f;
    float u = 1.0f - t;
    return 1.0f - (u * u * u);
}

static inline float anim_ease_out_quad(float t)
{
    if (t <= 0.0f)
        return 0.0f;
    if (t >= 1.0f)
        return 1.0f;
    float u = 1.0f - t;
    return 1.0f - (u * u);
}

static inline float anim_ease_in_out_quad(float t)
{
    if (t <= 0.0f)
        return 0.0f;
    if (t >= 1.0f)
        return 1.0f;
    if (t < 0.5f) {
        return 2.0f * t * t;
    } else {
        float u = -2.0f * t + 2.0f;
        return 1.0f - (u * u) * 0.5f;
    }
}

static inline float anim_lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

#endif /* _VANILLA_ANIM_H */
