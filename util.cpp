#include "pch.h"

#include <Uxtheme.h>

#include "util.h"

#include <exception>
#include <cstdarg>

namespace util {
    namespace str {
        std::wstring wstrprintf(_Printf_format_string_ const wchar_t* str, ...) {
            wchar_t buf[1024];
            std::va_list arg;
            va_start(arg, str);
            int len = std::vswprintf(buf, sizeof buf / sizeof *buf, str, arg);
            va_end(arg);
            if (len < 0) {
                // Maybe the result string is too long, try to allocate a larger buffer and try again
                constexpr auto max_buf_size = static_cast<size_t>(1024) * 1024 * 4;
                std::wstring str_buf;
                // NOTE: Method reserve is not applicable, as writing to reserved space
                //       invokes undefined behavior
                str_buf.resize(max_buf_size);
                va_start(arg, str);
                len = std::vswprintf(&str_buf[0], max_buf_size, str, arg);
                va_end(arg);
                if (len < 0) {
                    throw std::bad_alloc();
                }
                str_buf.resize(len);
                return str_buf;
            }
            return std::wstring(buf, buf + len);
        }
    }

    namespace fs {
        bool create_dir(const wchar_t* path) {
            if (CreateDirectoryW(path, nullptr)) {
                return true;
            }
            return GetLastError() == ERROR_ALREADY_EXISTS;
        }
        bool path_exists(const wchar_t* path) {
            return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
        }
        bool delete_file(const wchar_t* path) {
            return DeleteFileW(path) != 0;
        }
        bool rename_path(const wchar_t* orig_path, const wchar_t* new_path) {
            return MoveFileW(orig_path, new_path);
        }
    }

    namespace win32 {
        namespace dark_mode {
            // Dark mode APIs from https://github.com/ysc3839/win32-darkmode
            namespace {
                enum IMMERSIVE_HC_CACHE_MODE {
                    IHCM_USE_CACHED_VALUE,
                    IHCM_REFRESH
                };

                // 1903 18362
                enum PreferredAppMode {
                    Default,
                    AllowDark,
                    ForceDark,
                    ForceLight,
                    Max
                };

                enum WINDOWCOMPOSITIONATTRIB {
                    WCA_UNDEFINED = 0,
                    WCA_NCRENDERING_ENABLED = 1,
                    WCA_NCRENDERING_POLICY = 2,
                    WCA_TRANSITIONS_FORCEDISABLED = 3,
                    WCA_ALLOW_NCPAINT = 4,
                    WCA_CAPTION_BUTTON_BOUNDS = 5,
                    WCA_NONCLIENT_RTL_LAYOUT = 6,
                    WCA_FORCE_ICONIC_REPRESENTATION = 7,
                    WCA_EXTENDED_FRAME_BOUNDS = 8,
                    WCA_HAS_ICONIC_BITMAP = 9,
                    WCA_THEME_ATTRIBUTES = 10,
                    WCA_NCRENDERING_EXILED = 11,
                    WCA_NCADORNMENTINFO = 12,
                    WCA_EXCLUDED_FROM_LIVEPREVIEW = 13,
                    WCA_VIDEO_OVERLAY_ACTIVE = 14,
                    WCA_FORCE_ACTIVEWINDOW_APPEARANCE = 15,
                    WCA_DISALLOW_PEEK = 16,
                    WCA_CLOAK = 17,
                    WCA_CLOAKED = 18,
                    WCA_ACCENT_POLICY = 19,
                    WCA_FREEZE_REPRESENTATION = 20,
                    WCA_EVER_UNCLOAKED = 21,
                    WCA_VISUAL_OWNER = 22,
                    WCA_HOLOGRAPHIC = 23,
                    WCA_EXCLUDED_FROM_DDA = 24,
                    WCA_PASSIVEUPDATEMODE = 25,
                    WCA_USEDARKMODECOLORS = 26,
                    WCA_LAST = 27
                };

                struct WINDOWCOMPOSITIONATTRIBDATA {
                    WINDOWCOMPOSITIONATTRIB Attrib;
                    PVOID pvData;
                    SIZE_T cbData;
                };

