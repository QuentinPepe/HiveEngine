#pragma once

#include <QDialog>
#include <QString>

class QProgressBar;
class QPlainTextEdit;
class QPushButton;
class QThread;

namespace queen
{
    template <size_t> class ComponentRegistry;
} // namespace queen

namespace waggle
{
    class ProjectManager;
    struct ShipRequest;
} // namespace waggle

namespace forge
{
    class ShipWorker;

    class ShipProgressDialog : public QDialog
    {
        Q_OBJECT
    public:
        explicit ShipProgressDialog(QWidget* parent = nullptr);
        ~ShipProgressDialog() override;

        void StartShip(waggle::ProjectManager* project, queen::ComponentRegistry<256>* registry,
                       const waggle::ShipRequest& request, const QString& outputDir);

    protected:
        void closeEvent(QCloseEvent* event) override;

    private slots:
        void OnStageChanged(int stage, const QString& message);
        void OnLogLine(const QString& line, bool isError);
        void OnProgress(int current, int total);
        void OnFinished(bool success);

    private:
        QProgressBar* m_progress{};
        QPlainTextEdit* m_log{};
        QPushButton* m_closeButton{};
        QPushButton* m_openFolderButton{};
        QThread* m_thread{};
        ShipWorker* m_worker{};
        QString m_outputDir;
        bool m_finished{false};
    };
} // namespace forge
