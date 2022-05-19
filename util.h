#pragma once

#include "pch.h"

#include <string>
#include <functional>

namespace util {
    namespace misc {
#define CONCAT_2_IMPL(a, b) a ## b
#define CONCAT_2(a, b) CONCAT_2_IMPL(a, b)
#define CONCAT_3_IMPL(a, b, c) a ## b ## c
#define CONCAT_3(a, b, c) CONCAT_3_IMPL(a, b, c)

#define deferred(x) auto CONCAT_2(internal_deffered_, __COUNTER__) = ::util::misc::Defer(x)
        struct Defer {
            Defer(std::function<void(void)> pFunc) : func(std::move(pFunc)) {};
            std::function<void(void)> func;
            virtual ~Defer() {
                func();
            }
        };

        template<typename T, std::enable_if_t<std::is_enum_v<T>, int> = 0>
        constexpr std::underlying_type_t<T> enum_to_int(T const& value) {
            return static_cast<std::underlying_type_t<T>>(value);
        }
    }

    namespace str {
        std::wstring wstrprintf(_Printf_format_string_ const wchar_t* str, ...);
    }

    namespace num {
        // unsigned -> signed
        template<
            typename Out, typename In,
            std::enable_if_t<std::is_unsigned_v<In>, int> = 0,
            std::enable_if_t<std::is_signed_v<Out>, int> = 0
        >
        std::optional<Out> try_into(In v) {
            if constexpr (static_cast<uintmax_t>(std::numeric_limits<Out>::max()) > static_cast<uintmax_t>(std::numeric_limits<In>::max())) {
                // Infallible
                return static_cast<Out>(v);
            }
            else {
                if (v > static_cast<In>(std::numeric_limits<Out>::max())) {
                    return std::nullopt;
                }
                return static_cast<Out>(v);
            }
        }
        template<typename T>
        T saturating_add(T lhs, T rhs) {
            if constexpr (std::is_unsigned_v<T>) {
                if (lhs > std::numeric_limits<T>::max() - rhs) {
                    return std::numeric_limits<T>::max();
                }
                return lhs + rhs;
            }
            else {
                // Assume signed
                if (lhs == 0 || rhs == 0) {
                    return lhs + rhs;
                }
                if ((lhs < 0 && rhs > 0) || (lhs > 0 && rhs < 0)) {
                    return lhs + rhs;
                }
                if (lhs < 0) {
                    if (lhs < std::numeric_limits<T>::min() - rhs) {
                        return std::numeric_limits<T>::min();
                    }
                    return lhs + rhs;
                }
                else {
                    // Assume lhs > 0
                    if (lhs > std::numeric_limits<T>::max() - rhs) {
                        return std::numeric_limits<T>::max();
                    }
                    return lhs + rhs;
                }
            }
        }
        template<typename T>
        T saturating_mul(T lhs, T rhs) {
            if constexpr (std::is_unsigned_v<T>) {
                if (lhs > std::numeric_limits<T>::max() / rhs) {
                    return std::numeric_limits<T>::max();
                }
                return lhs * rhs;
            }
            else {
                // Assume signed
                if (lhs < 0 && rhs < 0) {
                    if (lhs < std::numeric_limits<T>::max() / rhs) {
                        return std::numeric_limits<T>::max();
                    }
                    return lhs * rhs;
                }
                else if (lhs > 0 && rhs < 0) {
                    if (lhs > std::numeric_limits<T>::min() / rhs) {
                        return std::numeric_limits<T>::min();
                    }
                    return lhs * rhs;
                }
                else if (lhs < 0 && rhs > 0) {
                    if (lhs < std::numeric_limits<T>::min() / rhs) {
                        return std::numeric_limits<T>::min();
                    }
                    return lhs * rhs;
                }
                else if (lhs > 0 && rhs > 0) {
                    if (lhs > std::numeric_limits<T>::max() / rhs) {
                        return std::numeric_limits<T>::max();
                    }
                    return lhs * rhs;
                }
                else {
                    // Assume lhs == 0 || rhs == 0
                    return 0;
                }
            }
        }
    }

    namespace fs {
        bool create_dir(const wchar_t* path);
        bool path_exists(const wchar_t* path);
        bool delete_file(const wchar_t* path);
        // NOTE: This function does not guarantee success for paths
        //       in different volumes / file systems
        bool rename_path(const wchar_t* orig_path, const wchar_t* new_path);
    }

    namespace win32 {
        namespace dark_mode {
            enum class AppDarkMode : unsigned {
                Default = 0,
                AllowDark = 1,
                ForceDark = 2,
                ForceLight = 3,
            };

            bool allow_dark_mode_for_app(AppDarkMode mode);
            bool allow_dark_mode_for_window(HWND hwnd, bool allow);
            bool update_title_bar_theme_color(HWND hwnd, bool enable_dark);
            bool update_title_bar_theme_color(HWND hwnd);
            bool is_color_scheme_change_message(UINT message, LPARAM lParam);
        }

        // Useful for dialogs & message boxes
        void set_main_window_handle(HWND hwnd);
        HWND get_main_window_handle(void);
    }

    namespace winrt {
#define co_safe_capture_val(val)                            \
    auto CONCAT_3(temp_capture_, val, __LINE__){ val };     \
    auto& val{ CONCAT_3(temp_capture_, val, __LINE__) }
#define co_safe_capture_ref(val)                            \
    auto CONCAT_3(temp_capture_, val, __LINE__){ &(val) };  \
    auto& val{ *CONCAT_3(temp_capture_, val, __LINE__) }
#define co_safe_capture(val) co_safe_capture_val(val)

