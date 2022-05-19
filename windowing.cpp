#include "pch.h"
#include "windowing.h"

#include "util.h"

using namespace winrt;

/*
void apply_theme_to_element(Windows::UI::Xaml::DependencyObject const& e, Windows::UI::Xaml::ElementTheme theme) {
    using Windows::UI::Xaml::Media::VisualTreeHelper;
    using Windows::UI::Xaml::FrameworkElement;
    if (auto fe = e.try_as<FrameworkElement>()) {
        fe.RequestedTheme(theme);
    }
    auto n = VisualTreeHelper::GetChildrenCount(e);
    for (decltype(n) i = 0; i < n; i++) {
        apply_theme_to_element(VisualTreeHelper::GetChild(e, i), theme);
    }
}
*/

namespace windowing {
    static constexpr wchar_t xaml_window_class_name[] = L"RAXamlHostWindowClass";
    static Windows::UI::Xaml::Hosting::WindowsXamlManager windows_xaml_manager{ nullptr };

    struct XamlWindowImpDetails {
        HWND root_hwnd, xaml_hwnd;
        Windows::UI::Xaml::Hosting::DesktopWindowXamlSource desktop_src;
        //winrt::com_ptr<IDesktopWindowXamlSourceNative2> desktop_src_native2;
        winrt::impl::com_ref<IDesktopWindowXamlSourceNative2> desktop_src_native2;
        Windows::UI::Xaml::Application::UnhandledException_revoker ue_revoker;
        WindowTheme window_theme;
        std::wstring original_window_title;
        WPARAM last_size_wparam;
        WINDOWPLACEMENT last_wnd_placement;
    };

