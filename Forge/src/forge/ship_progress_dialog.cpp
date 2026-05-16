#include <forge/ship_progress_dialog.h>

#include <forge/ship_worker.h>

#include <QCloseEvent>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

namespace forge
{
    ShipProgressDialog::ShipProgressDialog(QWidget* parent)
        : QDialog{parent}
    {
        setWindowTitle("Package & Ship");
        resize(720, 480);

        auto* layout = new QVBoxLayout{this};

        m_progress = new QProgressBar{this};
        m_progress->setRange(0, 0);
        m_progress->setTextVisible(true);
        layout->addWidget(m_progress);

        m_log = new QPlainTextEdit{this};
        m_log->setReadOnly(true);
        m_log->setMaximumBlockCount(10000);
        QFont logFont{"Consolas"};
        logFont.setStyleHint(QFont::Monospace);
        m_log->setFont(logFont);
        layout->addWidget(m_log, 1);

        auto* buttons = new QHBoxLayout{};
        buttons->addStretch();
        m_openFolderButton = new QPushButton{"Open Output Folder", this};
        m_openFolderButton->setEnabled(false);
        connect(m_openFolderButton, &QPushButton::clicked, this, [this]() {
            if (!m_outputDir.isEmpty())
            {
                QDesktopServices::openUrl(QUrl::fromLocalFile(m_outputDir));
            }
        });
        buttons->addWidget(m_openFolderButton);

        m_closeButton = new QPushButton{"Cancel", this};
        connect(m_closeButton, &QPushButton::clicked, this, &QDialog::close);
        buttons->addWidget(m_closeButton);

        layout->addLayout(buttons);
    }

    ShipProgressDialog::~ShipProgressDialog()
    {
        if (m_thread != nullptr)
        {
            m_thread->quit();
            m_thread->wait();
        }
    }

    void ShipProgressDialog::StartShip(waggle::ProjectManager* project, queen::ComponentRegistry<256>* registry,
                                       const waggle::ShipRequest& request, const QString& outputDir)
    {
        m_outputDir = outputDir;
        m_thread = new QThread{this};
        m_worker = new ShipWorker{};
        m_worker->Configure(project, registry, request);
        m_worker->moveToThread(m_thread);

        connect(m_thread, &QThread::started, m_worker, &ShipWorker::Run);
        connect(m_worker, &ShipWorker::stageChanged, this, &ShipProgressDialog::OnStageChanged);
        connect(m_worker, &ShipWorker::logLine, this, &ShipProgressDialog::OnLogLine);
        connect(m_worker, &ShipWorker::progress, this, &ShipProgressDialog::OnProgress);
        connect(m_worker, &ShipWorker::finished, this, &ShipProgressDialog::OnFinished);
        connect(m_worker, &ShipWorker::finished, m_thread, &QThread::quit);
        connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

        m_thread->start();
    }

    void ShipProgressDialog::closeEvent(QCloseEvent* event)
    {
        if (!m_finished && m_thread != nullptr && m_thread->isRunning())
        {
            event->ignore();
            return;
        }
        QDialog::closeEvent(event);
    }

    void ShipProgressDialog::OnStageChanged(int stage, const QString& message)
    {
        m_progress->setFormat(message);
        m_log->appendPlainText(QStringLiteral("[%1] %2").arg(stage).arg(message));
    }

    void ShipProgressDialog::OnLogLine(const QString& line, bool isError)
    {
        if (isError)
        {
            m_log->appendPlainText(QStringLiteral("[ERR] ") + line);
        }
        else
        {
            m_log->appendPlainText(line);
        }
    }

    void ShipProgressDialog::OnProgress(int current, int total)
    {
        if (total <= 0)
        {
            m_progress->setRange(0, 0);
            return;
        }
        m_progress->setRange(0, total);
        m_progress->setValue(current);
    }

    void ShipProgressDialog::OnFinished(bool success)
    {
        m_finished = true;
        m_progress->setRange(0, 1);
        m_progress->setValue(1);
        m_progress->setFormat(success ? QStringLiteral("Done") : QStringLiteral("Failed"));
        m_closeButton->setText("Close");
        m_openFolderButton->setEnabled(success);
        m_log->appendPlainText(success ? QStringLiteral("=== SHIP SUCCESS ===") : QStringLiteral("=== SHIP FAILED ==="));
    }
} // namespace forge