        // Same as ::winrt::fire_and_forget, except that it reports unhandled exceptions
        struct fire_forget_except {};

        ::winrt::Windows::UI::Xaml::UIElement get_child_elem(
            ::winrt::Windows::UI::Xaml::UIElement const& elem,
            std::wstring_view name,
            std::wstring_view class_name = L""
        );
        ::winrt::Windows::UI::Xaml::UIElement get_child_elem(
            ::winrt::Windows::UI::Xaml::UIElement const& elem,
            int32_t idx = 0
        );

        ::winrt::Windows::Foundation::IAsyncOperation<uint64_t> calc_folder_size(::winrt::hstring path);
        ::winrt::Windows::Foundation::IAsyncOperation<bool> delete_all_inside_folder(::winrt::hstring path);
        ::winrt::Windows::Foundation::IAsyncOperation<bool> delete_folder(::winrt::hstring path);

        ::winrt::guid gen_random_guid(void);
        std::wstring to_wstring(::winrt::guid const& value);

        inline ::winrt::guid to_guid(::winrt::hstring const& s) {
            return ::winrt::guid{ s };
        }
        inline ::winrt::guid to_guid(std::string_view s) {
            return ::winrt::guid{ s };
        }
        inline ::winrt::guid to_guid(std::wstring_view s) {
            return ::winrt::guid{ s };
        }

        // Source: https://devblogs.microsoft.com/oldnewthing/20210301-00/?p=104914
        struct awaitable_event {
            void set() const noexcept {
                SetEvent(os_handle());
            }
            auto operator co_await() const noexcept {
                return ::winrt::resume_on_signal(os_handle());
            }
        private:
            HANDLE os_handle() const noexcept {
                return handle.get();
            }
            ::winrt::handle handle{ ::winrt::check_pointer(CreateEvent(nullptr, true, false, nullptr)) };
        };

        inline bool update_popups_theme(
            ::winrt::Windows::UI::Xaml::XamlRoot const& xaml_root,
            ::winrt::Windows::UI::Xaml::ElementTheme theme
        ) {
            auto ivec = ::winrt::Windows::UI::Xaml::Media::VisualTreeHelper::GetOpenPopupsForXamlRoot(
                xaml_root
            );
            for (auto&& i : ivec) {
                i.RequestedTheme(theme);
            }
        }

        inline void fix_content_dialog_theme(
            ::winrt::Windows::UI::Xaml::Controls::ContentDialog const& cd,
            ::winrt::Windows::UI::Xaml::FrameworkElement const& theme_base
        ) {
            using ::winrt::Windows::UI::Xaml::FrameworkElement;
            using ::winrt::Windows::Foundation::IInspectable;
            using ::winrt::Windows::UI::Xaml::Controls::ContentDialog;
            using ::winrt::Windows::UI::Xaml::Controls::ContentDialogOpenedEventArgs;
            // Prevent cyclic references causing resource leak
            auto revoker = theme_base.ActualThemeChanged(::winrt::auto_revoke,
                [ref = ::winrt::make_weak(cd)](FrameworkElement const& sender, IInspectable const&) {
                    if (auto cd = ref.get()) {
                        cd.RequestedTheme(sender.RequestedTheme());
                    }
                }
            );
            cd.Opened(
                [revoker = std::move(revoker), ref = ::winrt::make_weak(theme_base)]
                (ContentDialog const& sender, ContentDialogOpenedEventArgs const&) {
                    if (auto theme_base = ref.get()) {
                        sender.RequestedTheme(theme_base.ActualTheme());
                    }
                }
            );
        }

        ::winrt::Windows::UI::Xaml::Controls::ControlTemplate make_simple_button_template(void);
    }
}

// Source: https://devblogs.microsoft.com/oldnewthing/20190320-00/?p=102345
namespace std::experimental {
    template <typename... Args>
    struct coroutine_traits<::util::winrt::fire_forget_except, Args...> {
        struct promise_type {
            ::util::winrt::fire_forget_except get_return_object() const noexcept { return {}; }
            void return_void() const noexcept {}
            suspend_never initial_suspend() const noexcept { return {}; }
            suspend_never final_suspend() const noexcept { return {}; }
            void unhandled_exception() noexcept {
                try { throw; }
                catch (::winrt::hresult_error const& e) {
                    ::MessageBoxW(
                        ::util::win32::get_main_window_handle(),
                        ::util::str::wstrprintf(
                            L"HRESULT: 0x%08x: %ls",
                            static_cast<uint32_t>(e.code()),
                            e.message().c_str()
                        ).c_str(),
                        L"Unhandled Async Exception",
                        MB_ICONERROR
                    );
                }
                catch (std::exception const& e) {
                    MessageBoxW(
                        util::win32::get_main_window_handle(),
                        (L"std::exception: " + ::winrt::to_hstring(e.what())).c_str(),
                        L"Unhandled Async Exception",
                        MB_ICONERROR
                    );
                }
                catch (const wchar_t* e) {
                    MessageBoxW(
                        util::win32::get_main_window_handle(),
                        e,
                        L"Unhandled Async Exception",
                        MB_ICONERROR
                    );
                }
                catch (...) {
                    MessageBoxW(
                        util::win32::get_main_window_handle(),
                        L"Unknown exception was thrown",
                        L"Unhandled Async Exception",
                        MB_ICONERROR
                    );
                }
            }
        };
    };
}

// Preludes
using ::util::winrt::fire_forget_except;
using ::util::str::wstrprintf;