    LRESULT CALLBACK XamlWindow::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (msg == WM_CREATE) {
            void* copied_this = reinterpret_cast<LPCREATESTRUCTW>(lParam)->lpCreateParams;
            SetWindowLongPtrW(hwnd, 0, reinterpret_cast<LONG_PTR>(copied_this));
            return 0;
        }
        XamlWindow* copied_this = reinterpret_cast<XamlWindow*>(GetWindowLongPtrW(hwnd, 0));
        if (copied_this == nullptr) {
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
        if (copied_this->m_imp->window_theme == WindowTheme::FollowSystem) {
            if (util::win32::dark_mode::is_color_scheme_change_message(msg, lParam)) {
                util::win32::dark_mode::update_title_bar_theme_color(hwnd);
                return 0;
            }
        }
        switch (msg) {
        case WM_SIZE: {
            RECT rt;
            GetClientRect(hwnd, &rt);
            SetWindowPos(
                copied_this->m_imp->xaml_hwnd,
                nullptr,
                0, 0,
                rt.right, rt.bottom,
                SWP_NOMOVE | SWP_NOZORDER
            );
            // Workaround a bug where ContentDialog behaves incorrectly when resizing
            if (auto elem = copied_this->content()) {
                auto ivec = Windows::UI::Xaml::Media::VisualTreeHelper::GetOpenPopupsForXamlRoot(elem.XamlRoot());
                if (ivec.Size() > 0) {
                    if (copied_this->m_imp->last_size_wparam != SIZE_MINIMIZED
                        && wParam != SIZE_MINIMIZED)
                    {
                        SetWindowPlacement(hwnd, &copied_this->m_imp->last_wnd_placement);
                        copied_this->m_imp->last_size_wparam = wParam;
                        return 0;
                    }
                }
            }
            copied_this->m_imp->last_size_wparam = wParam;
            GetWindowPlacement(hwnd, &copied_this->m_imp->last_wnd_placement);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            LPMINMAXINFO pmmi = reinterpret_cast<LPMINMAXINFO>(lParam);
            pmmi->ptMinTrackSize = { 400, 300 };
            return 0;
        }
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            copied_this->m_imp->desktop_src.Close();
            DestroyWindow(hwnd);
            return 0;
        case WM_SIZING:
            // Workaround a bug where ContentDialog behaves incorrectly when resizing
            if (auto elem = copied_this->content()) {
                auto ivec = Windows::UI::Xaml::Media::VisualTreeHelper::GetOpenPopupsForXamlRoot(elem.XamlRoot());
                if (ivec.Size() > 0) {
                    RECT rt;
                    GetWindowRect(hwnd, &rt);
                    *reinterpret_cast<RECT*>(lParam) = rt;
                }
            }
            return TRUE;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
    }
    bool XamlWindow::get_is_global_res_inited(void) {
        static bool result = []() -> bool {
            winrt::init_apartment(winrt::apartment_type::single_threaded);

            windows_xaml_manager =
                Windows::UI::Xaml::Hosting::WindowsXamlManager::InitializeForCurrentThread();

            WNDCLASSW wc;
            wc.style = 0;
            wc.lpfnWndProc = XamlWindow::WindowProc;
            wc.cbClsExtra = 0;
            wc.cbWndExtra = sizeof(void*);
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.hIcon = nullptr;
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            wc.lpszMenuName = nullptr;
            wc.lpszClassName = xaml_window_class_name;
            if (RegisterClassW(&wc) == 0) {
                return false;
            }

            util::win32::dark_mode::allow_dark_mode_for_app(util::win32::dark_mode::AppDarkMode::AllowDark);

            return true;
        }();
        return result;
    }
    XamlWindow::~XamlWindow() {
        if (m_imp != nullptr) {
            DestroyWindow(m_imp->root_hwnd);
            delete m_imp;
        }
    }
    XamlWindow::XamlWindow(std::nullptr_t) noexcept : m_imp(nullptr) {}
    XamlWindow::XamlWindow(XamlWindow&& other) noexcept : m_imp(other.m_imp) {
        other.m_imp = nullptr;
        SetWindowLongPtrW(m_imp->root_hwnd, 0, reinterpret_cast<LONG_PTR>(this));
    }
    XamlWindow XamlWindow::create_simple(const wchar_t* title) {
        if (!XamlWindow::get_is_global_res_inited()) {
            throw hresult_error(E_FAIL, L"无法初始化窗口全局资源");
        }

        XamlWindow window(nullptr);
        // Init XamlWindow structure and make use of RRID
        window.m_imp = new XamlWindowImpDetails;
        window.m_imp->root_hwnd = CreateWindowExW(
            WS_EX_NOREDIRECTIONBITMAP,
            xaml_window_class_name,
            title,
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            CW_USEDEFAULT, CW_USEDEFAULT,
            nullptr,
            nullptr,
            GetModuleHandleW(nullptr),
            &window
        );
        if (window.m_imp->root_hwnd == nullptr) {
            throw hresult_error(E_FAIL, L"无法创建窗口");
        }

        util::win32::dark_mode::allow_dark_mode_for_window(window.m_imp->root_hwnd, true);
        util::win32::dark_mode::update_title_bar_theme_color(window.m_imp->root_hwnd);
        window.m_imp->window_theme = WindowTheme::FollowSystem;

        /*  NOTE: Legacy way of getting interop ABI interface
        com_ptr<IUnknown> ptr;
        auto result = window.m_imp->desktop_src.as<IDesktopWindowXamlSourceNative2>();
        winrt::copy_to_abi(window.m_imp->desktop_src, *ptr.put_void());
        winrt::check_hresult(ptr->QueryInterface<IDesktopWindowXamlSourceNative2>(window.m_imp->desktop_src_native2.put()));
        winrt::check_hresult(window.m_imp->desktop_src_native2->AttachToWindow(window.m_imp->root_hwnd));
        winrt::check_hresult(window.m_imp->desktop_src_native2->get_WindowHandle(&window.m_imp->xaml_hwnd));
        */
        window.m_imp->desktop_src_native2 = window.m_imp->desktop_src.as<IDesktopWindowXamlSourceNative2>();
        winrt::check_hresult(window.m_imp->desktop_src_native2->AttachToWindow(window.m_imp->root_hwnd));
        winrt::check_hresult(window.m_imp->desktop_src_native2->get_WindowHandle(&window.m_imp->xaml_hwnd));
        {
            using namespace Windows::UI::Xaml;
            using namespace Windows::Foundation;
            using Windows::Foundation::IInspectable;
            window.m_imp->ue_revoker = Application::Current().UnhandledException(
                auto_revoke,
                [root_hwnd = window.m_imp->root_hwnd]
                (IInspectable const&, UnhandledExceptionEventArgs e) -> IAsyncAction {
                    e.Handled(true);
                    co_await resume_background();
                    MessageBoxW(
                        root_hwnd,
                        wstrprintf(
                            L"Xaml HRESULT: 0x%08x: %ls",
                            static_cast<uint32_t>(e.Exception()),
                            e.Message().c_str()
                        ).c_str(),
                        L"严重错误",
                        MB_ICONERROR
                    );
                }
            );
        }
        SetFocus(window.m_imp->xaml_hwnd);

        RECT rt;
        GetClientRect(window.m_imp->root_hwnd, &rt);
        SetWindowPos(
            window.m_imp->xaml_hwnd,
            nullptr,
            0, 0,
            rt.right, rt.bottom,
            SWP_SHOWWINDOW | SWP_NOZORDER
        );

        window.m_imp->original_window_title = title;

        return window;
    }
    bool XamlWindow::unload_global_res(void) {
        windows_xaml_manager = nullptr;
        return true;
    }
    void XamlWindow::run_loop_to_completion(void) {
        MSG msg;
        BOOL ret;
        while ((ret = GetMessageW(&msg, nullptr, 0, 0)) != 0) {
            if (!IsWindow(m_imp->root_hwnd)) {
                // Window has been destroyed; break event loop
                return;
            }
            if (ret == -1) {
                throw hresult_error(E_FAIL, L"处理窗口消息时发生了未知的严重错误");
            }
            else {
                // Workaround an XamlIsland keyboard capturing bug
                // (See https://github.com/microsoft/microsoft-ui-xaml/issues/2408)
                if (msg.message == WM_SYSKEYDOWN && msg.wParam == VK_F4) {
                    SendMessage(GetAncestor(msg.hwnd, GA_ROOT), msg.message, msg.wParam, msg.lParam);
                    continue;
                }
                BOOL xaml_src_processed_msg = FALSE;
                winrt::check_hresult(m_imp->desktop_src_native2->PreTranslateMessage(&msg, &xaml_src_processed_msg));
                if (!xaml_src_processed_msg) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            }
        }
    }
    void XamlWindow::update_element_theme(void) {
        auto elem = this->content();
        if (elem == nullptr) {
            return;
        }
        if (auto e = elem.try_as<Windows::UI::Xaml::FrameworkElement>()) {
            Windows::UI::Xaml::ElementTheme result_theme;
            switch (m_imp->window_theme) {
            case WindowTheme::FollowSystem:
                result_theme = Windows::UI::Xaml::ElementTheme::Default;
                break;
            case WindowTheme::Light:
                result_theme = Windows::UI::Xaml::ElementTheme::Light;
                break;
            case WindowTheme::Dark:
                result_theme = Windows::UI::Xaml::ElementTheme::Dark;
                break;
            default:
                throw hresult_error(E_FAIL, L"Integrity check for XamlWindow member window_theme has failed");
            }
            e.RequestedTheme(result_theme);
        }
    }
    void XamlWindow::content(Windows::UI::Xaml::UIElement const& elem) {
        m_imp->desktop_src.Content(elem);
        this->update_element_theme();
        {   // Workaround display glitches for some xaml controls
            RECT rt;
            GetClientRect(m_imp->root_hwnd, &rt);
            SetWindowPos(
                m_imp->xaml_hwnd,
                nullptr,
                -1, -1,
                rt.right + 1, rt.bottom + 1,
                SWP_SHOWWINDOW | SWP_NOZORDER
            );
            SetWindowPos(
                m_imp->xaml_hwnd,
                nullptr,
                0, 0,
                rt.right, rt.bottom,
                SWP_SHOWWINDOW | SWP_NOZORDER
            );
        }
    }
    Windows::UI::Xaml::UIElement XamlWindow::content() {
        return m_imp->desktop_src.Content();
    }
    HWND XamlWindow::host_window_handle() {
        return m_imp->root_hwnd;
    }
    void XamlWindow::is_visible(bool b) {
        ShowWindow(m_imp->root_hwnd, b ? SW_SHOW : SW_HIDE);
    }
    void XamlWindow::window_theme(WindowTheme theme) {
        // UNLIKELY TODO: Fix context menu theme color
        switch (theme) {
        case WindowTheme::FollowSystem:
            util::win32::dark_mode::update_title_bar_theme_color(m_imp->root_hwnd);
            break;
        case WindowTheme::Light:
            util::win32::dark_mode::update_title_bar_theme_color(m_imp->root_hwnd, false);
            break;
        case WindowTheme::Dark:
            util::win32::dark_mode::update_title_bar_theme_color(m_imp->root_hwnd, true);
            break;
        default:
            throw hresult_invalid_argument();
        }
        m_imp->window_theme = theme;
#if 1
        // A workaround for forcing redraw of window borders
        if (IsWindowVisible(m_imp->root_hwnd)) {
            auto force_redraw_fn = [this](HWND hwnd) {
                if (GetForegroundWindow() == hwnd) {
                    HWND hwnd_static = CreateWindowW(
                        L"Static", L"",
                        WS_VISIBLE | WS_POPUP,
                        -1, -1, 1, 1,
                        nullptr, nullptr,
                        nullptr, nullptr
                    );
                    SetFocus(hwnd_static);
                    DestroyWindow(hwnd_static);
                }
                else {
                    WINDOWPLACEMENT wp;
                    wp.length = sizeof wp;
                    GetWindowPlacement(hwnd, &wp);
                    if (wp.showCmd == SW_SHOWMINIMIZED) {
                        return;
                    }
#if 0
                    /*
                    if (wp.showCmd == SW_SHOWMAXIMIZED) {
                        ShowWindow(hwnd, SW_RESTORE);
                        ShowWindow(hwnd, SW_MAXIMIZE);
                    }
                    else {
                        ShowWindow(hwnd, SW_MAXIMIZE);
                        ShowWindow(hwnd, SW_RESTORE);
                    }*/
                    ShowWindow(hwnd, SW_HIDE);
                    ShowWindow(hwnd, SW_SHOWNA);
#else
                    // Force redraw non-client area by modifying window styles
                    RECT rt;
                    GetClientRect(m_imp->root_hwnd, &rt);
                    auto style = GetWindowLongPtrW(m_imp->root_hwnd, GWL_STYLE);
                    SetWindowLongPtrW(m_imp->root_hwnd, GWL_STYLE, WS_POPUP);
                    SetWindowLongPtrW(m_imp->root_hwnd, GWL_STYLE, style);
                    // Fix XamlIsland incorrect size after changing window styles
                    SetWindowPos(
                        m_imp->xaml_hwnd,
                        nullptr,
                        0, 0,
                        rt.right, rt.bottom,
                        SWP_SHOWWINDOW | SWP_NOZORDER
                    );
#endif
                }
            };
            force_redraw_fn(m_imp->root_hwnd);
        }
#else
        // Force redraw non-client area by modifying window styles
        RECT rt;
        GetClientRect(m_imp->root_hwnd, &rt);
        auto style = GetWindowLongPtrW(m_imp->root_hwnd, GWL_STYLE);
        SetWindowLongPtrW(m_imp->root_hwnd, GWL_STYLE, WS_POPUP);
        SetWindowLongPtrW(m_imp->root_hwnd, GWL_STYLE, style);
        // Fix XamlIsland incorrect size after changing window styles
        SetWindowPos(
            m_imp->xaml_hwnd,
            nullptr,
            0, 0,
            rt.right, rt.bottom,
            SWP_SHOWWINDOW | SWP_NOZORDER
        );
#endif
        this->update_element_theme();
    }
    WindowTheme XamlWindow::window_theme() {
        return m_imp->window_theme;
    }
    void XamlWindow::set_window_title(const wchar_t* title) {
        if (title == nullptr || *title == L'\0') {
            SetWindowTextW(m_imp->root_hwnd, m_imp->original_window_title.c_str());
            return;
        }
        SetWindowTextW(
            m_imp->root_hwnd,
            wstrprintf(L"%ls - %ls", title, m_imp->original_window_title.c_str()).c_str()
        );
    }
}
