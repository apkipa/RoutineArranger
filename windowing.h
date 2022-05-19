#pragma once

namespace windowing {
    enum class WindowTheme {
        FollowSystem,
        Light,
        Dark,
    };

    // TODO: Add Dispatcher for XamlWindow?
    struct XamlWindowImpDetails;
    class XamlWindow {
    public:
        XamlWindow() = delete;
        ~XamlWindow();
        XamlWindow(XamlWindow const&) = delete;
        XamlWindow(XamlWindow&&) noexcept;
        XamlWindow& operator=(XamlWindow const&) = delete;

        // NOTE: Assuming these APIs are only called in one thread
        static bool get_is_global_res_inited(void);
        static XamlWindow create_simple(const wchar_t* title);
        static bool unload_global_res(void);

        void run_loop_to_completion(void);

        void content(winrt::Windows::UI::Xaml::UIElement const& elem);
        winrt::Windows::UI::Xaml::UIElement content();

        HWND host_window_handle();

        void is_visible(bool b);

        void window_theme(WindowTheme theme);
        WindowTheme window_theme();

        // NOTE: This sets the sub title based on the title for creation
        void set_window_title(const wchar_t* title);
    private:
        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
        XamlWindow(std::nullptr_t) noexcept;

        void update_element_theme(void);

        XamlWindowImpDetails* m_imp;
    };
}
