#include "TextureFace.glsl"
layout(std430, binding = 7) readonly buffer TextureFaces { TextureFace faces[]; };
layout(std430, binding = 8) readonly buffer MaterialPixels { uint texels[]; };
#define RF_MASK_BINDING 9
