#include <forge/blueprint_panel.h>
#include <forge/blueprint_editor.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolButton>
#include <QLabel>
#include <QFileDialog>
#include <QFile>
#include <QMessageBox>

namespace forge
{
    static QToolButton* MakeToolBtn(const QString& text, const QString& tooltip, QWidget* parent)
    {
        auto* btn = new QToolButton{parent};
        btn->setText(text);
        btn->setToolTip(tooltip);
        btn->setFixedSize(28, 28);
        btn->setStyleSheet(R"(
            QToolButton {
                background: #1a1a1a; color: #ccc; border: 1px solid #2a2a2a;
                border-radius: 4px; font-size: 12px; font-weight: bold;
            }
            QToolButton:hover { border-color: #f0a500; color: #f0a500; }
            QToolButton:pressed { background: #f0a500; color: #0d0d0d; }
        )");
        return btn;
    }

    BlueprintPanel::BlueprintPanel(QWidget* parent)
        : QWidget{parent}
    {
        auto* layout = new QVBoxLayout{this};
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        auto* toolbar = new QWidget{this};
        toolbar->setFixedHeight(32);
        toolbar->setStyleSheet("background: #141414; border-bottom: 1px solid #2a2a2a;");

        auto* tbLayout = new QHBoxLayout{toolbar};
        tbLayout->setContentsMargins(8, 2, 8, 2);
        tbLayout->setSpacing(4);

        m_titleLabel = new QLabel{"Blueprint - Untitled", toolbar};
        m_titleLabel->setStyleSheet("color: #f0a500; font-weight: bold; font-size: 11px; border: none;");

        auto* saveBtn = MakeToolBtn("\xf0\x9f\x92\xbe", "Save (Ctrl+S)", toolbar);
        auto* loadBtn = MakeToolBtn("\xf0\x9f\x93\x82", "Load", toolbar);
        auto* fitBtn = MakeToolBtn("\xe2\x87\xb1", "Fit All", toolbar);

        tbLayout->addWidget(m_titleLabel);
        tbLayout->addStretch();
        tbLayout->addWidget(saveBtn);
        tbLayout->addWidget(loadBtn);
        tbLayout->addWidget(fitBtn);

        layout->addWidget(toolbar);

        m_editor = new BlueprintEditor{this};
        layout->addWidget(m_editor);

        connect(fitBtn, &QToolButton::clicked, this, [this]() {
            m_editor->fitInView(m_editor->scene()->itemsBoundingRect().adjusted(-80, -80, 80, 80),
                                Qt::KeepAspectRatio);
        });

        connect(saveBtn, &QToolButton::clicked, this, [this]() { Save(); });

        connect(loadBtn, &QToolButton::clicked, this, [this]() {
            if (m_dirty)
            {
                if (!PromptSaveIfDirty())
                {
                    return;
                }
            }
            QString path = QFileDialog::getOpenFileName(this, "Load Graph", "", "Propolis Graph (*.propolis)");
            if (!path.isEmpty())
            {
                LoadFromFile(path);
            }
        });

        connect(m_editor, &BlueprintEditor::graphModified, this, &BlueprintPanel::MarkDirty);
    }

    void BlueprintPanel::LoadFromFile(const QString& path)
    {
        QFile file{path};
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            return;
        }

        QByteArray data = file.readAll();
        file.close();

        if (m_editor->GetGraph().DeserializeFromJson(data.constData()))
        {
            m_lastSavePath = path;
            m_editor->RebuildVisualsFromGraph();
            m_titleLabel->setText(QString{"Blueprint - %1"}.arg(
                QString::fromUtf8(m_editor->GetGraph().Name().CStr())));
            MarkClean();
        }
    }

    bool BlueprintPanel::Save()
    {
        QString path = m_lastSavePath;
        if (path.isEmpty())
        {
            path = QFileDialog::getSaveFileName(this, "Save Graph", "", "Propolis Graph (*.propolis)");
            if (path.isEmpty())
            {
                return false;
            }
        }

        auto json = m_editor->GetGraph().SerializeToJson();
        QFile file{path};
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            return false;
        }

        file.write(json.CStr(), static_cast<qint64>(json.Size()));
        file.close();
        m_lastSavePath = path;
        m_titleLabel->setText(QString{"Blueprint - %1"}.arg(
            QString::fromUtf8(m_editor->GetGraph().Name().CStr())));
        MarkClean();
        return true;
    }

    bool BlueprintPanel::PromptSaveIfDirty()
    {
        if (!m_dirty)
        {
            return true;
        }

        QString name = m_lastSavePath.isEmpty() ? "Untitled" :
            QFileInfo{m_lastSavePath}.baseName();

        auto result = QMessageBox::question(this, "Unsaved Changes",
            QString{"Save changes to '%1'?"}.arg(name),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

        if (result == QMessageBox::Save)
        {
            return Save();
        }

        return result == QMessageBox::Discard;
    }

    void BlueprintPanel::MarkDirty()
    {
        if (m_dirty)
        {
            return;
        }
        m_dirty = true;
        emit dirtyChanged(true);
    }

    void BlueprintPanel::MarkClean()
    {
        m_dirty = false;
        emit dirtyChanged(false);
    }
} // namespace forge
