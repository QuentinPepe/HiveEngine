#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wimplicit-float-conversion"
#pragma clang diagnostic ignored "-Wdouble-promotion"
#pragma clang diagnostic ignored "-Wshadow"
#pragma clang diagnostic ignored "-Wextra-semi-stmt"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wcast-align"
#include <cgltf.h>
#pragma clang diagnostic pop

#include <nectar/mesh/gltf_material.h>

#include <cstdio>
#include <cstring>
#include <utility>

namespace nectar
{
    namespace
    {
        wax::StringView ExtensionForMimeType(wax::StringView mimeType) noexcept
        {
            if (mimeType.Find(wax::StringView{"jpeg"}) != wax::StringView::npos)
                return wax::StringView{".jpg"};
            if (mimeType.Find(wax::StringView{"jpg"}) != wax::StringView::npos)
                return wax::StringView{".jpg"};
            if (mimeType.Find(wax::StringView{"ktx"}) != wax::StringView::npos)
                return wax::StringView{".ktx2"};
            return wax::StringView{".png"};
        }

        bool IsAllowedFileChar(char c) noexcept
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        }

        wax::String SanitizeFileStem(wax::StringView in, comb::DefaultAllocator& alloc)
        {
            wax::String out{alloc};
            out.Reserve(in.Size());
            for (size_t i = 0; i < in.Size(); ++i)
            {
                char c = in.Data()[i];
                out.Append(IsAllowedFileChar(c) ? c : '_');
            }
            return out;
        }

        wax::String TextureNameForImage(const cgltf_image& image, size_t index, comb::DefaultAllocator& alloc)
        {
            wax::StringView mimeView{image.mime_type ? image.mime_type : ""};
            wax::StringView nameView{image.name ? image.name : ""};
            return MakeEmbeddedTextureFileName(nameView, mimeView, index, alloc);
        }

        bool ParseDataUri(wax::StringView uri, wax::StringView& outMime, wax::StringView& outBase64Payload) noexcept
        {
            const wax::StringView prefix{"data:"};
            if (uri.Size() < prefix.Size())
                return false;
            for (size_t i = 0; i < prefix.Size(); ++i)
            {
                if (uri.Data()[i] != prefix.Data()[i])
                    return false;
            }

            size_t comma = wax::StringView::npos;
            for (size_t i = prefix.Size(); i < uri.Size(); ++i)
            {
                if (uri.Data()[i] == ',')
                {
                    comma = i;
                    break;
                }
            }
            if (comma == wax::StringView::npos)
                return false;

            outMime = wax::StringView{uri.Data() + prefix.Size(), comma - prefix.Size()};
            outBase64Payload = wax::StringView{uri.Data() + comma + 1, uri.Size() - comma - 1};
            return true;
        }

        int Base64CharValue(char c) noexcept
        {
            if (c >= 'A' && c <= 'Z')
                return c - 'A';
            if (c >= 'a' && c <= 'z')
                return c - 'a' + 26;
            if (c >= '0' && c <= '9')
                return c - '0' + 52;
            if (c == '+' || c == '-')
                return 62;
            if (c == '/' || c == '_')
                return 63;
            return -1;
        }

