#pragma once

#include <comb/default_allocator.h>

#include <wax/containers/string.h>
#include <wax/containers/vector.h>
#include <wax/serialization/byte_span.h>

namespace nectar
{
    /// Material info extracted from a glTF file.
    struct GltfMaterialInfo
    {
        int32_t m_materialIndex{-1};
        wax::String m_name{};
        wax::String m_baseColorTexture{}; // relative path to albedo texture (empty if none)
        float m_baseColorFactor[4]{1.f, 1.f, 1.f, 1.f};
        wax::String m_normalTexture{};
        wax::String m_metallicRoughnessTexture{};
        float m_metallicFactor{1.f};
        float m_roughnessFactor{1.f};
        float m_alphaCutoff{0.f}; // >0 enables alpha test (glTF MASK mode)
        bool m_doubleSided{false};
    };

    /// Parse a glTF/GLB blob and extract per-material texture info.
    /// Returns one entry per material in the glTF materials[] array.
    HIVE_API wax::Vector<GltfMaterialInfo> ParseGltfMaterials(wax::ByteSpan gltfData, comb::DefaultAllocator& alloc);

    /// Compute the filename used for an embedded glTF image (buffer-view or data URI).
    /// Used by both the extractor (writes the file) and the material parser (references it).
    /// imageName is the image's optional name from glTF; mimeType is e.g. "image/png".
    /// imageIndex is the position in glTF data->images[].
    HIVE_API wax::String MakeEmbeddedTextureFileName(wax::StringView imageName, wax::StringView mimeType,
                                                    size_t imageIndex, comb::DefaultAllocator& alloc);

    /// Result entry for an extracted embedded texture.
    struct GltfExtractedTexture
    {
        wax::String m_fileName{}; // basename written into modelDir
    };

    /// Extract images embedded in a glTF/GLB (buffer-view or data:image/...) into modelDir.
    /// External-URI images are left untouched (assumed sibling-copied separately).
    /// Returns one entry per glTF image in declaration order; entries are empty when
    /// the image is external or extraction failed.
    HIVE_API wax::Vector<GltfExtractedTexture> ExtractEmbeddedTextures(wax::ByteSpan gltfData,
                                                                      const char* modelDir,
                                                                      comb::DefaultAllocator& alloc);
} // namespace nectar
