#include <forge/ship_worker.h>

#include <comb/default_allocator.h>

#include <waggle/project/project_manager.h>

namespace forge
{
    namespace
    {
        QString StageLabel(waggle::ShipStage stage)
        {
            switch (stage)
            {
            case waggle::ShipStage::BUILDING_GAMEPLAY:
                return QStringLiteral("Building gameplay");
            case waggle::ShipStage::WALKING_SCENE:
                return QStringLiteral("Walking scene");
            case waggle::ShipStage::COOKING:
                return QStringLiteral("Cooking assets");
            case waggle::ShipStage::WRITING_PAK:
                return QStringLiteral("Writing pak");
            case waggle::ShipStage::COPYING_RUNTIME:
                return QStringLiteral("Copying runtime");
            case waggle::ShipStage::WRITING_PROJECT_FILE:
                return QStringLiteral("Writing project file");
            case waggle::ShipStage::DONE:
                return QStringLiteral("Done");
            case waggle::ShipStage::FAILED:
                return QStringLiteral("Failed");
            }
            return QStringLiteral("Ship");
        }

        void EventTrampoline(const waggle::ShipEvent& event, void* userdata)
        {
            auto* worker = static_cast<ShipWorker*>(userdata);
            const QString message = event.m_message != nullptr ? QString::fromUtf8(event.m_message) : QString{};
            QMetaObject::invokeMethod(worker, "stageChanged", Qt::QueuedConnection,
                                      Q_ARG(int, static_cast<int>(event.m_stage)),
                                      Q_ARG(QString, StageLabel(event.m_stage)));
            if (!message.isEmpty())
            {
                QMetaObject::invokeMethod(worker, "logLine", Qt::QueuedConnection,
                                          Q_ARG(QString, message), Q_ARG(bool, event.m_isError));
            }
            if (event.m_total > 0)
            {
                QMetaObject::invokeMethod(worker, "progress", Qt::QueuedConnection,
                                          Q_ARG(int, static_cast<int>(event.m_current)),
                                          Q_ARG(int, static_cast<int>(event.m_total)));
            }
        }
    } // namespace

    ShipWorker::ShipWorker(QObject* parent)
        : QObject{parent}
    {
    }

    void ShipWorker::Configure(waggle::ProjectManager* project, queen::ComponentRegistry<256>* registry,
                               const waggle::ShipRequest& request)
    {
        m_project = project;
        m_registry = registry;
        m_request = request;
        m_outputDir = QString::fromUtf8(request.m_outputDir.Data(), static_cast<int>(request.m_outputDir.Size()));
        m_config = QString::fromUtf8(request.m_config.Data(), static_cast<int>(request.m_config.Size()));
        m_engineRoot = QString::fromUtf8(request.m_engineRoot.Data(), static_cast<int>(request.m_engineRoot.Size()));
        const QByteArray outputBytes = m_outputDir.toUtf8();
        const QByteArray configBytes = m_config.toUtf8();
        const QByteArray engineBytes = m_engineRoot.toUtf8();
        m_request.m_outputDir = wax::StringView{outputBytes.constData(), static_cast<size_t>(outputBytes.size())};
        m_request.m_config = wax::StringView{configBytes.constData(), static_cast<size_t>(configBytes.size())};
        m_request.m_engineRoot = wax::StringView{engineBytes.constData(), static_cast<size_t>(engineBytes.size())};
    }

    void ShipWorker::Run()
    {
        if (m_project == nullptr || m_registry == nullptr)
        {
            emit finished(false);
            return;
        }

        const QByteArray outputBytes = m_outputDir.toUtf8();
        const QByteArray configBytes = m_config.toUtf8();
        const QByteArray engineBytes = m_engineRoot.toUtf8();
        waggle::ShipRequest request = m_request;
        request.m_outputDir = wax::StringView{outputBytes.constData(), static_cast<size_t>(outputBytes.size())};
        request.m_config = wax::StringView{configBytes.constData(), static_cast<size_t>(configBytes.size())};
        request.m_engineRoot = wax::StringView{engineBytes.constData(), static_cast<size_t>(engineBytes.size())};

        waggle::ShipPipeline pipeline{comb::GetDefaultAllocator(), *m_project, *m_registry};
        const bool success = pipeline.Run(request, &EventTrampoline, this);
        emit finished(success);
    }
} // namespace forge