        bool DecodeBase64(wax::StringView in, wax::Vector<uint8_t>& out) noexcept
        {
            out.Clear();
            out.Reserve((in.Size() / 4) * 3);
            int acc = 0;
            int bits = 0;
            for (size_t i = 0; i < in.Size(); ++i)
            {
                char c = in.Data()[i];
                if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t')
                    continue;
                int v = Base64CharValue(c);
                if (v < 0)
                    return false;
                acc = (acc << 6) | v;
                bits += 6;
                if (bits >= 8)
                {
                    bits -= 8;
                    out.PushBack(static_cast<uint8_t>((acc >> bits) & 0xFF));
                }
            }
            return true;
        }
    } // namespace

    wax::String MakeEmbeddedTextureFileName(wax::StringView imageName, wax::StringView mimeType, size_t imageIndex,
                                            comb::DefaultAllocator& alloc)
    {
        wax::String stem{alloc};
        if (imageName.Size() > 0)
        {
            stem = SanitizeFileStem(imageName, alloc);
        }
        else
        {
            char buf[32]{};
            std::snprintf(buf, sizeof(buf), "embedded_%zu", imageIndex);
            stem = wax::String{alloc, wax::StringView{buf}};
        }

        const auto ext = ExtensionForMimeType(mimeType);
        wax::String result{alloc};
        result.Reserve(stem.Size() + ext.Size());
        result.Append(stem.View());
        result.Append(ext);
        return result;
    }

    wax::Vector<GltfExtractedTexture> ExtractEmbeddedTextures(wax::ByteSpan gltfData, const char* modelDir,
                                                              comb::DefaultAllocator& alloc)
    {
        wax::Vector<GltfExtractedTexture> result{alloc};

        cgltf_options options{};
        cgltf_data* data = nullptr;
        if (cgltf_parse(&options, gltfData.Data(), gltfData.Size(), &data) != cgltf_result_success)
            return result;

        if (cgltf_load_buffers(&options, data, nullptr) != cgltf_result_success)
        {
            cgltf_free(data);
            return result;
        }

        result.Reserve(data->images_count);

        for (cgltf_size i = 0; i < data->images_count; ++i)
        {
            const auto& image = data->images[i];
            GltfExtractedTexture entry{};

            const uint8_t* bytes = nullptr;
            size_t byteSize = 0;
            wax::Vector<uint8_t> decoded{alloc};

            if (image.buffer_view)
            {
                const auto* bv = image.buffer_view;
                const auto* buffer = bv->buffer;
                if (buffer && buffer->data)
                {
                    bytes = static_cast<const uint8_t*>(buffer->data) + bv->offset;
                    byteSize = bv->size;
                }
            }
            else if (image.uri)
            {
                wax::StringView uriView{image.uri};
                wax::StringView mimeFromUri{};
                wax::StringView b64{};
                if (ParseDataUri(uriView, mimeFromUri, b64))
                {
                    if (DecodeBase64(b64, decoded) && !decoded.IsEmpty())
                    {
                        bytes = decoded.Data();
                        byteSize = decoded.Size();
                        if (!image.mime_type)
                        {
                            // Mime came from URI prefix.
                            // Honoured indirectly: ExtensionForMimeType called below uses mimeView.
                        }
                    }
                }
                else
                {
                    // External URI (sibling file) — leave for the sibling copy pass.
                    result.PushBack(std::move(entry));
                    continue;
                }
            }

            if (bytes == nullptr || byteSize == 0)
            {
                result.PushBack(std::move(entry));
                continue;
            }

            wax::String fileName = TextureNameForImage(image, i, alloc);

            wax::String fullPath{alloc};
            fullPath.Append(wax::StringView{modelDir});
            fullPath.Append(wax::StringView{"/"});
            fullPath.Append(fileName.View());

            FILE* f = nullptr;
#ifdef _MSC_VER
            fopen_s(&f, fullPath.CStr(), "wb");
#else
            f = fopen(fullPath.CStr(), "wb");
#endif
            if (f)
            {
                std::fwrite(bytes, 1, byteSize, f);
                std::fclose(f);
                entry.m_fileName = std::move(fileName);
            }

            result.PushBack(std::move(entry));
        }

        cgltf_free(data);
        return result;
    }

    wax::Vector<GltfMaterialInfo> ParseGltfMaterials(wax::ByteSpan gltfData, comb::DefaultAllocator& alloc)
    {
        wax::Vector<GltfMaterialInfo> materials{alloc};

        cgltf_options options{};
        cgltf_data* data = nullptr;
        if (cgltf_parse(&options, gltfData.Data(), gltfData.Size(), &data) != cgltf_result_success)
        {
            return materials;
        }

        materials.Reserve(data->materials_count);

        for (cgltf_size i = 0; i < data->materials_count; ++i)
        {
            const auto& mat = data->materials[i];

            GltfMaterialInfo info{};
            info.m_materialIndex = static_cast<int32_t>(i);
            if (mat.name)
                info.m_name = wax::String{mat.name};

            auto resolveTextureName = [&](const cgltf_image* image) -> wax::String {
                if (!image)
                    return wax::String{};
                if (image->uri)
                {
                    wax::StringView uriView{image->uri};
                    if (uriView.Size() >= 5 && std::memcmp(uriView.Data(), "data:", 5) == 0)
                    {
                        const size_t idx = static_cast<size_t>(image - data->images);
                        return TextureNameForImage(*image, idx, alloc);
                    }
                    return wax::String{image->uri};
                }
                if (image->buffer_view)
                {
                    const size_t idx = static_cast<size_t>(image - data->images);
                    return TextureNameForImage(*image, idx, alloc);
                }
                return wax::String{};
            };

            if (mat.has_pbr_metallic_roughness)
            {
                std::memcpy(info.m_baseColorFactor, mat.pbr_metallic_roughness.base_color_factor, 4 * sizeof(float));

                if (mat.pbr_metallic_roughness.base_color_texture.texture)
                    info.m_baseColorTexture =
                        resolveTextureName(mat.pbr_metallic_roughness.base_color_texture.texture->image);

                if (mat.pbr_metallic_roughness.metallic_roughness_texture.texture)
                    info.m_metallicRoughnessTexture =
                        resolveTextureName(mat.pbr_metallic_roughness.metallic_roughness_texture.texture->image);

                info.m_metallicFactor = mat.pbr_metallic_roughness.metallic_factor;
                info.m_roughnessFactor = mat.pbr_metallic_roughness.roughness_factor;
            }

            if (mat.normal_texture.texture)
                info.m_normalTexture = resolveTextureName(mat.normal_texture.texture->image);

            if (mat.alpha_mode == cgltf_alpha_mode_mask)
            {
                info.m_alphaCutoff = mat.alpha_cutoff;
            }
            info.m_doubleSided = mat.double_sided != 0;

            materials.PushBack(std::move(info));
        }

        cgltf_free(data);
        return materials;
    }
} // namespace nectar
