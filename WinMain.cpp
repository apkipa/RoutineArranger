#include "pch.h"

#include "util.h"
#include "windowing.h"

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::UI;
using namespace Windows::UI::Core;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Controls;

#include "RoutineArranger_Core.h"
#include "RoutineArranger_UI.h"

int main(void) {
    if (!util::fs::create_dir(L"RoutineArrangerData")) {
        throw L"无法创建 RoutineArrangerData 文件夹";
    }

    using namespace RoutineArranger::UI;
    using namespace RoutineArranger::Core;

    auto core_app_model = RoutineArranger::make<CoreAppModel>();
    for (;;) {
        auto connect_storage_result = core_app_model->try_connect_storage(L"RoutineArrangerData");
        int msgbox_ret;
        if (connect_storage_result == RoutineArrangerResultErrorKind::Ok) {
            break;
        }
        switch (connect_storage_result) {
        case RoutineArrangerResultErrorKind::StorageNotAccessible:
            throw L"无法连接程序的存储空间: 程序没有适当的访问权限或另一个实例已经打开";
        case RoutineArrangerResultErrorKind::StorageCorrupted:
            msgbox_ret = MessageBoxW(
                nullptr,
                L"程序的存储空间已经损坏。是否重置存储空间(将丢失所有用户数据)并尝试继续?",
                L"警告",
                MB_ICONWARNING | MB_YESNO
            );
            if (msgbox_ret == IDNO) {
                throw L"无法连接程序的存储空间: 存储空间已经损坏";
            }
            util::winrt::delete_all_inside_folder(L"RoutineArrangerData").get();
            break;
        default:
            throw L"无法连接程序的存储空间: 未知错误";
        }
    }

    windowing::XamlWindow window = windowing::XamlWindow::create_simple(L"日程安排者");

    auto root_container_presenter = RoutineArranger::make<RootContainerPresenter>(core_app_model, &window);

    // Workaround NavigationView incorrectly playing animations and causing glitches
    root_container_presenter->get_root().Dispatcher().RunAsync(CoreDispatcherPriority::Normal, [&]() -> fire_forget_except {
        co_safe_capture_ref(window);
        co_safe_capture(root_container_presenter);
        using namespace std::chrono_literals;
        apartment_context ui_ctx;
        window.content(root_container_presenter->get_root());
        window.is_visible(true);
        co_await 500ms;
        co_await ui_ctx;
        window.content(root_container_presenter->get_root());
    });

    util::win32::set_main_window_handle(window.host_window_handle());
    window.run_loop_to_completion();

    core_app_model->try_flush_storage();

    return 0;
}

int wrapped_main(void) try {
    int ret = main();
    windowing::XamlWindow::unload_global_res();
    return ret;
}
catch (hresult_error const& e) {
    MessageBoxW(
        util::win32::get_main_window_handle(),
        wstrprintf(L"HRESULT: 0x%08x: %ls", static_cast<uint32_t>(e.code()), e.message().c_str()).c_str(),
        L"严重错误",
        MB_ICONERROR
    );
    return 1;
}
catch (std::exception const& e) {
    MessageBoxW(
        util::win32::get_main_window_handle(),
        (L"std::exception: " + winrt::to_hstring(e.what())).c_str(),
        L"严重错误",
        MB_ICONERROR
    );
    return 1;
}
catch (const wchar_t* e) {
    MessageBoxW(util::win32::get_main_window_handle(), e, L"严重错误", MB_ICONERROR);
    return 1;
}
catch (...) {
    MessageBoxW(util::win32::get_main_window_handle(), L"发生了未知的严重错误", L"严重错误", MB_ICONERROR);
    return 1;
}

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    // Workaround a bug where ColorPicker causes crashes
    // (See https://github.com/microsoft/microsoft-ui-xaml/issues/3541)
    // Avoid the crash by failing fast
    TerminateProcess(GetCurrentProcess(), wrapped_main());
    return 1;
}