                using ftype_RtlGetNtVersionNumbers = void (WINAPI*)(LPDWORD major, LPDWORD minor, LPDWORD build);
                using ftype_SetWindowCompositionAttribute = BOOL(WINAPI*)(HWND hWnd, WINDOWCOMPOSITIONATTRIBDATA*);
                // 1809 17763
                using ftype_ShouldAppsUseDarkMode = bool (WINAPI*)();   // ordinal 132
                using ftype_AllowDarkModeForWindow = bool (WINAPI*)(HWND hWnd, bool allow); // ordinal 133
                using ftype_AllowDarkModeForApp = bool (WINAPI*)(bool allow);   // ordinal 135, in 1809
                using ftype_FlushMenuThemes = void (WINAPI*)(); // ordinal 136
                using ftype_RefreshImmersiveColorPolicyState = void (WINAPI*)();    // ordinal 104
                using ftype_IsDarkModeAllowedForWindow = bool (WINAPI*)(HWND hWnd); // ordinal 137
                using ftype_GetIsImmersiveColorUsingHighContrast = bool (WINAPI*)(IMMERSIVE_HC_CACHE_MODE mode); // ordinal 106
                using ftype_OpenNcThemeData = HTHEME(WINAPI*)(HWND hWnd, LPCWSTR pszClassList); // ordinal 49
                // 1903 18362
                using ftype_ShouldSystemUseDarkMode = bool (WINAPI*)(); // ordinal 138
                using ftype_SetPreferredAppMode = PreferredAppMode(WINAPI*)(PreferredAppMode appMode); // ordinal 135, in 1903
                using ftype_IsDarkModeAllowedForApp = bool (WINAPI*)(); // ordinal 139

                ftype_SetWindowCompositionAttribute fn_SetWindowCompositionAttribute = nullptr;
                ftype_ShouldAppsUseDarkMode fn_ShouldAppsUseDarkMode = nullptr;
                ftype_AllowDarkModeForWindow fn_AllowDarkModeForWindow = nullptr;
                ftype_AllowDarkModeForApp fn_AllowDarkModeForApp = nullptr;
                ftype_FlushMenuThemes fn_FlushMenuThemes = nullptr;
                ftype_RefreshImmersiveColorPolicyState fn_RefreshImmersiveColorPolicyState = nullptr;
                ftype_IsDarkModeAllowedForWindow fn_IsDarkModeAllowedForWindow = nullptr;
                ftype_GetIsImmersiveColorUsingHighContrast fn_GetIsImmersiveColorUsingHighContrast = nullptr;
                ftype_OpenNcThemeData fn_OpenNcThemeData = nullptr;
                // 1903 18362
                ftype_ShouldSystemUseDarkMode fn_ShouldSystemUseDarkMode = nullptr;
                ftype_SetPreferredAppMode fn_SetPreferredAppMode = nullptr;
            }

