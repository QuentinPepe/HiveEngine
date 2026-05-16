#include <forge/ship_dialog.h>

#include <forge/ship_progress_dialog.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <waggle/project/project_manager.h>
#include <waggle/project/ship_pipeline.h>

namespace forge
{
    namespace
    {
        QString DefaultOutputDir(waggle::ProjectManager* project)
        {
            if (project == nullptr)
            {
                return QDir::currentPath();
            }
            const auto rootView = project->Paths().m_root.View();
            const QString root = QString::fromUtf8(rootView.Data(), static_cast<int>(rootView.Size()));
            const auto nameView = project->Project().Name();
            const QString name =
                QString::fromUtf8(nameView.Data(), static_cast<int>(nameView.Size())).remove(' ').toLower();
            const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
            return QDir{root}.filePath(QStringLiteral("dist/%1-%2").arg(name).arg(stamp));
        }
    } // namespace

    ShipDialog::ShipDialog(QWidget* parent)
        : QDialog{parent}
    {
        setWindowTitle("Package & Ship");
        setMinimumWidth(560);

        auto* layout = new QVBoxLayout{this};

        auto* form = new QFormLayout{};

        auto* outputRow = new QHBoxLayout{};
        m_outputEdit = new QLineEdit{this};
        outputRow->addWidget(m_outputEdit, 1);
        auto* browseBtn = new QPushButton{"...", this};
        browseBtn->setMaximumWidth(32);
        connect(browseBtn, &QPushButton::clicked, this, &ShipDialog::OnBrowseOutput);
        outputRow->addWidget(browseBtn);
        form->addRow("Output directory:", outputRow);

        m_configCombo = new QComboBox{this};
        m_configCombo->addItems({"Retail", "Profile", "Release", "Debug"});
        form->addRow("Build config:", m_configCombo);

        m_buildGameplayCheck = new QCheckBox{"Rebuild gameplay DLL", this};
        m_buildGameplayCheck->setChecked(true);
        form->addRow("", m_buildGameplayCheck);

        m_freshCookCheck = new QCheckBox{"Force fresh cook of reachable assets", this};
        m_freshCookCheck->setChecked(true);
        form->addRow("", m_freshCookCheck);

        layout->addLayout(form);

        auto* note = new QLabel{
            "<b>Note:</b> requires <code>out/build/hive-game-retail/bin/Retail/hive_launcher.exe</code> "
            "to exist in the engine tree.",
            this};
        note->setTextFormat(Qt::RichText);
        note->setWordWrap(true);
        layout->addWidget(note);

        auto* buttons = new QDialogButtonBox{this};
        auto* shipBtn = buttons->addButton("Ship", QDialogButtonBox::AcceptRole);
        buttons->addButton(QDialogButtonBox::Cancel);
        connect(shipBtn, &QPushButton::clicked, this, &ShipDialog::OnShipClicked);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);
    }

    void ShipDialog::Configure(waggle::ProjectManager* project, queen::ComponentRegistry<256>* registry,
                               const QString& engineRoot)
    {
        m_project = project;
        m_registry = registry;
        m_engineRoot = engineRoot;
        m_outputEdit->setText(QDir::toNativeSeparators(DefaultOutputDir(project)));
    }

    void ShipDialog::OnBrowseOutput()
    {
        const QString dir = QFileDialog::getExistingDirectory(this, "Select output directory", m_outputEdit->text());
        if (!dir.isEmpty())
        {
            m_outputEdit->setText(QDir::toNativeSeparators(dir));
        }
    }

    void ShipDialog::OnShipClicked()
    {
        const QString outputDir = QDir::fromNativeSeparators(m_outputEdit->text()).trimmed();
        if (outputDir.isEmpty() || m_project == nullptr || m_registry == nullptr)
        {
            reject();
            return;
        }

        const QByteArray outBytes = outputDir.toUtf8();
        const QByteArray configBytes = m_configCombo->currentText().toUtf8();
        const QByteArray engineBytes = m_engineRoot.toUtf8();

        waggle::ShipRequest request{};
        request.m_outputDir = wax::StringView{outBytes.constData(), static_cast<size_t>(outBytes.size())};
        request.m_config = wax::StringView{configBytes.constData(), static_cast<size_t>(configBytes.size())};
        request.m_engineRoot = wax::StringView{engineBytes.constData(), static_cast<size_t>(engineBytes.size())};
        request.m_buildGameplay = m_buildGameplayCheck->isChecked();
        request.m_forceFreshCook = m_freshCookCheck->isChecked();

        auto* dlg = new ShipProgressDialog{parentWidget()};
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->StartShip(m_project, m_registry, request, outputDir);
        dlg->show();
        accept();
    }
} // namespace forge
