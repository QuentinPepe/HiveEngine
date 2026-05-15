#include <forge/vulkan_viewport_widget.h>

#include <swarm/swarm.h>

#include <terra/input/keys.h>
#include <terra/terra.h>

#ifdef Q_OS_WIN
#define TERRA_NATIVE_WIN32
#include <terra/terra_native.h>
#include <windows.h>
#endif

#include <QApplication>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWindow>

namespace forge
{
    namespace
    {
#ifdef TERRA_NATIVE_WIN32
        struct VirtualKeyMapping
        {
            int m_virtualKey;
            terra::Key m_terraKey;
        };

        // Polled every frame via GetAsyncKeyState. Qt's nativeEventFilter misses WM_KEYDOWN
        // sent to the foreign embedded GLFW window, so we read physical key state directly.
        constexpr VirtualKeyMapping kPolledKeyMappings[] = {
            {'A', terra::Key::A},     {'B', terra::Key::B},   {'C', terra::Key::C},   {'D', terra::Key::D},
            {'E', terra::Key::E},     {'F', terra::Key::F},   {'G', terra::Key::G},   {'H', terra::Key::H},
            {'I', terra::Key::I},     {'J', terra::Key::J},   {'K', terra::Key::K},   {'L', terra::Key::L},
            {'M', terra::Key::M},     {'N', terra::Key::N},   {'O', terra::Key::O},   {'P', terra::Key::P},
            {'Q', terra::Key::Q},     {'R', terra::Key::R},   {'S', terra::Key::S},   {'T', terra::Key::T},
            {'U', terra::Key::U},     {'V', terra::Key::V},   {'W', terra::Key::W},   {'X', terra::Key::X},
            {'Y', terra::Key::Y},     {'Z', terra::Key::Z},
            {VK_SPACE, terra::Key::SPACE},
            {VK_LSHIFT, terra::Key::LEFT_SHIFT}, {VK_RSHIFT, terra::Key::RIGHT_SHIFT},
            {VK_LCONTROL, terra::Key::LEFT_CONTROL}, {VK_RCONTROL, terra::Key::RIGHT_CONTROL},
            {VK_LMENU, terra::Key::LEFT_ALT}, {VK_RMENU, terra::Key::RIGHT_ALT},
            {VK_ESCAPE, terra::Key::ESCAPE},
            {VK_F1, terra::Key::F1},   {VK_F2, terra::Key::F2},   {VK_F3, terra::Key::F3},
            {VK_F4, terra::Key::F4},   {VK_F5, terra::Key::F5},   {VK_F6, terra::Key::F6},
            {VK_F7, terra::Key::F7},   {VK_F8, terra::Key::F8},   {VK_F9, terra::Key::F9},
            {VK_F10, terra::Key::F10}, {VK_F11, terra::Key::F11}, {VK_F12, terra::Key::F12},
        };

        bool IsTextInputFocused()
        {
            QWidget* widget = QApplication::focusWidget();
            if (widget == nullptr)
            {
                return false;
            }
            return qobject_cast<QLineEdit*>(widget) != nullptr ||
                   qobject_cast<QTextEdit*>(widget) != nullptr ||
                   qobject_cast<QPlainTextEdit*>(widget) != nullptr ||
                   qobject_cast<QAbstractSpinBox*>(widget) != nullptr;
        }

        void PollKeyboardFromOS(terra::InputState& state)
        {
            if (IsTextInputFocused())
            {
                return;
            }
            const HWND foreground = ::GetForegroundWindow();
            const HWND self = ::GetActiveWindow();
            // Only sample when our app is the foreground window so we don't capture global typing.
            if (foreground != nullptr && self != nullptr && foreground != self &&
                ::GetWindow(foreground, GW_OWNER) != self)
            {
                return;
            }
            for (const VirtualKeyMapping& mapping : kPolledKeyMappings)
            {
                const SHORT keyState = ::GetAsyncKeyState(mapping.m_virtualKey);
                const bool pressed = (keyState & 0x8000) != 0;
                const int index = static_cast<int>(mapping.m_terraKey);
                if (index >= 0 && index < static_cast<int>(sizeof(state.m_keys) / sizeof(state.m_keys[0])))
                {
                    state.m_keys[index] = pressed;
                }
            }
        }

        void HandleMouseWheelMessage(MSG* msg, terra::InputState& state)
        {
            if (msg->message != WM_MOUSEWHEEL && msg->message != WM_MOUSEHWHEEL)
            {
                return;
            }
            const float delta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(msg->wParam)) /
                                static_cast<float>(WHEEL_DELTA);
            if (msg->message == WM_MOUSEWHEEL)
            {
                state.m_scrollDeltaY += delta;
            }
            else
            {
                state.m_scrollDeltaX += delta;
            }
        }
#endif
    } // namespace

    NativeToTerraInputBridge::NativeToTerraInputBridge(terra::WindowContext* window) : m_window{window} {}

    bool NativeToTerraInputBridge::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
    {
        (void)result;
#ifdef TERRA_NATIVE_WIN32
        if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG")
        {
            return false;
        }
        if (m_window == nullptr)
        {
            return false;
        }

        auto* msg = static_cast<MSG*>(message);
        if (msg == nullptr)
        {
            return false;
        }

        terra::InputState* state = terra::GetWindowInputState(m_window);
        if (state == nullptr)
        {
            return false;
        }

        HandleMouseWheelMessage(msg, *state);
#else
        (void)eventType;
        (void)message;
#endif
        return false;
    }

    void NativeToTerraInputBridge::PollKeyboard()
    {
#ifdef TERRA_NATIVE_WIN32
        if (m_window == nullptr)
        {
            return;
        }
        terra::InputState* state = terra::GetWindowInputState(m_window);
        if (state == nullptr)
        {
            return;
        }
        PollKeyboardFromOS(*state);
#endif
    }

    VulkanViewportWidget::VulkanViewportWidget(QWidget* parent)
        : QWidget{parent}
    {
        setMinimumSize(320, 240);
    }

    VulkanViewportWidget::~VulkanViewportWidget()
    {
        if (m_inputBridge != nullptr)
        {
            QCoreApplication::instance()->removeNativeEventFilter(m_inputBridge);
            delete m_inputBridge;
            m_inputBridge = nullptr;
        }
    }

    void VulkanViewportWidget::EmbedGlfwWindow(terra::WindowContext* window)
    {
        if (window == nullptr)
        {
            return;
        }
        if (m_window != nullptr)
        {
            return;
        }

        m_window = window;

#ifdef TERRA_NATIVE_WIN32
        auto native = terra::GetNativeWindow(window);
        if (native.m_window == nullptr)
        {
            return;
        }

        auto* foreignWindow = QWindow::fromWinId(reinterpret_cast<WId>(native.m_window));
        m_embedded = QWidget::createWindowContainer(foreignWindow, this);

        auto* layout = new QVBoxLayout{this};
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(m_embedded);

        if (m_inputBridge == nullptr)
        {
            m_inputBridge = new NativeToTerraInputBridge{m_window};
            QCoreApplication::instance()->installNativeEventFilter(m_inputBridge);
        }
#endif
    }

    NativeToTerraInputBridge* VulkanViewportWidget::InputBridge() const
    {
        return m_inputBridge;
    }

    void VulkanViewportWidget::SetRenderContext(swarm::RenderContext* renderContext)
    {
        m_renderContext = renderContext;
    }
} // namespace forge