            bool init_dark_mode_apis(void) {
                static bool inited = [] {
                    HMODULE uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
                    if (!uxtheme) {
                        return false;
                    }
                    fn_RefreshImmersiveColorPolicyState =
                        reinterpret_cast<ftype_RefreshImmersiveColorPolicyState>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(104)));
                    if (!fn_RefreshImmersiveColorPolicyState) {
                        return false;
                    }
                    fn_GetIsImmersiveColorUsingHighContrast =
                        reinterpret_cast<ftype_GetIsImmersiveColorUsingHighContrast>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(106)));
                    if (!fn_GetIsImmersiveColorUsingHighContrast) {
                        return false;
                    }
                    fn_ShouldAppsUseDarkMode =
                        reinterpret_cast<ftype_ShouldAppsUseDarkMode>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(132)));
                    if (!fn_ShouldAppsUseDarkMode) {
                        return false;
                    }
                    fn_AllowDarkModeForWindow =
                        reinterpret_cast<ftype_AllowDarkModeForWindow>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(133)));
                    if (!fn_AllowDarkModeForWindow) {
                        return false;
                    }
                    fn_SetPreferredAppMode =
                        reinterpret_cast<ftype_SetPreferredAppMode>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(135)));
                    if (!fn_SetPreferredAppMode) {
                        return false;
                    }
                    fn_IsDarkModeAllowedForWindow =
                        reinterpret_cast<ftype_IsDarkModeAllowedForWindow>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(137)));
                    if (!fn_IsDarkModeAllowedForWindow) {
                        return false;
                    }
                    fn_SetWindowCompositionAttribute =
                        reinterpret_cast<ftype_SetWindowCompositionAttribute>
                        (GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute"));
                    if (!fn_SetWindowCompositionAttribute) {
                        return false;
                    }

                    return true;
                }();
                return inited;
            }

            bool is_high_contrast(void) {
                HIGHCONTRASTW high_contrast;
                high_contrast.cbSize = sizeof(high_contrast);
                if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(high_contrast), &high_contrast, FALSE)) {
                    return high_contrast.dwFlags & HCF_HIGHCONTRASTON;
                }
                return false;
            }

            bool allow_dark_mode_for_app(AppDarkMode mode) {
                if (!init_dark_mode_apis()) {
                    return false;
                }
                switch (mode) {
                case AppDarkMode::Default:
                    fn_SetPreferredAppMode(Default);
                    break;
                case AppDarkMode::AllowDark:
                    fn_SetPreferredAppMode(AllowDark);
                    break;
                case AppDarkMode::ForceDark:
                    fn_SetPreferredAppMode(ForceDark);
                    break;
                case AppDarkMode::ForceLight:
                    fn_SetPreferredAppMode(ForceLight);
                    break;
                default:
                    return false;
                }
                return true;
            }

            bool allow_dark_mode_for_window(HWND hwnd, bool allow) {
                if (!init_dark_mode_apis()) {
                    return false;
                }
                return fn_AllowDarkModeForWindow(hwnd, allow);
            }

            bool update_title_bar_theme_color_inner(HWND hwnd, bool enable_dark) {
                BOOL dark = enable_dark;
                WINDOWCOMPOSITIONATTRIBDATA data = { WCA_USEDARKMODECOLORS, &dark, sizeof(dark) };
                return fn_SetWindowCompositionAttribute(hwnd, &data);
            }
            bool update_title_bar_theme_color(HWND hwnd, bool enable_dark) {
                if (!init_dark_mode_apis()) {
                    return false;
                }
                return update_title_bar_theme_color_inner(hwnd, enable_dark);
            }
            bool update_title_bar_theme_color(HWND hwnd) {
                if (!init_dark_mode_apis()) {
                    return false;
                }
                return update_title_bar_theme_color_inner(
                    hwnd,
                    fn_IsDarkModeAllowedForWindow(hwnd) && fn_ShouldAppsUseDarkMode() && !is_high_contrast()
                );
            }

            bool is_color_scheme_change_message(LPARAM lParam) {
                if (!init_dark_mode_apis()) {
                    return false;
                }
                bool is = false;
                if (lParam && CompareStringOrdinal(reinterpret_cast<LPCWCH>(lParam), -1, L"ImmersiveColorSet", -1, TRUE) == CSTR_EQUAL) {
                    fn_RefreshImmersiveColorPolicyState();
                    is = true;
                }
                fn_GetIsImmersiveColorUsingHighContrast(IHCM_REFRESH);
                return is;
            }
            bool is_color_scheme_change_message(UINT message, LPARAM lParam) {
                if (!init_dark_mode_apis()) {
                    return false;
                }
                if (message == WM_SETTINGCHANGE)
                    return is_color_scheme_change_message(lParam);
                return false;
            }
        }

        static HWND main_window_handle = nullptr;
        void set_main_window_handle(HWND hwnd) {
            main_window_handle = hwnd;
        }
        HWND get_main_window_handle(void) {
            return main_window_handle;
        }
    }

    namespace winrt {
        ::winrt::Windows::UI::Xaml::UIElement get_child_elem(
            ::winrt::Windows::UI::Xaml::UIElement const& elem,
            std::wstring_view name,
            std::wstring_view class_name
        ) {
            using namespace ::winrt::Windows::UI::Xaml;
            using namespace ::winrt::Windows::UI::Xaml::Media;
            int32_t child_elem_cnt;
            if (!elem) {
                return nullptr;
            }
            child_elem_cnt = VisualTreeHelper::GetChildrenCount(elem);
            for (int32_t i = 0; i < child_elem_cnt; i++) {
                bool is_name_match = false, is_class_match = false;
                UIElement cur_elem = VisualTreeHelper::GetChild(elem, i).as<UIElement>();

                if (name != L"") {
                    if (auto fe = cur_elem.try_as<FrameworkElement>()) {
                        is_name_match = (fe.Name() == name);
                    }
                }
                else {
                    is_name_match = true;
                }
                if (class_name != L"") {
                    is_class_match = (::winrt::get_class_name(cur_elem) == class_name);
                }
                else {
                    is_class_match = true;
                }

                if (is_name_match && is_class_match) {
                    return cur_elem;
                }
            }
            return nullptr;
        }
        ::winrt::Windows::UI::Xaml::UIElement get_child_elem(
            ::winrt::Windows::UI::Xaml::UIElement const& elem,
            int32_t idx
        ) {
            using namespace ::winrt::Windows::UI::Xaml;
            using namespace ::winrt::Windows::UI::Xaml::Media;
            if (!elem) {
                return nullptr;
            }
            return VisualTreeHelper::GetChild(elem, idx).as<UIElement>();
        }

        // Assuming path is always valid and not longer than MAX_PATH; path must represent a folder
        uint64_t calc_folder_size_inner(const wchar_t* path) noexcept {
            wchar_t buf[MAX_PATH * 2 + 5];
            wcscpy(buf, path);
            wcscat(buf, L"\\*");

            // Just use the good old Win32 APIs
            WIN32_FIND_DATAW find_data;
            auto find_handle = FindFirstFileW(buf, &find_data);
            if (find_handle == INVALID_HANDLE_VALUE) {
                return 0;
            }

            uint64_t size = 0;
            do {
                if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    if (wcscmp(find_data.cFileName, L".") == 0 || wcscmp(find_data.cFileName, L"..") == 0) {
                        continue;
                    }
                    wcscpy(buf, path);
                    wcscat(buf, L"\\");
                    wcscat(buf, find_data.cFileName);
                    size += calc_folder_size_inner(buf);
                }
                else {
                    size += (static_cast<uint64_t>(find_data.nFileSizeHigh) << 32) | find_data.nFileSizeLow;
                }
            } while (FindNextFileW(find_handle, &find_data) != 0);

            FindClose(find_handle);

            return size;
        }

        ::winrt::Windows::Foundation::IAsyncOperation<uint64_t> calc_folder_size(::winrt::hstring path) {
            if (path == L"") {
                co_return 0;
            }
            // Don't block current thread
            co_await ::winrt::resume_background();
            co_return calc_folder_size_inner(path.c_str());
        }

        bool delete_file_inner(const wchar_t* path) noexcept;
        bool delete_folder_inner(const wchar_t* path) noexcept;

        bool delete_file_inner(const wchar_t* path) noexcept {
            // Just use the good old Win32 APIs
            return DeleteFileW(path) != 0;
        }

        bool delete_all_inside_folder_inner(const wchar_t* path) noexcept {
            wchar_t buf[MAX_PATH * 2 + 5];
            wcscpy(buf, path);
            wcscat(buf, L"\\*");

            // Just use the good old Win32 APIs
            WIN32_FIND_DATAW find_data;
            auto find_handle = FindFirstFileW(buf, &find_data);
            if (find_handle == INVALID_HANDLE_VALUE) {
                // If no matching files can be found, assume that operation has succeeded
                return GetLastError() == ERROR_FILE_NOT_FOUND;
            }

            bool succeeded = true;
            do {
                if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    if (wcscmp(find_data.cFileName, L".") == 0 || wcscmp(find_data.cFileName, L"..") == 0) {
                        continue;
                    }
                    wcscpy(buf, path);
                    wcscat(buf, L"\\");
                    wcscat(buf, find_data.cFileName);
                    if (!delete_folder_inner(buf)) {
                        succeeded = false;
                    }
                }
                else {
                    wcscpy(buf, path);
                    wcscat(buf, L"\\");
                    wcscat(buf, find_data.cFileName);
                    if (!delete_file_inner(buf)) {
                        succeeded = false;
                    }
                }
            } while (FindNextFileW(find_handle, &find_data) != 0);

            FindClose(find_handle);

            return succeeded;
        }

        bool delete_folder_inner(const wchar_t* path) noexcept {
            return delete_all_inside_folder_inner(path) && RemoveDirectoryW(path) != 0;
        }

        ::winrt::Windows::Foundation::IAsyncOperation<bool> delete_all_inside_folder(::winrt::hstring path) {
            if (path == L"") {
                co_return false;
            }
            // Don't block current thread
            co_await ::winrt::resume_background();
            co_return delete_all_inside_folder_inner(path.c_str());
        }

        ::winrt::Windows::Foundation::IAsyncOperation<bool> delete_folder(::winrt::hstring path) {
            if (path == L"") {
                co_return false;
            }
            // Don't block current thread
            co_await ::winrt::resume_background();
            co_return delete_folder_inner(path.c_str());
        }

        ::winrt::guid gen_random_guid(void) {
            return ::winrt::Windows::Foundation::GuidHelper::CreateNewGuid();
        }
        std::wstring to_wstring(::winrt::guid const& value) {
            wchar_t buffer[40];
            swprintf_s(buffer, L"%08x-%04hx-%04hx-%02hhx%02hhx-%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx",
                value.Data1, value.Data2, value.Data3, value.Data4[0], value.Data4[1],
                value.Data4[2], value.Data4[3], value.Data4[4], value.Data4[5], value.Data4[6], value.Data4[7]
            );
            return buffer;
        }
        ::winrt::Windows::UI::Xaml::Controls::ControlTemplate make_simple_button_template(void) {
            return ::winrt::Windows::UI::Xaml::Markup::XamlReader::Load(LR"(
<ControlTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
                 xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
                 TargetType="Button">
    <Grid x:Name="RootGrid" Background="{TemplateBinding Background}" CornerRadius="{TemplateBinding CornerRadius}">
        <VisualStateManager.VisualStateGroups>
            <VisualStateGroup x:Name="CommonStates">
                <VisualState x:Name="Normal"/>
                <VisualState x:Name="PointerOver">
                    <VisualState.Setters>
                        <Setter Target="RootGridBackground.Background" Value="{ThemeResource AppBarButtonBackgroundPointerOver}"/>
                        <Setter Target="ContentPresenter.Foreground" Value="{ThemeResource AppBarButtonForegroundPointerOver}"/>
                    </VisualState.Setters>
                </VisualState>
                <VisualState x:Name="Pressed">
                    <VisualState.Setters>
                        <Setter Target="RootGridBackground.Background" Value="{ThemeResource AppBarButtonBackgroundPressed}"/>
                        <Setter Target="ContentPresenter.Foreground" Value="{ThemeResource AppBarButtonForegroundPressed}"/>
                    </VisualState.Setters>
                </VisualState>
                <VisualState x:Name="Disabled">
                    <VisualState.Setters>
                        <Setter Target="RootGridBackground.Background" Value="{ThemeResource AppBarButtonBackgroundDisabled}"/>
                        <Setter Target="ContentPresenter.Foreground" Value="{ThemeResource AppBarButtonForegroundDisabled}"/>
                    </VisualState.Setters>
                </VisualState>
            </VisualStateGroup>
        </VisualStateManager.VisualStateGroups>
        <Grid x:Name="RootGridBackground" Background="{ThemeResource AppBarButtonBackground}"/>
        <ContentPresenter x:Name="ContentPresenter" BorderBrush="{TemplateBinding BorderBrush}" BorderThickness="{TemplateBinding BorderThickness}" CornerRadius="{TemplateBinding CornerRadius}" Content="{TemplateBinding Content}" ContentTransitions="{TemplateBinding ContentTransitions}" ContentTemplate="{TemplateBinding ContentTemplate}" Padding="{TemplateBinding Padding}" HorizontalContentAlignment="{TemplateBinding HorizontalContentAlignment}" VerticalContentAlignment="{TemplateBinding VerticalContentAlignment}" AutomationProperties.AccessibilityView="Raw"/>
    </Grid>
</ControlTemplate>)").as<::winrt::Windows::UI::Xaml::Controls::ControlTemplate>();
        }
    }
}