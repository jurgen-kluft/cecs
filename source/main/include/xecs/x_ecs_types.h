#ifndef __XECS_ECS_TYPES_H__
#define __XECS_ECS_TYPES_H__
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

namespace xcore
{
    // Opaque 32 bits entity identifier.
    //
    // A 32 bits entity identifier guarantees per type:
    //   - 16 bits for the entity count (65536)
    //   - 8 bit for the type(256), so in total you could have 16 million entities
    //   - 8 bit for the version(resets in[0 - 255]).


} // namespace xcore

#endif // __XECS_ECS_TYPES_H__