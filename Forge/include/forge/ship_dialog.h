#pragma once

#include <QDialog>
#include <QString>

class QCheckBox;
class QComboBox;
class QLineEdit;

namespace queen
{
    template <size_t> class ComponentRegistry;
} // namespace queen

namespace waggle
{
    class ProjectManager;
} // namespace waggle

namespace forge
{
    class ShipDialog : public QDialog
    {
        Q_OBJECT
    public:
        explicit ShipDialog(QWidget* parent = nullptr);

        void Configure(waggle::ProjectManager* project, queen::ComponentRegistry<256>* registry,
                       const QString& engineRoot);

    private slots:
        void OnBrowseOutput();
        void OnShipClicked();

    private:
        QLineEdit* m_outputEdit{};
        QComboBox* m_configCombo{};
        QCheckBox* m_freshCookCheck{};
        QCheckBox* m_buildGameplayCheck{};

        waggle::ProjectManager* m_project{};
        queen::ComponentRegistry<256>* m_registry{};
        QString m_engineRoot;
    };
} // namespace forge
