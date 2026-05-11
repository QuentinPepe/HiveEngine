#pragma once

#include <QString>
#include <QWidget>

#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

namespace forge
{
    class EditorUndoManager;

    enum class MaterialParamKind
    {
        Float,
        Float2,
        Float3,
        Float4,
        Color3,
        Color4,
        Range,
    };

    struct MaterialParamAnnotation
    {
        QString name;
        MaterialParamKind kind{MaterialParamKind::Float};
        float minValue{0.f};
        float maxValue{1.f};
        std::vector<float> defaults;
    };

    struct MaterialTextureAnnotation
    {
        QString name;
    };

    struct MaterialFeatureAnnotation
    {
        QString name;
        bool defaultValue{false};
    };

    struct MaterialSchema
    {
        QString shaderPath;
        bool shaderResolved{false};
        std::vector<MaterialParamAnnotation> params;
        std::vector<MaterialTextureAnnotation> textures;
        std::vector<MaterialFeatureAnnotation> features;
    };

    static constexpr int MATERIAL_THUMB_SIZE = 48;

    class MaterialInspector : public QWidget
    {
        Q_OBJECT

    public:
        MaterialInspector(const std::filesystem::path& path, EditorUndoManager& editorUndo,
                          QWidget* parent = nullptr);

    protected:
        bool eventFilter(QObject* obj, QEvent* event) override;

    signals:
        void materialModified();
        void browseToAsset(const QString& path);

    public:
        struct State;

    private:
        std::shared_ptr<State> m_state;
    };
} // namespace forge
