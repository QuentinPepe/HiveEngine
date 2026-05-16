#pragma once

#include <QObject>
#include <QString>

#include <waggle/project/ship_pipeline.h>

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
    class ShipWorker : public QObject
    {
        Q_OBJECT
    public:
        explicit ShipWorker(QObject* parent = nullptr);

        void Configure(waggle::ProjectManager* project, queen::ComponentRegistry<256>* registry,
                       const waggle::ShipRequest& request);

    public slots:
        void Run();

    signals:
        void stageChanged(int stage, const QString& message);
        void logLine(const QString& line, bool isError);
        void progress(int current, int total);
        void finished(bool success);

    private:
        waggle::ProjectManager* m_project{};
        queen::ComponentRegistry<256>* m_registry{};
        waggle::ShipRequest m_request{};
        QString m_outputDir;
        QString m_config;
        QString m_engineRoot;
    };
} // namespace forge
