#pragma once

#include <QAbstractNativeEventFilter>
#include <QWidget>

namespace terra
{
    struct WindowContext;
}

namespace swarm
{
    struct RenderContext;
}

namespace forge
{
    // Intercepts raw Win32 messages from Qt's native event pump and forwards keyboard +
    // mouse-wheel input to Terra's InputState. The embedded GLFW window's WndProc never
    // sees these events because Qt consumes them at the message-pump level — without this
    // bridge, ZQSD/WASD and mouse wheel do nothing in the editor.
    class NativeToTerraInputBridge : public QAbstractNativeEventFilter
    {
    public:
        explicit NativeToTerraInputBridge(terra::WindowContext* window);

        bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

        // Reads physical keyboard state via GetAsyncKeyState and writes it to Terra's
        // InputState. Called every frame so input doesn't depend on which HWND has focus.
        void PollKeyboard();

    private:
        terra::WindowContext* m_window{nullptr};
    };

    class VulkanViewportWidget : public QWidget
    {
        Q_OBJECT

    public:
        explicit VulkanViewportWidget(QWidget* parent = nullptr);
        ~VulkanViewportWidget() override;

        void EmbedGlfwWindow(terra::WindowContext* window);
        void SetRenderContext(swarm::RenderContext* renderContext);
        [[nodiscard]] NativeToTerraInputBridge* InputBridge() const;

    private:
        swarm::RenderContext* m_renderContext{nullptr};
        QWidget* m_embedded{nullptr};
        terra::WindowContext* m_window{nullptr};
        NativeToTerraInputBridge* m_inputBridge{nullptr};
    };
} // namespace forge
