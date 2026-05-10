#pragma once

#include <QWidget>
#include <QString>

class QLabel;

namespace forge
{
    class BlueprintEditor;

    class BlueprintPanel : public QWidget
    {
        Q_OBJECT

    public:
        explicit BlueprintPanel(QWidget* parent = nullptr);

        BlueprintEditor* Editor() const { return m_editor; }
        const QString& FilePath() const { return m_lastSavePath; }
        bool IsDirty() const { return m_dirty; }
        void LoadFromFile(const QString& path);
        bool Save();
        bool PromptSaveIfDirty();

    signals:
        void dirtyChanged(bool dirty);

    private:
        void MarkDirty();
        void MarkClean();

        BlueprintEditor* m_editor;
        QLabel* m_titleLabel{};
        QString m_lastSavePath;
        bool m_dirty{false};
    };
} // namespace forge
