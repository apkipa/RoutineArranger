#include "pch.h"

#include "RoutineArranger_UI.h"
#include "util.h"
#include <cinttypes>
#include <cwctype>
#include <sstream>
#include <cerrno>

// TODO: TextBox can copy & paste texts with foreground within the app,
//       which is undesired behavior. Try overriding this behavior.

using namespace winrt;

const uint64_t SECS_PER_MINUTE = 60;
const uint64_t SECS_PER_HOUR = SECS_PER_MINUTE * 60;
const uint64_t SECS_PER_DAY = SECS_PER_HOUR * 24;

template<typename T>
auto time_point_to_tm(T const& time_point) {
    std::time_t t = T::clock::to_time_t(time_point);
    return *std::localtime(&t);
}
template<typename T>
auto duration_to_secs(T const& duration) {
    return std::chrono::duration_cast<std::chrono::seconds>(duration).count();
}
template<typename T>
auto time_point_to_secs_since_epoch(T const& time_point) {
    //return std::chrono::duration_cast<std::chrono::seconds>(time_point.time_since_epoch()).count();
    return duration_to_secs(time_point.time_since_epoch());
}
bool is_tm_in_normal_range(std::tm const& v) {
    std::tm copy = v;
    std::mktime(&copy);
    return v.tm_year == copy.tm_year
        && v.tm_mon == copy.tm_mon
        && v.tm_mday == copy.tm_mday
        && v.tm_hour == copy.tm_hour
        && v.tm_min == copy.tm_min
        && v.tm_sec == copy.tm_sec;
}
// NOTE: Returns Local - UTC
std::chrono::minutes get_current_time_zone_offset(void) {
    using namespace std::chrono_literals;
    TIME_ZONE_INFORMATION tzi;
    if (GetTimeZoneInformation(&tzi) == TIME_ZONE_ID_INVALID) {
        return 0min;
    }
    return -tzi.Bias * 1min;
}

constexpr uint32_t winrt_color_to_u32(Windows::UI::Color clr) {
    uint32_t result = static_cast<uint32_t>(clr.A) << 24;
    result |= static_cast<uint32_t>(clr.R) << 16;
    result |= static_cast<uint32_t>(clr.G) << 8;
    result |= clr.B;
    return result;
}
constexpr Windows::UI::Color u32_color_to_winrt(uint32_t clr) {
    // This line cannot result in a constexpr, so it is commented out
    //return Windows::UI::ColorHelper::FromArgb(clr >> 24, clr >> 16, clr >> 8, clr);
    // Assuming order is ARGB
    return {
        static_cast<uint8_t>(clr >> 24),
        static_cast<uint8_t>(clr >> 16),
        static_cast<uint8_t>(clr >> 8),
        static_cast<uint8_t>(clr)
    };
}

auto show_simple_content_dialog(
    Windows::UI::Xaml::FrameworkElement const& root_elem,
    Windows::Foundation::IInspectable const& title,
    Windows::Foundation::IInspectable const& content
) {
    Windows::UI::Xaml::Controls::ContentDialog cd;
    cd.XamlRoot(root_elem.XamlRoot());
    cd.Title(title);
    cd.Content(content);
    cd.CloseButtonText(L"关闭");
    util::winrt::fix_content_dialog_theme(cd, root_elem);
    return cd.ShowAsync();
}

// Assuming result string is short enough
std::wstring wstrftime(const wchar_t* fmt, const std::tm* time) {
    wchar_t buf[128];
    if (std::wcsftime(buf, sizeof buf / sizeof *buf, fmt, time) == 0) {
        throw std::invalid_argument("Time string is too long");
    }
    return buf;
}

std::optional<uint64_t> parse_digits_strict(std::wstring_view str) {
    // Radix is 10
    if (str == L"" || !std::all_of(str.begin(), str.end(), std::iswdigit)) {
        return std::nullopt;
    }
    uint64_t result = 0;
    for (auto ch : str) {
        if (result > std::numeric_limits<uint64_t>::max() / 10) {
            // Overflow
            return std::nullopt;
        }
        result *= 10;
        if (result > std::numeric_limits<uint64_t>::max() - (ch - L'0')) {
            // Overflow
            return std::nullopt;
        }
        result += ch - L'0';
    }
    return result;
}

// Compliant with https://www.w3.org/TR/WCAG20
Windows::UI::Color get_text_color_from_background(Windows::UI::Color background) {
    // NOTE: Alpha values are ignored
    using Windows::UI::Colors;
    auto transform_fn = [](uint8_t c) {
        double fc = c / 255.0;
        return fc <= 0.03928 ? fc / 12.92 : std::pow((fc + 0.055) / 1.055, 2.4);
    };
    double R = transform_fn(background.R);
    double G = transform_fn(background.G);
    double B = transform_fn(background.B);
    double L = 0.2126 * R + 0.7152 * G + 0.0722 * B;
    return (L + 0.05) / (0.0 + 0.05) > (1.0 + 0.05) / (L + 0.05) ? Colors::Black() : Colors::White();
}

namespace RoutineArranger::UI::implementation {
    using namespace Windows::UI;
    using namespace Windows::UI::Text;
    using namespace Windows::UI::Xaml;
    using namespace Windows::UI::Xaml::Media;
    using namespace Windows::UI::Xaml::Input;
    using namespace Windows::UI::Xaml::Controls;
    using namespace Windows::UI::Xaml::Controls::Primitives;

    // Designed for code in implementation namespace
    template<typename T, typename... Types>
    std::shared_ptr<T> make(Types&&... args) {
        return std::make_shared<T>(std::forward<Types>(args)...);
    }

    StackedContentControlHelper::StackedContentControlHelper(bool enable_transition) :
        m_root_ctrl(ContentControl()), m_nav_stack(), m_enable_transition(enable_transition), m_resize_removed_transition(false)
    {
        m_root_ctrl.VerticalContentAlignment(VerticalAlignment::Stretch);
        m_root_ctrl.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        if (enable_transition) {
            m_root_ctrl.ContentTransitions().Append(Windows::UI::Xaml::Media::Animation::ContentThemeTransition());
        }
        m_root_ctrl.Content(nullptr);
        m_event_revoker = m_root_ctrl.SizeChanged(auto_revoke, [this](IInspectable const&, SizeChangedEventArgs const&) {
            if (m_enable_transition) {
                if (!m_resize_removed_transition) {
                    m_resize_removed_transition = true;
                    m_root_ctrl.ContentTransitions().Clear();
                }
            }
        });
    }
    UIElement StackedContentControlHelper::get_root() {
        return m_root_ctrl;
    }
    void StackedContentControlHelper::navigate_to(IInspectable const& ins, bool clear_stack) {
        if (clear_stack) {
            m_nav_stack.clear();
        }
        else {
            if (IInspectable cont = m_root_ctrl.Content()) {
                m_nav_stack.push_back(cont);
            }
        }
        if (m_enable_transition) {
            if (m_resize_removed_transition) {
                m_resize_removed_transition = false;
                m_root_ctrl.ContentTransitions().Append(Windows::UI::Xaml::Media::Animation::ContentThemeTransition());
            }
        }
        m_root_ctrl.Content(ins);
    }
    bool StackedContentControlHelper::go_back(void) {
        if (m_nav_stack.empty()) {
            return false;
        }
        if (m_enable_transition) {
            if (m_resize_removed_transition) {
                m_resize_removed_transition = false;
                m_root_ctrl.ContentTransitions().Append(Windows::UI::Xaml::Media::Animation::ContentThemeTransition());
            }
        }
        m_root_ctrl.Content(m_nav_stack.back());
        m_nav_stack.pop_back();
        return true;
    }
    bool StackedContentControlHelper::can_go_back(void) {
        return !m_nav_stack.empty();
    }
    bool StackedContentControlHelper::enable_transition(bool new_value) {
        bool old_value = m_enable_transition;
        m_enable_transition = new_value;
        if (old_value != new_value) {
            if (new_value) {
                m_root_ctrl.ContentTransitions().Append(Windows::UI::Xaml::Media::Animation::ContentThemeTransition());
                m_resize_removed_transition = false;
            }
            else {
                m_root_ctrl.ContentTransitions().Clear();
            }
        }
        return old_value;
    }

    UserAccountManageContainerPresenter::UserAccountManageContainerPresenter(RoutineArranger::Core::CoreAppModel model) :
        m_root_grid(Grid()), m_lv_accounts(ListView()), m_sp_adduser(StackPanel()),
        m_abtn_back(AppBarButton()), m_abtn_close(AppBarButton()),
        m_tb_adduser_name(TextBox()), m_tb_adduser_nickname(TextBox()),
        m_cb_adduser_admin(CheckBox()),
        m_model(model)
    {
        auto container_grid = Grid();
        container_grid.MaxWidth(300);
        container_grid.Margin(ThicknessHelper::FromLengths(10, 30, 10, 30));
        auto grid_rd1 = RowDefinition();
        grid_rd1.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Auto));
        container_grid.RowDefinitions().Append(grid_rd1);
        auto grid_rd2 = RowDefinition();
        grid_rd2.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        container_grid.RowDefinitions().Append(grid_rd2);
        // Make container responsive
        m_root_grid.SizeChanged([=](IInspectable const&, SizeChangedEventArgs const& e) {
            if (container_grid.DesiredSize().Height < e.NewSize().Height) {
                container_grid.VerticalAlignment(VerticalAlignment::Center);
            }
            else {
                container_grid.VerticalAlignment(VerticalAlignment::Stretch);
            }
        });

        auto sp_heading = StackPanel();
        Grid::SetRow(sp_heading, 0);
        sp_heading.Orientation(Orientation::Vertical);
        sp_heading.HorizontalAlignment(HorizontalAlignment::Center);
        sp_heading.VerticalAlignment(VerticalAlignment::Center);
        auto tb_welcome = TextBlock();
        tb_welcome.HorizontalAlignment(HorizontalAlignment::Center);
        tb_welcome.Text(L"欢迎");
        tb_welcome.FontSize(30);
        tb_welcome.Margin(ThicknessHelper::FromLengths(0, 0, 0, 10));
        sp_heading.Children().Append(tb_welcome);
        auto tb_welcome_sub = TextBlock();
        tb_welcome_sub.HorizontalAlignment(HorizontalAlignment::Center);
        tb_welcome_sub.Text(L"选择或创建一个账号以开始...");
        tb_welcome_sub.FontSize(15);
        tb_welcome_sub.Margin(ThicknessHelper::FromLengths(0, 0, 0, 10));
        sp_heading.Children().Append(tb_welcome_sub);
        Grid::SetRow(sp_heading, 0);
        container_grid.Children().Append(sp_heading);

        // Users list
        m_lv_accounts.HorizontalAlignment(HorizontalAlignment::Center);
        m_lv_accounts.IsItemClickEnabled(true);
        m_lv_accounts.SelectionMode(ListViewSelectionMode::None);
        Grid::SetRow(m_lv_accounts, 1);
        container_grid.Children().Append(m_lv_accounts);

        // Add-user panel
        m_tb_adduser_name.HorizontalAlignment(HorizontalAlignment::Stretch);
        m_tb_adduser_name.PlaceholderText(L"用户名");
        m_sp_adduser.Children().Append(m_tb_adduser_name);
        m_tb_adduser_nickname.HorizontalAlignment(HorizontalAlignment::Stretch);
        m_tb_adduser_nickname.PlaceholderText(L"昵称");
        m_sp_adduser.Children().Append(m_tb_adduser_nickname);
        m_cb_adduser_admin.Content(box_value(L"创建为管理员账号"));
        m_sp_adduser.Children().Append(m_cb_adduser_admin);
        auto btn_create = Button();
        btn_create.Content(box_value(L"创建"));
        btn_create.HorizontalAlignment(HorizontalAlignment::Right);
        btn_create.Click([this](IInspectable const&, RoutedEventArgs const&) {
            bool succeeded = m_model->create_user(
                m_tb_adduser_name.Text().c_str(),
                m_tb_adduser_nickname.Text().c_str(),
                unbox_value<bool>(m_cb_adduser_admin.IsChecked())
            );
            if (!succeeded) {
                show_simple_content_dialog(
                    m_root_grid,
                    box_value(L"错误"),
                    box_value(L"输入无效。请重试。")
                );
                return;
            }

            m_lv_accounts.Visibility(Visibility::Visible);
            m_sp_adduser.Visibility(Visibility::Collapsed);
            m_abtn_back.Visibility(Visibility::Collapsed);
            m_tb_adduser_name.Text(L"");
            m_tb_adduser_nickname.Text(L"");
            m_cb_adduser_admin.IsChecked(false);

            this->update_users_list();
        });
        m_sp_adduser.Children().Append(btn_create);
        Grid::SetRow(m_sp_adduser, 1);
        container_grid.Children().Append(m_sp_adduser);

        m_root_grid.Children().Append(container_grid);

        auto si_back = SymbolIcon();
        si_back.Symbol(Symbol::Back);
        m_abtn_back.Width(40);
        m_abtn_back.Height(40);
        m_abtn_back.Icon(si_back);
        m_abtn_back.HorizontalAlignment(HorizontalAlignment::Left);
        m_abtn_back.VerticalAlignment(VerticalAlignment::Top);
        m_abtn_back.Click([this](IInspectable const&, RoutedEventArgs const&) {
            m_lv_accounts.Visibility(Visibility::Visible);
            m_sp_adduser.Visibility(Visibility::Collapsed);
            m_abtn_back.Visibility(Visibility::Collapsed);
            m_tb_adduser_name.Text(L"");
            m_tb_adduser_nickname.Text(L"");
            m_cb_adduser_admin.IsChecked(false);
        });
        m_root_grid.Children().Append(m_abtn_back);
        auto si_close = SymbolIcon();
        si_close.Symbol(Symbol::Cancel);
        m_abtn_close.Width(40);
        m_abtn_close.Height(40);
        m_abtn_close.Icon(si_close);
        m_abtn_close.HorizontalAlignment(HorizontalAlignment::Right);
        m_abtn_close.VerticalAlignment(VerticalAlignment::Top);
        m_root_grid.Children().Append(m_abtn_close);

        m_root_grid.Visibility(Visibility::Collapsed);
    }
    IAsyncOperation<::winrt::guid> UserAccountManageContainerPresenter::start(bool user_cancellable) {
        apartment_context ui_ctx;

        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();

        deferred([&] {
            m_root_grid.Visibility(Visibility::Collapsed);
            m_lv_accounts.Items().Clear();
        });

        m_lv_accounts.Visibility(Visibility::Visible);
        m_sp_adduser.Visibility(Visibility::Collapsed);
        m_abtn_back.Visibility(Visibility::Collapsed);
        m_abtn_close.Visibility(user_cancellable ? Visibility::Visible : Visibility::Collapsed);
        m_root_grid.Visibility(Visibility::Visible);

        ::winrt::guid result_guid{ GUID{} };

        util::winrt::awaitable_event waker;

        // Update users list
        this->update_users_list();

        auto revoker_lv = m_lv_accounts.ItemClick(auto_revoke, [&](IInspectable const& sender, ItemClickEventArgs const& e) {
            uint32_t index;
            if (!sender.as<ListView>().Items().IndexOf(e.ClickedItem(), index)) {
                // Should NEVER happen
                throw hresult_error(E_FAIL, L"用户点击了账号列表中的越界项");
            }
            if (index >= m_model->get_users().size()) {
                // Create new account
                m_lv_accounts.Visibility(Visibility::Collapsed);
                m_sp_adduser.Visibility(Visibility::Visible);
                m_abtn_back.Visibility(Visibility::Visible);
            }
            else {
                auto const& cur_user = m_model->get_users()[index];
                auto on_success_fn = [&] {
                    result_guid = cur_user.id;
                    waker.set();
                };
                // Verify user identity if required
                if (cur_user.preferences.verify_identity_before_login) {
                    [](auto copied_this, auto on_success_fn) -> fire_forget_except {
                        using namespace Windows::Security::Credentials::UI;
                        const wchar_t* str = L"当前账号设置要求验证你的身份以继续。";
                        // TODO: Maybe use winrt::copy_to_abi to copy hstring to HSTRING?
                        HSTRING_HEADER hstr_hdr;
                        HSTRING hstr;
                        winrt::check_hresult(WindowsCreateStringReference(
                            str, static_cast<UINT32>(wcslen(str)),
                            &hstr_hdr, &hstr
                        ));
                        auto result = co_await winrt::capture<IAsyncOperation<UserConsentVerificationResult>>(
                            get_activation_factory<UserConsentVerifier, IUserConsentVerifierInterop>(),
                            &IUserConsentVerifierInterop::RequestVerificationForWindowAsync,
                            util::win32::get_main_window_handle(),
                            hstr
                        );
                        switch (result) {
                        case UserConsentVerificationResult::Verified:
                            // Verified
                            on_success_fn();
                            return;
                        case UserConsentVerificationResult::Canceled:
                            break;
                        default:
                            // Unable to verify
                            show_simple_content_dialog(
                                copied_this->m_root_grid,
                                box_value(L"错误"),
                                box_value(L"身份验证失败。")
                            );
                            break;
                        }
                    }(this, std::move(on_success_fn));
                }
                else {
                    // Return selected account
                    on_success_fn();
                }
            }
        });
        auto revoker_btn = m_abtn_close.Click([&](IInspectable const&, RoutedEventArgs const&) {
            m_tb_adduser_name.Text(L"");
            m_tb_adduser_nickname.Text(L"");
            m_cb_adduser_admin.IsChecked(false);
            waker.set();
        });

        co_await waker;
        // Required for deferred
        co_await ui_ctx;

        co_return result_guid;
    }
    void UserAccountManageContainerPresenter::update_users_list(void) {
        m_lv_accounts.Items().Clear();
        auto add_item_fn = [this](UIElement const& heading, std::wstring_view str) {
            auto sp = StackPanel();
            sp.Orientation(Orientation::Horizontal);
            sp.Children().Append(heading);
            auto tb = TextBlock();
            tb.Text(str);
            tb.VerticalAlignment(VerticalAlignment::Center);
            tb.TextLineBounds(TextLineBounds::Tight);
            sp.Children().Append(tb);
            m_lv_accounts.Items().Append(sp);
        };
        for (auto const& i : m_model->get_users()) {
            auto pp = PersonPicture();
            pp.VerticalAlignment(VerticalAlignment::Center);
            // In XAML: NaN => Auto
            pp.Width(std::numeric_limits<double>::quiet_NaN());
            pp.Height(20);
            pp.Margin(ThicknessHelper::FromLengths(0, 0, 10, 0));
            add_item_fn(pp, i.nickname);
        }
        {
            auto tb = TextBlock();
            tb.FontFamily(FontFamily(L"Segoe MDL2 Assets"));
            tb.Text(L"\xE710");
            tb.VerticalAlignment(VerticalAlignment::Center);
            tb.TextLineBounds(TextLineBounds::Tight);
            tb.FontSize(15);
            tb.Margin(ThicknessHelper::FromLengths(0, 0, 10, 0));
            add_item_fn(tb, L"新建账号...");
        }
    }
    UIElement UserAccountManageContainerPresenter::get_root() {
        return m_root_grid;
    }

    DayViewContainerPresenter::DayViewContainerPresenter(RootContainerPresenter* root) :
        m_root_splitview(SplitView()), m_grid_day(Grid()), m_tb_day_top(TextBlock()), m_lv_routines(ListView()),
        m_tb_routines_placeholder(TextBlock()), m_tb_day_bottom_name(TextBox()),
        m_grid_details_root(Grid()), m_sv_details(ScrollViewer()), m_cb_details_ended(CheckBox()),
        m_tb_details_title(TextBox()), m_border_details_info(Border()), m_tb_details_info(TextBlock()),
        m_tp_details_start(TimePicker()), m_tp_details_end(TimePicker()),
        m_btn_details_color_selection(nullptr), m_border_clr_details_color_selection(Border()),
        m_tb_details_color_selection(TextBlock()), m_border_details_repeating(nullptr),
        m_cb_details_repeat_type(ComboBox()), m_grid_details_repeat_by_day(Grid()),
        m_tb_details_repeat_by_day_days(TextBox()), m_tb_details_repeat_by_day_cycles(TextBox()),
        m_sp_details_repeat_by_day_sel_flags(StackPanel()),
        m_tb_details_description(nullptr), m_btn_details_delete(Button()),
        m_tb_details_placeholder(TextBlock()),
        m_rvk_tb_details_title_losing_focus(), m_rvk_tb_details_description_losing_focus(),
        m_rvk_tb_details_repeat_by_day_days_losing_focus(), m_rvk_tb_details_repeat_by_day_cycles_losing_focus(),
        m_root_pre(root),
        m_cur_time(), m_day_offset(0), m_cur_routines(), m_cur_editing_color(Colors::White()),
        m_cur_editing_time_start(nullptr), m_cur_editing_time_end(nullptr)
    {
        auto show_error_fn = [this](::winrt::hstring const& content) {
            return show_simple_content_dialog(
                m_root_splitview,
                box_value(L"错误"),
                box_value(content)
            );
        };
        auto routine_from_input_or_report_fn = [=](RoutineArranger::Core::RoutineDesc& routine) {
            using namespace std::chrono_literals;

            auto routine_name = m_tb_day_bottom_name.Text();
            if (routine_name == L"") {
                show_error_fn(L"日程名称不能为空。");
                return false;
            }
            if (!m_cur_editing_time_start) {
                show_error_fn(L"日程起始时间不能为空。");
                return false;
            }
            if (!m_cur_editing_time_end) {
                show_error_fn(L"日程结束时间不能为空。");
                return false;
            }
            auto duration_secs = duration_to_secs(m_cur_editing_time_end.Value() - m_cur_editing_time_start.Value());
            if (duration_secs <= 0) {
                show_error_fn(L"日程时间范围无效。");
                return false;
            }

            RoutineArranger::Core::RoutineDesc rt_temp;
            rt_temp.name = routine_name;
            auto cur_time = time_point_to_secs_since_epoch(m_cur_time + m_day_offset * 24h + m_cur_tz_offset);
            cur_time = (cur_time / SECS_PER_DAY) * SECS_PER_DAY;
            rt_temp.start_secs_since_epoch =
                (cur_time - duration_to_secs(m_cur_tz_offset))
                + duration_to_secs(m_cur_editing_time_start.Value());
            rt_temp.duration_secs = duration_secs;
            rt_temp.template_options = nullptr;
            rt_temp.description = L"";
            rt_temp.end_trigger_kind = RoutineArranger::Core::RoutineEndTriggerKind::Manual;
            rt_temp.id = util::winrt::gen_random_guid();
            rt_temp.is_ended = false;
            rt_temp.is_ghost = false;
            rt_temp.color = winrt_color_to_u32(m_cur_editing_color);
            routine = rt_temp;

            return true;
        };
        auto clear_routine_input_fn = [this] {
            m_tb_day_bottom_name.Text(L"");
            m_cur_editing_time_start = nullptr;
            m_cur_editing_time_end = nullptr;
            //m_cur_editing_color = Colors::White();
        };

        const auto clock_ident_24h = Windows::Globalization::ClockIdentifiers::TwentyFourHour();

        m_root_splitview.DisplayMode(SplitViewDisplayMode::Inline);
        m_root_splitview.IsPaneOpen(true);
        m_root_splitview.OpenPaneLength(400);
        m_root_splitview.PanePlacement(SplitViewPanePlacement::Right);
        // ----- SplitView.Pane Begin
        m_sv_details.IsScrollInertiaEnabled(true);
        m_sv_details.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        m_sv_details.VerticalScrollMode(ScrollMode::Auto);
        m_sv_details.HorizontalScrollMode(ScrollMode::Disabled);
        auto sp_details = StackPanel();
        sp_details.Padding(ThicknessHelper::FromLengths(7, 5, 7, 5));
        // Region: CheckBox + Title
        auto grid_details_row1 = Grid();
        auto grid_details_row1_cd1 = ColumnDefinition();
        grid_details_row1_cd1.Width(GridLengthHelper::FromPixels(30));
        grid_details_row1.ColumnDefinitions().Append(grid_details_row1_cd1);
        auto grid_details_row1_cd2 = ColumnDefinition();
        grid_details_row1_cd2.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        grid_details_row1.ColumnDefinitions().Append(grid_details_row1_cd2);
        m_cb_details_ended.Margin(ThicknessHelper::FromLengths(4, 0, 0, 0));
        // NOTE: Special handling of CheckBox to avoid redundant updates
        auto cb_handler_gen_fn = [=](bool target_value) {
            return [=](IInspectable const&, RoutedEventArgs const&) {
                auto idx = m_lv_routines.SelectedIndex();
                if (idx == -1) {
                    throw hresult_error(E_FAIL, L"从 Details.IsEndedCheckBox 触发更新时遇到了意外的日程下标");
                }
                if (m_cur_routines[idx].is_ended != target_value) {
                    m_cur_routines[idx].is_ended = target_value;
                    m_root_pre->get_model()->try_update_routine_from_user_view(
                        m_root_pre->get_active_user_id(),
                        m_cur_routines[idx]
                    );
                    this->update_cur_day_routines_ui_item(static_cast<uint32_t>(idx));
                }
            };
        };
        m_cb_details_ended.Checked(cb_handler_gen_fn(true));
        m_cb_details_ended.Unchecked(cb_handler_gen_fn(false));
        Grid::SetColumn(m_cb_details_ended, 0);
        grid_details_row1.Children().Append(m_cb_details_ended);
        m_tb_details_title.Padding(ThicknessHelper::FromUniformLength(4));
        m_tb_details_title.Background(nullptr);
        m_tb_details_title.BorderThickness(ThicknessHelper::FromUniformLength(0));
        m_tb_details_title.TextWrapping(TextWrapping::Wrap);
        m_tb_details_title.FontSize(22);
        Grid::SetColumn(m_tb_details_title, 1);
        grid_details_row1.Children().Append(m_tb_details_title);
        sp_details.Children().Append(grid_details_row1);
        // Region: InfoZone
        m_border_details_info.Margin(ThicknessHelper::FromLengths(0, 4, 0, 0));
        m_border_details_info.Padding(ThicknessHelper::FromUniformLength(10));
        m_border_details_info.BorderThickness(ThicknessHelper::FromUniformLength(1));
        m_border_details_info.BorderBrush(SolidColorBrush(Colors::Gray()));
        m_border_details_info.CornerRadius(CornerRadiusHelper::FromUniformRadius(4));
        // #1e90ff: DodgerBlue
        m_border_details_info.Background(SolidColorBrush(ColorHelper::FromArgb(0x40, 0x1e, 0x90, 0xff)));
        auto grid_details_info = Grid();
        auto grid_details_info_cd1 = ColumnDefinition();
        grid_details_info_cd1.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Auto));
        grid_details_info.ColumnDefinitions().Append(grid_details_info_cd1);
        auto grid_details_info_cd2 = ColumnDefinition();
        grid_details_info_cd2.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        grid_details_info.ColumnDefinitions().Append(grid_details_info_cd2);
        auto fi_details_info = FontIcon();
        fi_details_info.Margin(ThicknessHelper::FromLengths(0, 0, 8, -1));
        fi_details_info.FontFamily(FontFamily(L"Segoe MDL2 Assets"));
        fi_details_info.Glyph(L"\xE946");       // Info
        Grid::SetColumn(fi_details_info, 0);
        grid_details_info.Children().Append(fi_details_info);
        m_tb_details_info.Margin(ThicknessHelper::FromLengths(0, -2, 0, 0));
        m_tb_details_info.TextWrapping(TextWrapping::Wrap);
        m_tb_details_info.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(m_tb_details_info, 1);
        grid_details_info.Children().Append(m_tb_details_info);
        m_border_details_info.Child(grid_details_info);
        sp_details.Children().Append(m_border_details_info);
        // Region: TimeZone
        auto border_details_time = Windows::UI::Xaml::Markup::XamlReader::Load(LR"(
<Border xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        Background="{ThemeResource SystemControlBackgroundAltMediumHighBrush}"
        Margin="0,4,0,0" BorderThickness="1" BorderBrush="Gray" Padding="6,4,6,6"
        CornerRadius="4">
</Border>)").as<Border>();
        auto sp_details_time = StackPanel();
        m_tp_details_start.ClockIdentifier(clock_ident_24h);
        m_tp_details_start.Header(box_value(L"开始时间"));
        // NOTE: Special handling of time selection to avoid redundant updates
        m_tp_details_start.SelectedTimeChanged(
            [this](IInspectable const& sender, TimePickerSelectedValueChangedEventArgs const& e) {
                using namespace std::chrono_literals;
                auto start_time = e.NewTime();
                auto end_time = m_tp_details_end.SelectedTime();
                if (!start_time || !end_time) {
                    return;
                }
                auto idx = m_lv_routines.SelectedIndex();
                if (idx == -1) {
                    throw hresult_error(E_FAIL, L"从 Details.StartTimePicker 触发更新时遇到了意外的日程下标");
                }
                auto& cur_routine = m_cur_routines[idx];
                if (start_time.Value() >= end_time.Value()) {
                    // Invalid time; try to restore previous value
                    auto old_start_time = std::chrono::duration_cast<TimeSpan>
                        (((cur_routine.start_secs_since_epoch
                            + duration_to_secs(m_cur_tz_offset)) % SECS_PER_DAY) * 1s);
                    if (old_start_time >= end_time.Value()) {
                        throw hresult_error(E_FAIL, L"从 Details.StartTimePicker 触发更新时完整性检查失败");
                    }
                    sender.as<TimePicker>().SelectedTime(old_start_time);
                }
                else {
                    auto new_start_secs =
                        ((cur_routine.start_secs_since_epoch + duration_to_secs(m_cur_tz_offset)) / SECS_PER_DAY) * SECS_PER_DAY
                        - duration_to_secs(m_cur_tz_offset) + duration_to_secs(start_time.Value());
                    if (cur_routine.start_secs_since_epoch != new_start_secs) {
                        cur_routine.start_secs_since_epoch = new_start_secs;
                        cur_routine.duration_secs = duration_to_secs(end_time.Value() - start_time.Value());
                        auto model = m_root_pre->get_model();
                        auto cur_user = m_root_pre->get_active_user_id();
                        model->try_update_routine_from_user_view(cur_user, cur_routine);
                        if (model->try_lookup_routine(::winrt::guid{ GUID{} }, cur_routine.id, nullptr)) {
                            model->update_public_routine(cur_routine);
                        }
                        // NOTE: Routine ordering in list is not updated on
                        //       purpose (to prevent possible inconsistency)
                        this->update_cur_day_routines_ui_item(static_cast<uint32_t>(idx));
                    }
                }
            }
        );
        sp_details_time.Children().Append(m_tp_details_start);
        m_tp_details_end.ClockIdentifier(clock_ident_24h);
        m_tp_details_end.Header(box_value(L"结束时间"));
        // NOTE: Special handling of time selection to avoid redundant updates
        m_tp_details_end.SelectedTimeChanged(
            [this](IInspectable const& sender, TimePickerSelectedValueChangedEventArgs const& e) {
                using namespace std::chrono_literals;
                auto start_time = m_tp_details_start.SelectedTime();
                auto end_time = e.NewTime();
                if (!start_time || !end_time) {
                    return;
                }
                auto idx = m_lv_routines.SelectedIndex();
                if (idx == -1) {
                    throw hresult_error(E_FAIL, L"从 Details.EndTimePicker 触发更新时遇到了意外的日程下标");
                }
                auto& cur_routine = m_cur_routines[idx];
                if (start_time.Value() >= end_time.Value()) {
                    // Invalid time; try to restore previous value
                    auto old_end_time = std::chrono::duration_cast<TimeSpan>
                        (((cur_routine.start_secs_since_epoch
                            + duration_to_secs(m_cur_tz_offset)
                            + cur_routine.duration_secs) % SECS_PER_DAY) * 1s);
                    if (start_time.Value() >= old_end_time) {
                        throw hresult_error(E_FAIL, L"从 Details.EndTimePicker 触发更新时完整性检查失败");
                    }
                    sender.as<TimePicker>().SelectedTime(old_end_time);
                }
                else {
                    auto new_duration_secs =
                        static_cast<uint64_t>(duration_to_secs(end_time.Value() - start_time.Value()));
                    if (cur_routine.duration_secs != new_duration_secs) {
                        cur_routine.duration_secs = new_duration_secs;
                        auto model = m_root_pre->get_model();
                        auto cur_user = m_root_pre->get_active_user_id();
                        model->try_update_routine_from_user_view(cur_user, cur_routine);
                        if (model->try_lookup_routine(::winrt::guid{ GUID{} }, cur_routine.id, nullptr)) {
                            model->update_public_routine(cur_routine);
                        }
                        this->update_cur_day_routines_ui_item(static_cast<uint32_t>(idx));
                    }
                }
            }
        );
        sp_details_time.Children().Append(m_tp_details_end);
        border_details_time.Child(sp_details_time);
        sp_details.Children().Append(border_details_time);
        // Region: ColorSelection
        m_btn_details_color_selection = Windows::UI::Xaml::Markup::XamlReader::Load(LR"(
<Button xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        Background="{ThemeResource SystemControlBackgroundAltMediumHighBrush}"
        HorizontalAlignment="Stretch" HorizontalContentAlignment="Left"
        Margin="0,4,0,0" BorderThickness="1" BorderBrush="Gray"
        CornerRadius="4">
</Button>)").as<Button>();
        m_btn_details_color_selection.Template(util::winrt::make_simple_button_template());
        auto sp_details_color_selection = StackPanel();
        sp_details_color_selection.Orientation(Orientation::Horizontal);
        m_border_clr_details_color_selection.Margin(ThicknessHelper::FromLengths(0, 2, 8, 2));
        m_border_clr_details_color_selection.Width(20);
        m_border_clr_details_color_selection.Height(20);
        m_border_clr_details_color_selection.CornerRadius(CornerRadiusHelper::FromUniformRadius(10));
        m_border_clr_details_color_selection.BorderThickness(ThicknessHelper::FromUniformLength(1));
        sp_details_color_selection.Children().Append(m_border_clr_details_color_selection);
        m_tb_details_color_selection.TextLineBounds(TextLineBounds::Tight);
        m_tb_details_color_selection.VerticalAlignment(VerticalAlignment::Center);
        sp_details_color_selection.Children().Append(m_tb_details_color_selection);
        m_btn_details_color_selection.Content(sp_details_color_selection);
        auto flyout_btn_details_color_selection = Flyout();
        flyout_btn_details_color_selection.Placement(FlyoutPlacementMode::Left);
        auto cp_flyout_btn_details_color_selection = ColorPicker();
        cp_flyout_btn_details_color_selection.IsColorChannelTextInputVisible(false);
        cp_flyout_btn_details_color_selection.IsHexInputVisible(false);
        flyout_btn_details_color_selection.Content(cp_flyout_btn_details_color_selection);
        flyout_btn_details_color_selection.Opening([=](IInspectable const&, IInspectable const&) {
            auto idx = m_lv_routines.SelectedIndex();
            if (idx == -1) {
                throw hresult_error(E_FAIL, L"更新 Details.ColorPicker 时遇到了意外的日程下标");
            }
            cp_flyout_btn_details_color_selection.Color(
                u32_color_to_winrt(m_cur_routines[idx].color)
            );
        });
        // NOTE: Special handling of color selection to avoid redundant updates
        flyout_btn_details_color_selection.Closed([=](IInspectable const&, IInspectable const&) {
            auto idx = m_lv_routines.SelectedIndex();
            if (idx == -1) {
                throw hresult_error(E_FAIL, L"从 Details.ColorPicker 触发更新时遇到了意外的日程下标");
            }
            auto& cur_routine = m_cur_routines[idx];
            // Update if color is not the same
            auto new_color = winrt_color_to_u32(cp_flyout_btn_details_color_selection.Color());
            if (new_color != cur_routine.color) {
                cur_routine.color = new_color;
                auto model = m_root_pre->get_model();
                auto cur_user = m_root_pre->get_active_user_id();
                model->try_update_routine_from_user_view(cur_user, cur_routine);
                if (model->try_lookup_routine(::winrt::guid{ GUID{} }, cur_routine.id, nullptr)) {
                    model->update_public_routine(cur_routine);
                }
                this->update_cur_day_routines_ui_item(static_cast<uint32_t>(idx));
                this->update_cur_routine_details();
            }
        });
        m_btn_details_color_selection.Flyout(flyout_btn_details_color_selection);
        sp_details.Children().Append(m_btn_details_color_selection);
        // Region: RepeatingZone (hidden if routine is derived)
        m_border_details_repeating = Windows::UI::Xaml::Markup::XamlReader::Load(LR"(
<Border xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        Background="{ThemeResource SystemControlBackgroundAltMediumHighBrush}"
        Margin="0,4,0,0" BorderThickness="1" BorderBrush="Gray" Padding="6,4,6,6"
        CornerRadius="4">
</Border>)").as<Border>();
        auto sp_details_repeating = StackPanel();
        m_cb_details_repeat_type.Width(120);
        m_cb_details_repeat_type.Header(box_value(L"重复类型"));
        m_cb_details_repeat_type.Items().Append(box_value(L"不重复")); // Index 0
        m_cb_details_repeat_type.Items().Append(box_value(L"按天")); // Index 1
        m_cb_details_repeat_type.SelectionChanged([this](IInspectable const& sender, SelectionChangedEventArgs const& e) {
            if (e.AddedItems().Size() == 0 || e.RemovedItems().Size() == 0) {
                // Programmatically triggered event(such as updating data
                // from storage via code); ignore it
                return;
            }
            // Update routine data
            auto idx = m_lv_routines.SelectedIndex();
            if (idx == -1) {
                throw hresult_error(E_FAIL, L"从 Details.RepeatTypeComboBox 触发更新时遇到了意外的日程下标");
            }
            auto& cur_routine = m_cur_routines[idx];
            switch (sender.as<ComboBox>().SelectedIndex()) {
            case 0:     // No repeat
                cur_routine.template_options = nullptr;
                break;
            case 1:     // Repeat by day
            {
                RoutineArranger::Core::RoutineDescTemplate_Repeating template_options;
                template_options.repeat_cycles = 0;
                template_options.repeat_days_cycle = 1;
                template_options.repeat_days_flags = { true };
                cur_routine.template_options = std::move(template_options);
                break;
            }
            case -1:
            default:
                throw hresult_error(E_FAIL, L"处理 Details.RepeatTypeComboBox 时完整性检查失败");
            }
            auto model = m_root_pre->get_model();
            auto cur_user = m_root_pre->get_active_user_id();
            model->try_update_routine_from_user_view(cur_user, cur_routine);
            if (model->try_lookup_routine(::winrt::guid{ GUID{} }, cur_routine.id, nullptr)) {
                model->update_public_routine(cur_routine);
            }
            this->update_cur_day_routines_ui_item(static_cast<uint32_t>(idx));
            this->update_cur_routine_details();
        });
        sp_details_repeating.Children().Append(m_cb_details_repeat_type);
        auto grid_details_repeat_by_day_rd1 = RowDefinition();
        grid_details_repeat_by_day_rd1.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Auto));
        m_grid_details_repeat_by_day.RowDefinitions().Append(grid_details_repeat_by_day_rd1);
        auto grid_details_repeat_by_day_rd2 = RowDefinition();
        grid_details_repeat_by_day_rd2.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Auto));
        m_grid_details_repeat_by_day.RowDefinitions().Append(grid_details_repeat_by_day_rd2);
        auto grid_details_repeat_by_day_rd3 = RowDefinition();
        grid_details_repeat_by_day_rd3.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Auto));
        m_grid_details_repeat_by_day.RowDefinitions().Append(grid_details_repeat_by_day_rd3);
        auto grid_details_repeat_by_day_cd1 = ColumnDefinition();
        grid_details_repeat_by_day_cd1.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        m_grid_details_repeat_by_day.ColumnDefinitions().Append(grid_details_repeat_by_day_cd1);
        auto grid_details_repeat_by_day_cd2 = ColumnDefinition();
        grid_details_repeat_by_day_cd2.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        m_grid_details_repeat_by_day.ColumnDefinitions().Append(grid_details_repeat_by_day_cd2);
        m_tb_details_repeat_by_day_days.HorizontalAlignment(HorizontalAlignment::Stretch);
        m_tb_details_repeat_by_day_days.Margin(ThicknessHelper::FromLengths(0, 0, 2, 0));
        m_tb_details_repeat_by_day_days.Header(box_value(L"循环天数"));
        Grid::SetRow(m_tb_details_repeat_by_day_days, 0);
        Grid::SetColumn(m_tb_details_repeat_by_day_days, 0);
        m_grid_details_repeat_by_day.Children().Append(m_tb_details_repeat_by_day_days);
        m_tb_details_repeat_by_day_cycles.HorizontalAlignment(HorizontalAlignment::Stretch);
        m_tb_details_repeat_by_day_cycles.Margin(ThicknessHelper::FromLengths(2, 0, 0, 0));
        m_tb_details_repeat_by_day_cycles.Header(box_value(L"循环次数 (0 为无限次)"));
        Grid::SetRow(m_tb_details_repeat_by_day_cycles, 0);
        Grid::SetColumn(m_tb_details_repeat_by_day_cycles, 1);
        m_grid_details_repeat_by_day.Children().Append(m_tb_details_repeat_by_day_cycles);
        auto tb_sv_sp_details_repeat_by_day_sel_flags = TextBlock();
        tb_sv_sp_details_repeat_by_day_sel_flags.Text(L"循环日");
        Grid::SetRow(tb_sv_sp_details_repeat_by_day_sel_flags, 1);
        Grid::SetColumn(tb_sv_sp_details_repeat_by_day_sel_flags, 0);
        Grid::SetColumnSpan(tb_sv_sp_details_repeat_by_day_sel_flags, 2);
        m_grid_details_repeat_by_day.Children().Append(tb_sv_sp_details_repeat_by_day_sel_flags);
        auto sv_sp_details_repeat_by_day_sel_flags = ScrollViewer();
        sv_sp_details_repeat_by_day_sel_flags.IsScrollInertiaEnabled(true);
        sv_sp_details_repeat_by_day_sel_flags.HorizontalScrollBarVisibility(ScrollBarVisibility::Auto);
        sv_sp_details_repeat_by_day_sel_flags.HorizontalScrollMode(ScrollMode::Auto);
        sv_sp_details_repeat_by_day_sel_flags.VerticalScrollMode(ScrollMode::Disabled);
        sv_sp_details_repeat_by_day_sel_flags.VerticalScrollBarVisibility(ScrollBarVisibility::Hidden);
        m_sp_details_repeat_by_day_sel_flags.Orientation(Orientation::Horizontal);
        sv_sp_details_repeat_by_day_sel_flags.Content(m_sp_details_repeat_by_day_sel_flags);
        Grid::SetRow(sv_sp_details_repeat_by_day_sel_flags, 2);
        Grid::SetColumn(sv_sp_details_repeat_by_day_sel_flags, 0);
        Grid::SetColumnSpan(sv_sp_details_repeat_by_day_sel_flags, 2);
        m_grid_details_repeat_by_day.Children().Append(sv_sp_details_repeat_by_day_sel_flags);
        sp_details_repeating.Children().Append(m_grid_details_repeat_by_day);
        m_border_details_repeating.Child(sp_details_repeating);
        sp_details.Children().Append(m_border_details_repeating);
        // Region: Description
        auto border_details_description = Windows::UI::Xaml::Markup::XamlReader::Load(LR"(
<Border xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        Background="{ThemeResource SystemControlBackgroundAltMediumHighBrush}"
        Margin="0,4,0,0" BorderThickness="1" BorderBrush="Gray" Padding="6,4"
        CornerRadius="4">
</Border>)").as<Border>();
        auto sp_details_description = StackPanel();
        m_tb_details_description = Windows::UI::Xaml::Markup::XamlReader::Load(LR"(
<TextBox xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
         xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
         AcceptsReturn="True" TextWrapping="Wrap" PlaceholderText="输入备注..."
         Background="Transparent" BorderThickness="0" Padding="0" PlaceholderForeground="Gray">
    <TextBox.Resources>
        <ResourceDictionary>
            <ResourceDictionary.ThemeDictionaries>
                <ResourceDictionary x:Key="Light">
                    <SolidColorBrush x:Key="TextControlForegroundFocused" Color="Black"/>
                </ResourceDictionary>
                <ResourceDictionary x:Key="Dark">
                    <SolidColorBrush x:Key="TextControlForegroundFocused" Color="White"/>
                </ResourceDictionary>
                <ResourceDictionary x:Key="HighContrast">
                    <SolidColorBrush x:Key="TextControlForegroundFocused" Color="{ThemeResource SystemBaseHighColor}"/>
                </ResourceDictionary>
            </ResourceDictionary.ThemeDictionaries>
            <SolidColorBrush x:Key="TextControlBackgroundFocused" Color="Transparent"/>
            <SolidColorBrush x:Key="TextControlBackgroundPointerOver" Color="Transparent"/>
        </ResourceDictionary>
    </TextBox.Resources>
</TextBox>)").as<TextBox>();
        sp_details_description.Children().Append(m_tb_details_description);
        border_details_description.Child(sp_details_description);
        sp_details.Children().Append(border_details_description);
        // Region: DangerZone
        auto border_details_danger = Windows::UI::Xaml::Markup::XamlReader::Load(LR"(
<Border xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        Background="#20ff0000"
        Margin="0,4,0,0" BorderThickness="1" BorderBrush="Red" Padding="6,4"
        CornerRadius="4">
</Border>)").as<Border>();
        auto sp_details_danger = StackPanel();
        auto sp_btn_details_delete = StackPanel();
        sp_btn_details_delete.Orientation(Orientation::Horizontal);
        auto fi_details_delete = FontIcon();
        fi_details_delete.Margin(ThicknessHelper::FromLengths(0, 0, 6, 0));
        fi_details_delete.FontFamily(FontFamily(L"Segoe MDL2 Assets"));
        fi_details_delete.Glyph(L"\xE74D");     // Delete
        sp_btn_details_delete.Children().Append(fi_details_delete);
        auto tb_btn_details_delete = TextBlock();
        tb_btn_details_delete.TextLineBounds(TextLineBounds::Tight);
        tb_btn_details_delete.VerticalAlignment(VerticalAlignment::Center);
        tb_btn_details_delete.Text(L"删除此日程");
        sp_btn_details_delete.Children().Append(tb_btn_details_delete);
        m_btn_details_delete.Content(sp_btn_details_delete);
        m_btn_details_delete.Click([=](IInspectable const&, RoutedEventArgs const&) -> fire_forget_except {
            auto copied_this = this;
            ContentDialog cd;
            cd.XamlRoot(m_btn_details_delete.XamlRoot());
            cd.Title(box_value(L"警告"));
            cd.Content(box_value(L"此日程将被永久移除。仍然继续吗?"));
            cd.PrimaryButtonText(L"确定");
            cd.CloseButtonText(L"取消");
            util::winrt::fix_content_dialog_theme(cd, m_btn_details_delete);
            // Freeze UI to prevent accidental events from firing
            copied_this->m_root_splitview.IsHitTestVisible(false);
            auto result = co_await cd.ShowAsync();
            auto idx = copied_this->m_lv_routines.SelectedIndex();
            if (idx == -1) {
                throw hresult_error(E_FAIL, L"从 Details.DeleteButton 触发更新时遇到了意外的日程下标");
            }
            auto& cur_routine = copied_this->m_cur_routines[idx];
            if (result == ContentDialogResult::Primary) {
                auto model = copied_this->m_root_pre->get_model();
                auto cur_user = copied_this->m_root_pre->get_active_user_id();
                model->try_remove_routine_from_user_view(cur_user, cur_routine.id);
                model->try_remove_public_routine(cur_routine.id);
                copied_this->update_cur_day_routines();
            }
            // Unfreeze UI
            copied_this->m_root_splitview.IsHitTestVisible(true);
        });
        sp_details_danger.Children().Append(m_btn_details_delete);
        border_details_danger.Child(sp_details_danger);
        sp_details.Children().Append(border_details_danger);
        // End Region
        m_sv_details.Content(sp_details);
        m_grid_details_root.Children().Append(m_sv_details);
        m_tb_details_placeholder.HorizontalAlignment(HorizontalAlignment::Center);
        m_tb_details_placeholder.VerticalAlignment(VerticalAlignment::Center);
        m_tb_details_placeholder.Text(L"选择一项日程以查看明细");
        m_grid_details_root.Children().Append(m_tb_details_placeholder);
        m_root_splitview.Pane(m_grid_details_root);
        // ----- SplitView.Pane End
        // ----- SplitView.Content Begin
        auto grid_day_rd1 = RowDefinition();
        grid_day_rd1.Height(GridLengthHelper::FromPixels(40));
        m_grid_day.RowDefinitions().Append(grid_day_rd1);
        auto grid_day_rd2 = RowDefinition();
        grid_day_rd2.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        m_grid_day.RowDefinitions().Append(grid_day_rd2);
        auto grid_day_rd3 = RowDefinition();
        grid_day_rd3.Height(GridLengthHelper::FromPixels(40));
        m_grid_day.RowDefinitions().Append(grid_day_rd3);
        auto grid_day_top = Windows::UI::Xaml::Markup::XamlReader::Load(LR"(
<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
      BorderThickness="0,0,0,1"
      BorderBrush="{ThemeResource SystemControlBackgroundBaseLowBrush}">
    <Grid.ColumnDefinitions>
        <ColumnDefinition Width="40"/>
        <ColumnDefinition Width="*"/>
        <ColumnDefinition Width="40"/>
    </Grid.ColumnDefinitions>
</Grid>)").as<Grid>();
        auto abb_day_top_prev = AppBarButton();
        abb_day_top_prev.Width(40);
        abb_day_top_prev.Height(40);
        auto day_top_prev_icon = SymbolIcon();
        day_top_prev_icon.Symbol(static_cast<Symbol>(0xE76B));  // ChevronLeft
        abb_day_top_prev.Icon(day_top_prev_icon);
        abb_day_top_prev.Click([this](IInspectable const&, RoutedEventArgs const&) {
            m_day_offset--;
            this->update_cur_day_routines();
        });
        grid_day_top.SetColumn(abb_day_top_prev, 0);
        grid_day_top.Children().Append(abb_day_top_prev);
        auto abb_day_top_next = AppBarButton();
        abb_day_top_next.Width(40);
        abb_day_top_next.Height(40);
        auto day_top_next_icon = SymbolIcon();
        day_top_next_icon.Symbol(static_cast<Symbol>(0xE76C));  // ChevronRight
        abb_day_top_next.Icon(day_top_next_icon);
        abb_day_top_next.Click([this](IInspectable const&, RoutedEventArgs const&) {
            m_day_offset++;
            this->update_cur_day_routines();
        });
        grid_day_top.SetColumn(abb_day_top_next, 2);
        grid_day_top.Children().Append(abb_day_top_next);
        m_tb_day_top.HorizontalAlignment(HorizontalAlignment::Center);
        m_tb_day_top.VerticalAlignment(VerticalAlignment::Center);
        m_tb_day_top.TextLineBounds(TextLineBounds::Tight);
        m_tb_day_top.FontWeight(FontWeights::Bold());
        grid_day_top.SetColumn(m_tb_day_top, 1);
        grid_day_top.Children().Append(m_tb_day_top);
        m_grid_day.SetRow(grid_day_top, 0);
        m_grid_day.Children().Append(grid_day_top);
        m_lv_routines.SelectionChanged([this](IInspectable const&, SelectionChangedEventArgs const&) {
            this->update_cur_routine_details();
        });
        m_grid_day.SetRow(m_lv_routines, 1);
        m_grid_day.Children().Append(m_lv_routines);
        m_tb_routines_placeholder.HorizontalAlignment(HorizontalAlignment::Center);
        m_tb_routines_placeholder.VerticalAlignment(VerticalAlignment::Center);
        m_tb_routines_placeholder.Foreground(SolidColorBrush(Colors::Gray()));
        m_grid_day.SetRow(m_tb_routines_placeholder, 1);
        m_grid_day.Children().Append(m_tb_routines_placeholder);
        auto grid_day_bottom = Windows::UI::Xaml::Markup::XamlReader::Load(LR"(
<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
      Background="{ThemeResource SystemControlBackgroundBaseLowBrush}">
    <Grid.ColumnDefinitions>
        <ColumnDefinition Width="40"/>
        <ColumnDefinition Width="*"/>
        <ColumnDefinition Width="40"/>
        <ColumnDefinition Width="40"/>
    </Grid.ColumnDefinitions>
</Grid>)").as<Grid>();
        auto abb_day_bottom_add = AppBarButton();
        ToolTipService::SetToolTip(abb_day_bottom_add, box_value(L"添加"));
        abb_day_bottom_add.Width(40);
        abb_day_bottom_add.Height(40);
        auto icon_day_bottom_add = SymbolIcon();
        icon_day_bottom_add.Symbol(Symbol::Up);
        abb_day_bottom_add.Icon(icon_day_bottom_add);
        auto menu_day_bottom_add = MenuFlyout();
        auto mi_day_bottom_add_template = MenuFlyoutItem();
        auto icon_day_bottom_add_template = SymbolIcon();
        icon_day_bottom_add_template.Symbol(Symbol::OpenFile);
        mi_day_bottom_add_template.Icon(icon_day_bottom_add_template);
        mi_day_bottom_add_template.Text(L"作为模板添加");
        mi_day_bottom_add_template.Click([=](IInspectable const&, RoutedEventArgs const&) {
            RoutineArranger::Core::RoutineDesc routine;
            if (routine_from_input_or_report_fn(routine)) {
                m_root_pre->get_model()->update_public_routine(routine);
                clear_routine_input_fn();
                this->update_cur_day_routines();
            }
        });
        menu_day_bottom_add.Items().Append(mi_day_bottom_add_template);
        menu_day_bottom_add.Opening([=](IInspectable const&, IInspectable const&) {
            auto model = m_root_pre->get_model();
            auto cur_user = m_root_pre->get_active_user_id();
            RoutineArranger::Core::UserDesc ud;
            if (!model->try_lookup_user(cur_user, ud)) {
                throw hresult_error(E_FAIL, L"调出添加日程的上下文菜单时获取用户信息失败");
            }
            mi_day_bottom_add_template.IsEnabled(ud.is_admin);
        });
        abb_day_bottom_add.ContextFlyout(menu_day_bottom_add);
        abb_day_bottom_add.Click([=](IInspectable const&, RoutedEventArgs const&) {
            RoutineArranger::Core::RoutineDesc routine;
            if (routine_from_input_or_report_fn(routine)) {
                m_root_pre->get_model()->try_update_routine_from_user_view(
                    m_root_pre->get_active_user_id(),
                    routine
                );
                clear_routine_input_fn();
                this->update_cur_day_routines();
            }
        });
        Grid::SetColumn(abb_day_bottom_add, 0);
        grid_day_bottom.Children().Append(abb_day_bottom_add);
        m_tb_day_bottom_name.FontSize(20);
        m_tb_day_bottom_name.PlaceholderText(L"输入日程名称...");
        Grid::SetColumn(m_tb_day_bottom_name, 1);
        grid_day_bottom.Children().Append(m_tb_day_bottom_name);
        auto abb_day_bottom_time = AppBarButton();
        abb_day_bottom_time.Width(40);
        abb_day_bottom_time.Height(40);
        auto icon_day_bottom_time = SymbolIcon();
        icon_day_bottom_time.Symbol(static_cast<Symbol>(0xE823));   // Recent
        abb_day_bottom_time.Icon(icon_day_bottom_time);
        abb_day_bottom_time.Click([this](IInspectable const& sender, RoutedEventArgs const&) {
            auto clock_ident_24h = Windows::Globalization::ClockIdentifiers::TwentyFourHour();
            auto flyout = Flyout();
            auto sp = StackPanel();
            auto tp_start = TimePicker();
            tp_start.ClockIdentifier(clock_ident_24h);
            tp_start.Header(box_value(L"开始时间"));
            if (m_cur_editing_time_start) {
                tp_start.Time(m_cur_editing_time_start.Value());
            }
            tp_start.TimeChanged([this](IInspectable const&, TimePickerValueChangedEventArgs const& e) {
                m_cur_editing_time_start = e.NewTime();
            });
            sp.Children().Append(tp_start);
            auto tp_end = TimePicker();
            tp_end.ClockIdentifier(clock_ident_24h);
            tp_end.Header(box_value(L"结束时间"));
            if (m_cur_editing_time_end) {
                tp_end.Time(m_cur_editing_time_end.Value());
            }
            tp_end.TimeChanged([this](IInspectable const&, TimePickerValueChangedEventArgs const& e) {
                m_cur_editing_time_end = e.NewTime();
            });
            sp.Children().Append(tp_end);
            flyout.Content(sp);
            flyout.ShowAt(sender.as<FrameworkElement>());
        });
        Grid::SetColumn(abb_day_bottom_time, 2);
        grid_day_bottom.Children().Append(abb_day_bottom_time);
        auto abb_day_bottom_color = AppBarButton();
        abb_day_bottom_color.Width(40);
        abb_day_bottom_color.Height(40);
        auto icon_day_bottom_color = SymbolIcon();
        icon_day_bottom_color.Symbol(static_cast<Symbol>(0xE790));  // Color
        abb_day_bottom_color.Icon(icon_day_bottom_color);
        abb_day_bottom_color.Click([this](IInspectable const& sender, RoutedEventArgs const&) {
            auto flyout = Flyout();
            auto cp = ColorPicker();
            cp.Color(m_cur_editing_color);
            cp.ColorChanged([this](ColorPicker const&, ColorChangedEventArgs const& e) {
                m_cur_editing_color = e.NewColor();
            });
            cp.IsColorChannelTextInputVisible(false);
            cp.IsHexInputVisible(false);
            flyout.Content(cp);
            flyout.ShowAt(sender.as<FrameworkElement>());
        });
        Grid::SetColumn(abb_day_bottom_color, 3);
        grid_day_bottom.Children().Append(abb_day_bottom_color);
        m_grid_day.SetRow(grid_day_bottom, 2);
        m_grid_day.Children().Append(grid_day_bottom);
        m_root_splitview.Content(m_grid_day);
        // ----- SplitView.Content End
    }
    UIElement DayViewContainerPresenter::get_root() {
        return m_root_splitview;
    }
    void DayViewContainerPresenter::refresh_content(bool user_changed) {
        m_cur_time = std::chrono::system_clock::now();
        m_day_offset = 0;
        m_cur_tz_offset = get_current_time_zone_offset();
        this->update_cur_day_routines();
        this->update_cur_routine_details();
    }
    bool DayViewContainerPresenter::jump_to_routine(::winrt::guid routine_id) {
        RoutineArranger::Core::RoutineDesc rd;
        if (!m_root_pre->get_model()->try_lookup_routine(
            m_root_pre->get_active_user_id(),
            routine_id,
            &rd
        )) {
            return false;
        }
        auto cur_day = time_point_to_secs_since_epoch(m_cur_time + m_cur_tz_offset) / SECS_PER_DAY;
        auto routine_day = (rd.start_secs_since_epoch + duration_to_secs(m_cur_tz_offset)) / SECS_PER_DAY;
        m_day_offset = routine_day - cur_day;
        this->update_cur_day_routines();
        auto it = std::find_if(
            m_cur_routines.begin(), m_cur_routines.end(),
            [&](RoutineArranger::Core::RoutineDesc const& rd) {
                return rd.id == routine_id;
            }
        );
        if (it == m_cur_routines.end()) {
            return false;
        }
        m_lv_routines.SelectedIndex(static_cast<int32_t>(it - m_cur_routines.begin()));
        return true;
    }
    void DayViewContainerPresenter::update_cur_day_routines_ui_item(uint32_t idx) {
        RoutineArranger::Core::RoutineDesc& rd = m_cur_routines[idx];
        auto fm_symbol = FontFamily(L"Segoe MDL2 Assets");

        auto grid = Grid();
        auto grid_rd1 = RowDefinition();
        grid_rd1.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        grid.RowDefinitions().Append(grid_rd1);
        auto grid_rd2 = RowDefinition();
        grid_rd2.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        grid.RowDefinitions().Append(grid_rd2);
        auto grid_cd1 = ColumnDefinition();
        grid_cd1.Width(GridLengthHelper::FromPixels(40));
        grid.ColumnDefinitions().Append(grid_cd1);
        auto grid_cd2 = ColumnDefinition();
        grid_cd2.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        grid.ColumnDefinitions().Append(grid_cd2);
        auto grid_cd3 = ColumnDefinition();
        grid_cd3.Width(GridLengthHelper::FromPixels(40));
        grid.ColumnDefinitions().Append(grid_cd3);
        auto cb = CheckBox();
        cb.IsChecked(rd.is_ended);
        cb.Margin(ThicknessHelper::FromLengths(10, 0, 0, 0));
        auto cb_handler_gen_fn = [&](bool target_value) {
            return [=](IInspectable const&, RoutedEventArgs const&) {
                m_cur_routines[idx].is_ended = target_value;
                m_root_pre->get_model()->try_update_routine_from_user_view(
                    m_root_pre->get_active_user_id(),
                    m_cur_routines[idx]
                );
                if (m_lv_routines.SelectedIndex() == static_cast<int32_t>(idx)) {
                    this->update_cur_routine_details();
                }
            };
        };
        cb.Checked(cb_handler_gen_fn(true));
        cb.Unchecked(cb_handler_gen_fn(false));
        Grid::SetRow(cb, 0);
        Grid::SetColumn(cb, 0);
        Grid::SetRowSpan(cb, 2);
        grid.Children().Append(cb);
        auto tb_name = TextBlock();
        tb_name.Margin(ThicknessHelper::FromLengths(0, 0, 0, 4));
        tb_name.VerticalAlignment(VerticalAlignment::Bottom);
        tb_name.TextLineBounds(TextLineBounds::Tight);
        tb_name.TextTrimming(TextTrimming::CharacterEllipsis);
        tb_name.Text(rd.name);
        Grid::SetRow(tb_name, 0);
        Grid::SetColumn(tb_name, 1);
        grid.Children().Append(tb_name);
        auto tb_time = TextBlock();
        tb_time.Margin(ThicknessHelper::FromLengths(0, 4, 0, 0));
        tb_time.VerticalAlignment(VerticalAlignment::Top);
        tb_time.TextLineBounds(TextLineBounds::Tight);
        tb_time.Foreground(SolidColorBrush(Colors::Gray()));
        auto tb_time_r1 = Windows::UI::Xaml::Documents::Run();
        tb_time_r1.FontSize(11);
        tb_time_r1.FontFamily(fm_symbol);
        tb_time_r1.Text(L"\xE823  ");       // Recent
        tb_time.Inlines().Append(tb_time_r1);
        auto tb_time_r2 = Windows::UI::Xaml::Documents::Run();
        auto time_begin_day_secs = (rd.start_secs_since_epoch + duration_to_secs(m_cur_tz_offset)) % SECS_PER_DAY;
        auto time_end_day_secs = (time_begin_day_secs + rd.duration_secs) % SECS_PER_DAY;
        tb_time_r2.Text(wstrprintf(
            L"%02" PRIu64 ":%02" PRIu64 " ~ %02" PRIu64 ":%02" PRIu64,
            time_begin_day_secs / (SECS_PER_HOUR),
            (time_begin_day_secs % SECS_PER_HOUR) / SECS_PER_MINUTE,
            time_end_day_secs / (SECS_PER_HOUR),
            (time_end_day_secs % SECS_PER_HOUR) / SECS_PER_MINUTE
        ));
        tb_time.Inlines().Append(tb_time_r2);
        // Mark if routine comes from public
        if (m_root_pre->get_model()->try_lookup_routine(::winrt::guid{ GUID{} }, rd.id, nullptr)) {
            auto tb_time_rs = Windows::UI::Xaml::Documents::Run();
            tb_time_rs.FontWeight(FontWeights::Bold());
            tb_time_rs.Text(L"  ·  ");
            tb_time.Inlines().Append(tb_time_rs);
            auto tb_time_r3 = Windows::UI::Xaml::Documents::Run();
            tb_time_r3.FontSize(11);
            tb_time_r3.FontFamily(fm_symbol);
            tb_time_r3.Text(L"\xE7C3");     // Page
            tb_time.Inlines().Append(tb_time_r3);
        }
        // Mark if routine comes from / is personal repeating
        if (!std::holds_alternative<std::nullptr_t>(rd.template_options)) {
            auto tb_time_rs = Windows::UI::Xaml::Documents::Run();
            tb_time_rs.FontWeight(FontWeights::Bold());
            tb_time_rs.Text(L"  ·  ");
            tb_time.Inlines().Append(tb_time_rs);
            auto tb_time_r4 = Windows::UI::Xaml::Documents::Run();
            tb_time_r4.FontSize(11);
            tb_time_r4.FontFamily(fm_symbol);
            tb_time_r4.Text(L"\xE8EE");     // RepeatAll
            tb_time.Inlines().Append(tb_time_r4);
        }
        Grid::SetRow(tb_time, 1);
        Grid::SetColumn(tb_time, 1);
        grid.Children().Append(tb_time);
        auto border_clr = Border();
        border_clr.Width(20);
        border_clr.Height(20);
        border_clr.CornerRadius(CornerRadiusHelper::FromUniformRadius(10));
        auto border_clr_bgclr = u32_color_to_winrt(rd.color);
        border_clr.Background(SolidColorBrush(border_clr_bgclr));
        border_clr.BorderThickness(ThicknessHelper::FromUniformLength(1));
        auto contrast_color = get_text_color_from_background(border_clr_bgclr);
        border_clr.BorderBrush(SolidColorBrush(contrast_color));
        Grid::SetRow(border_clr, 0);
        Grid::SetColumn(border_clr, 2);
        Grid::SetRowSpan(border_clr, 2);
        grid.Children().Append(border_clr);
        m_lv_routines.Items().GetAt(idx).as<ListViewItem>().Content(grid);
    }
    void DayViewContainerPresenter::update_cur_day_routines(void) {
        using namespace std::chrono_literals;

        // Flush & revoke handler to avoid incorrect data flushing
        this->flush_cur_routine_details();
        m_rvk_tb_details_title_losing_focus.revoke();
        m_rvk_tb_details_description_losing_focus.revoke();
        m_rvk_tb_details_repeat_by_day_days_losing_focus.revoke();
        m_rvk_tb_details_repeat_by_day_cycles_losing_focus.revoke();

        std::tm tm_cur = time_point_to_tm(m_cur_time);
        std::tm tm_with_offset = time_point_to_tm(m_cur_time + m_day_offset * 24h);
        std::wstring day_str;
        // Decorator #: removes leading zeros
        // https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/strftime-wcsftime-strftime-l-wcsftime-l
        if (tm_cur.tm_year != tm_with_offset.tm_year) {
            day_str = wstrftime(L"%Y年%#m月%#d日", &tm_with_offset);
        }
        else {
            day_str = wstrftime(L"%#m月%#d日", &tm_with_offset);
        }
        switch (tm_with_offset.tm_wday) {
        case 0:     day_str += L" 星期日";     break;
        case 1:     day_str += L" 星期一";     break;
        case 2:     day_str += L" 星期二";     break;
        case 3:     day_str += L" 星期三";     break;
        case 4:     day_str += L" 星期四";     break;
        case 5:     day_str += L" 星期五";     break;
        case 6:     day_str += L" 星期六";     break;
        }
        switch (m_day_offset) {
        case -1:    day_str = L"昨天 (" + day_str + L")";     break;
        case 0:     day_str = L"今天 (" + day_str + L")";     break;
        case 1:     day_str = L"明天 (" + day_str + L")";     break;
        }
        m_tb_day_top.Text(day_str);

        m_lv_routines.Items().Clear();
        m_cur_routines.clear();
        auto add_routine_item_fn = [this](uint32_t idx, bool is_last) {
            auto lvi = Windows::UI::Xaml::Markup::XamlReader::Load(LR"(
<ListViewItem xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
              Padding="0" Height="50" BorderThickness="0,0,0,1"
              HorizontalContentAlignment="Stretch" VerticalContentAlignment="Stretch"
              BorderBrush="{ThemeResource SystemControlBackgroundBaseLowBrush}">
</ListViewItem>)").as<ListViewItem>();
            lvi.Margin(ThicknessHelper::FromLengths(0, 0, 0, is_last ? 0 : -1));
            m_lv_routines.Items().Append(lvi);
            this->update_cur_day_routines_ui_item(idx);
            // TODO: Add context menu for routines
        };
        auto cur_time = time_point_to_secs_since_epoch(m_cur_time + m_day_offset * 24h + m_cur_tz_offset);
        cur_time = (cur_time / SECS_PER_DAY) * SECS_PER_DAY;
        cur_time -= duration_to_secs(m_cur_tz_offset);
        if (!m_root_pre->get_model()->try_get_routines_from_user_view(
            m_root_pre->get_active_user_id(),
            cur_time,
            cur_time + SECS_PER_DAY,
            m_cur_routines
        )) {
            m_tb_routines_placeholder.Visibility(Visibility::Visible);
            m_tb_routines_placeholder.Text(L"错误: 无法加载日程数据");
            return;
        }
        if (m_cur_routines.empty()) {
            m_tb_routines_placeholder.Visibility(Visibility::Visible);
            m_tb_routines_placeholder.Text(L"当天尚未设置日程");
            return;
        }

        m_tb_routines_placeholder.Visibility(Visibility::Collapsed);
        size_t cur_idx = 0;
        for (; cur_idx + 1 < m_cur_routines.size(); cur_idx++) {
            add_routine_item_fn(static_cast<uint32_t>(cur_idx), false);
        }
        add_routine_item_fn(static_cast<uint32_t>(cur_idx), true);
    }
    void DayViewContainerPresenter::update_cur_routine_details(void) {
        using namespace std::chrono_literals;
        using namespace RoutineArranger::Core;

        auto cur_idx = m_lv_routines.SelectedIndex();
        if (cur_idx == -1) {
            m_sv_details.Visibility(Visibility::Collapsed);
            m_tb_details_placeholder.Visibility(Visibility::Visible);
            return;
        }

        auto& cur_routine = m_cur_routines[cur_idx];
        auto cur_user = m_root_pre->get_active_user_id();
        auto model = m_root_pre->get_model();

        bool modify_allowed = true, delete_allowed = true;

        m_cb_details_ended.IsChecked(cur_routine.is_ended);
        m_tb_details_title.Text(cur_routine.name);
        const wchar_t info_msg_mod_pub_no_permission[] = L"您无权修改或删除公共日程。";
        const wchar_t info_msg_mod_pub[] = L"对公共日程做出的绝大多数修改将对所有尚未使用的用户生效。";
        const wchar_t info_msg_mod_repeating[] = L"对重复日程做出的修改将只对尚未使用的派生日程生效。";
        const wchar_t info_msg_derived_unable_del[] = L"从重复日程派生而来的日程无法被删除，除非删除其父日程。";
        const wchar_t info_msg_derived_parent_not_found[] = L"找不到此日程的父日程。也许它已被删除。";
        m_tb_details_info.Text(L"");

        // Info texts:
        // a) 您无权修改或删除公共日程。
        // b) 对公共日程做出的绝大多数修改将对所有尚未使用的用户生效。
        // c) 对重复日程做出的修改将只对尚未使用的派生日程生效。
        // d) 从重复日程派生而来的日程无法被删除，除非删除其父日程。
        // e) 找不到此日程的父日程。也许它已被删除。
        // Check steps:
        //     IsDerived
        //         d) or e)
        //     Else
        //         IsPublic (currently only considers direct public)
        //             IsAdmin
        //                 IsRepeating
        //                     b) + c)
        //                 Else
        //                     b)
        //             Else
        //                 a)
        //         Else
        //             IsRepeating
        //                 c)
        //             Else
        //                 none)
        if (auto derived = std::get_if<RoutineDescTemplate_Derived>(&cur_routine.template_options)) {
            if (model->try_lookup_routine(cur_user, derived->source_routine, nullptr)) {
                delete_allowed = false;
                m_tb_details_info.Text(info_msg_derived_unable_del);
            }
            else {
                m_tb_details_info.Text(info_msg_derived_parent_not_found);
            }
        }
        else {
            bool is_repeating =
                std::holds_alternative<RoutineDescTemplate_Repeating>(cur_routine.template_options);
            bool is_public = model->try_lookup_routine(::winrt::guid{ GUID{} }, cur_routine.id, nullptr);
            if (is_public) {
                UserDesc ud;
                if (!model->try_lookup_user(cur_user, ud)) {
                    throw hresult_error(E_FAIL, L"刷新日程详情时获取用户信息失败");
                }
                if (ud.is_admin) {
                    m_tb_details_info.Text(
                        info_msg_mod_pub +
                        (is_repeating ? hstring{ L"\n" } + info_msg_mod_repeating : L"")
                    );
                }
                else {
                    modify_allowed = false;
                    m_tb_details_info.Text(info_msg_mod_pub_no_permission);
                }
            }
            else {
                m_tb_details_info.Text(is_repeating ? info_msg_mod_repeating : L"");
            }
        }
        m_border_details_info.Visibility(
            m_tb_details_info.Text() == L"" ? Visibility::Collapsed : Visibility::Visible
        );
        auto routine_winrt_clr = u32_color_to_winrt(cur_routine.color);
        m_border_clr_details_color_selection.Background(SolidColorBrush(routine_winrt_clr));
        m_border_clr_details_color_selection.BorderBrush(SolidColorBrush(
            get_text_color_from_background(routine_winrt_clr)
        ));
        m_tb_details_color_selection.Text(ColorHelper::ToDisplayName(routine_winrt_clr));
        m_tp_details_start.SelectedTime(nullptr);
        m_tp_details_end.SelectedTime(nullptr);
        m_tp_details_start.SelectedTime(
            TimeSpan{ ((cur_routine.start_secs_since_epoch + duration_to_secs(m_cur_tz_offset)) % SECS_PER_DAY) * 1s}
        );
        m_tp_details_end.SelectedTime(
            TimeSpan{
                (
                    (cur_routine.start_secs_since_epoch + duration_to_secs(m_cur_tz_offset)
                        + cur_routine.duration_secs)
                    % SECS_PER_DAY) * 1s
            }
        );
        m_cb_details_repeat_type.SelectedIndex(-1);
        m_sp_details_repeat_by_day_sel_flags.Children().Clear();
        // TODO: Fix fragile index-text connection
        if (auto derived = std::get_if<RoutineDescTemplate_Derived>(&cur_routine.template_options)) {
            // Derived routine
            m_border_details_repeating.Visibility(Visibility::Collapsed);
        }
        else if (std::holds_alternative<std::nullptr_t>(cur_routine.template_options)) {
            // Normal routine
            m_border_details_repeating.Visibility(Visibility::Visible);
            m_cb_details_repeat_type.SelectedIndex(0);  // No repeat
            m_grid_details_repeat_by_day.Visibility(Visibility::Collapsed);
        }
        else if (auto repeating = std::get_if<RoutineDescTemplate_Repeating>(&cur_routine.template_options)) {
            // Repeating routine
            m_border_details_repeating.Visibility(Visibility::Visible);
            m_cb_details_repeat_type.SelectedIndex(1);  // Repeat by day
            m_grid_details_repeat_by_day.Visibility(Visibility::Visible);
            m_tb_details_repeat_by_day_days.Text(to_hstring(repeating->repeat_days_cycle));
            m_tb_details_repeat_by_day_cycles.Text(to_hstring(repeating->repeat_cycles));
            for (size_t i = 0; i < repeating->repeat_days_cycle; i++) {
                auto sp = StackPanel();
                sp.Width(30);
                auto tb = TextBlock();
                tb.HorizontalAlignment(HorizontalAlignment::Center);
                tb.Text(to_hstring(i + 1));
                sp.Children().Append(tb);
                auto cb = CheckBox();
                cb.HorizontalAlignment(HorizontalAlignment::Center);
                cb.Padding(ThicknessHelper::FromUniformLength(0));
                cb.MinWidth(0);
                cb.IsEnabled(modify_allowed);
                cb.IsChecked(static_cast<bool>(repeating->repeat_days_flags[i]));
                auto cb_handler_gen_fn = [=](bool value) {
                    return [this, repeating, i, value](IInspectable const&, RoutedEventArgs const&) {
                        if (i >= repeating->repeat_days_flags.size()) {
                            // Size was already changed; short-circuit out
                            return;
                        }
                        repeating->repeat_days_flags[i] = value;
                        auto idx = m_lv_routines.SelectedIndex();
                        if (idx == -1) {
                            throw hresult_error(E_FAIL, L"从 Details.RepeatByDayFlagCheckBox 触发更新时遇到了意外的日程下标");
                        }
                        auto& cur_routine = m_cur_routines[idx];
                        auto model = m_root_pre->get_model();
                        auto cur_user = m_root_pre->get_active_user_id();
                        model->try_update_routine_from_user_view(cur_user, cur_routine);
                        if (model->try_lookup_routine(::winrt::guid{ GUID{} }, cur_routine.id, nullptr)) {
                            model->update_public_routine(cur_routine);
                        }
                    };
                };
                cb.Checked(cb_handler_gen_fn(true));
                cb.Unchecked(cb_handler_gen_fn(false));
                sp.Children().Append(cb);
                m_sp_details_repeat_by_day_sel_flags.Children().Append(sp);
            }
        }
        else {
            throw hresult_error(E_FAIL, L"刷新日程详情时解析模板类型失败");
        }
        m_tb_details_description.Text(cur_routine.description);

        if (modify_allowed) {
            m_tb_details_title.IsEnabled(true);
            m_btn_details_color_selection.IsEnabled(true);
            m_tp_details_start.IsEnabled(true);
            m_tp_details_end.IsEnabled(true);
            m_cb_details_repeat_type.IsEnabled(true);
            m_tb_details_repeat_by_day_days.IsEnabled(true);
            m_tb_details_repeat_by_day_cycles.IsEnabled(true);
            m_tb_details_description.IsEnabled(true);

            m_btn_details_delete.IsEnabled(delete_allowed);

            auto flush_details_handler = [this](UIElement const&, LosingFocusEventArgs const& e) {
                e.Handled(true);
                this->flush_cur_routine_details();
            };
            if (!m_rvk_tb_details_title_losing_focus) {
                m_rvk_tb_details_title_losing_focus = m_tb_details_title.LosingFocus(
                    auto_revoke,
                    flush_details_handler
                );
            }
            if (!m_rvk_tb_details_description_losing_focus) {
                m_rvk_tb_details_description_losing_focus = m_tb_details_description.LosingFocus(
                    auto_revoke,
                    flush_details_handler
                );
            }
            if (!m_rvk_tb_details_repeat_by_day_days_losing_focus) {
                m_rvk_tb_details_repeat_by_day_days_losing_focus = m_tb_details_repeat_by_day_days.LosingFocus(
                    auto_revoke,
                    flush_details_handler
                );
            }
            if (!m_rvk_tb_details_repeat_by_day_cycles_losing_focus) {
                m_rvk_tb_details_repeat_by_day_cycles_losing_focus = m_tb_details_repeat_by_day_cycles.LosingFocus(
                    auto_revoke,
                    flush_details_handler
                );
            }
        }
        else {
            // NOTE: If modify_allowed is false, deletion is also impossible
            m_tb_details_title.IsEnabled(false);
            m_btn_details_color_selection.IsEnabled(false);
            m_tp_details_start.IsEnabled(false);
            m_tp_details_end.IsEnabled(false);
            m_cb_details_repeat_type.IsEnabled(false);
            m_tb_details_repeat_by_day_days.IsEnabled(false);
            m_tb_details_repeat_by_day_cycles.IsEnabled(false);
            m_tb_details_description.IsEnabled(false);

            m_btn_details_delete.IsEnabled(false);
        }

        m_sv_details.Visibility(Visibility::Visible);
        m_tb_details_placeholder.Visibility(Visibility::Collapsed);
    }
    void DayViewContainerPresenter::flush_cur_routine_details(void) {
        // NOTE: This method only compares difference of non-immediate
        //       fields(usually text input) and decides whether to trigger flush
        using namespace RoutineArranger::Core;

        auto cur_idx = m_lv_routines.SelectedIndex();
        if (cur_idx == -1) {
            return;
        }
        auto& cur_routine = m_cur_routines[cur_idx];

        bool flush_ui_item_required = false;
        bool flush_details_required = false;
        bool flush_storage_required = false;

        // Ignore empty title
        if (m_tb_details_title.Text() == L"") {
            m_tb_details_title.Text(cur_routine.name);
        }
        // Check #1
        if (m_tb_details_title.Text() != cur_routine.name) {
            cur_routine.name = m_tb_details_title.Text();
            flush_storage_required = true;
            flush_ui_item_required = true;
        }
        // Check #2
        if (m_tb_details_description.Text() != cur_routine.description) {
            cur_routine.description = m_tb_details_description.Text();
            flush_storage_required = true;
        }
        if (auto repeating = std::get_if<RoutineDescTemplate_Repeating>(&cur_routine.template_options)) {
            uint32_t repeat_days, repeat_cycles;
            // Ignore invalid & extract values
            auto parse_field_fn = [](TextBox const& tb, auto orig_value) {
                // Ensures positive values
                auto str = tb.Text();
                if (str == L"" || !std::all_of(str.begin(), str.end(), std::iswdigit)) {
                    tb.Text(to_hstring(orig_value));
                    return orig_value;
                }
                return static_cast<decltype(orig_value)>(std::wcstoull(str.c_str(), nullptr, 10));
            };
            repeat_days = parse_field_fn(m_tb_details_repeat_by_day_days, repeating->repeat_days_cycle);
            repeat_cycles = parse_field_fn(m_tb_details_repeat_by_day_cycles, repeating->repeat_cycles);
            if (repeat_days == 0) {
                m_tb_details_repeat_by_day_days.Text(to_hstring(repeating->repeat_days_cycle));
                repeat_days = repeating->repeat_days_cycle;
            }
            // Check #3
            if (repeat_days != repeating->repeat_days_cycle) {
                repeating->repeat_days_cycle = repeat_days;
                repeating->repeat_days_flags.resize(repeat_days, false);
                flush_details_required = true;
                flush_storage_required = true;
            }
            // Check #4
            if (repeat_cycles != repeating->repeat_cycles) {
                repeating->repeat_cycles = repeat_cycles;
                flush_storage_required = true;
            }
        }

        if (flush_storage_required) {
            auto model = m_root_pre->get_model();
            auto cur_user = m_root_pre->get_active_user_id();
            model->try_update_routine_from_user_view(cur_user, cur_routine);
            if (model->try_lookup_routine(::winrt::guid{ GUID{} }, cur_routine.id, nullptr)) {
                model->update_public_routine(cur_routine);
            }
        }
        if (flush_ui_item_required) {
            this->update_cur_day_routines_ui_item(static_cast<uint32_t>(cur_idx));
        }
        if (flush_details_required) {
            // Delay UI update to prevent nullptr crash when clicking
            // StackPanel of repeating day flags
            // TODO: Report ICE when the parameter type uses `auto`
            [](DayViewContainerPresenter* copied_this) -> fire_forget_except {
                using namespace std::chrono_literals;
                apartment_context ui_ctx;
                co_await 1ms;
                co_await ui_ctx;
                copied_this->update_cur_routine_details();
            }(this);
        }
    }

    MonthViewContainerPresenter::MonthViewContainerPresenter(RootContainerPresenter* root) :
        m_root_splitview(SplitView()), m_calendar_view(CalendarView()),
        m_grid_day_root(Grid()), m_grid_day(Grid()), m_tb_day_top(TextBlock()),
        m_lv_day_routines(ListView()), m_tb_day_routines_placeholder(TextBlock()),
        m_tb_day_placeholder(TextBlock()),
        m_root_pre(root),
        m_cur_routines()
    {
        m_calendar_view.HorizontalAlignment(HorizontalAlignment::Stretch);
        m_calendar_view.VerticalAlignment(VerticalAlignment::Stretch);
        m_calendar_view.FirstDayOfWeek(Windows::Globalization::DayOfWeek::Monday);
        m_calendar_view.IsTodayHighlighted(false);
        // Keep CalendarView date range within C++ system_clock range
        m_calendar_view.MinDate(winrt::clock::from_sys(std::chrono::system_clock::time_point{}));
        m_calendar_view.RegisterPropertyChangedCallback(
            CalendarView::DisplayModeProperty(),
            [](DependencyObject const& sender, DependencyProperty const& dp) {
                if (dp == CalendarView::DisplayModeProperty()) {
                    auto cv = sender.as<CalendarView>();
                    if (cv.DisplayMode() == CalendarViewDisplayMode::Month) {
                        cv.IsTodayHighlighted(false);
                    }
                    else {
                        cv.IsTodayHighlighted(true);
                    }
                }
            }
        );
        m_calendar_view.CalendarViewDayItemChanging({ this, &MonthViewContainerPresenter::CalendarView_DayItemChanging });
        m_calendar_view.SelectedDatesChanged([this](CalendarView const&, CalendarViewSelectedDatesChangedEventArgs const& e) {
            auto added_dates = e.AddedDates();
            if (added_dates.Size() == 0) {
                // Aggressively clear routines list
                m_lv_day_routines.Items().Clear();
                m_grid_day.Visibility(Visibility::Collapsed);
                m_tb_day_placeholder.Visibility(Visibility::Visible);
                return;
            }
            m_grid_day.Visibility(Visibility::Visible);
            m_tb_day_placeholder.Visibility(Visibility::Collapsed);
            this->update_day(*added_dates.First());
        });
        m_root_splitview.DisplayMode(SplitViewDisplayMode::Inline);
        m_root_splitview.IsPaneOpen(true);
        m_root_splitview.OpenPaneLength(400);
        m_root_splitview.PanePlacement(SplitViewPanePlacement::Right);
        auto grid_day_rd1 = RowDefinition();
        grid_day_rd1.Height(GridLengthHelper::FromPixels(40));
        m_grid_day.RowDefinitions().Append(grid_day_rd1);
        auto grid_day_rd2 = RowDefinition();
        grid_day_rd2.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        m_grid_day.RowDefinitions().Append(grid_day_rd2);
        auto grid_day_top = Windows::UI::Xaml::Markup::XamlReader::Load(LR"(
<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
      BorderThickness="0,0,0,1"
      BorderBrush="{ThemeResource SystemControlBackgroundBaseLowBrush}">
</Grid>)").as<Grid>();
        m_tb_day_top.HorizontalAlignment(HorizontalAlignment::Center);
        m_tb_day_top.VerticalAlignment(VerticalAlignment::Center);
        m_tb_day_top.TextLineBounds(TextLineBounds::Tight);
        m_tb_day_top.FontWeight(FontWeights::Bold());
        grid_day_top.Children().Append(m_tb_day_top);
        Grid::SetRow(grid_day_top, 0);
        m_grid_day.Children().Append(grid_day_top);
        m_lv_day_routines.SelectionMode(ListViewSelectionMode::None);
        m_lv_day_routines.IsItemClickEnabled(true);
        m_lv_day_routines.ItemClick([this](IInspectable const& sender, ItemClickEventArgs const& e) {
            auto parent = VisualTreeHelper::GetParent(e.ClickedItem().as<DependencyObject>());
            ListViewItem lvi(nullptr);
            while (!(lvi = parent.try_as<ListViewItem>())) {
                parent = VisualTreeHelper::GetParent(parent);
            }
            uint32_t idx;
            if (!sender.as<ListView>().Items().IndexOf(lvi, idx)) {
                throw hresult_error(E_FAIL, L"点击了不存在的日程");
            }
            // Jump to DayView with given routine selected
            m_root_pre->jump_to_day_view_routine(m_cur_routines[idx].id);
        });
        Grid::SetRow(m_lv_day_routines, 1);
        m_grid_day.Children().Append(m_lv_day_routines);
        m_tb_day_routines_placeholder.HorizontalAlignment(HorizontalAlignment::Center);
        m_tb_day_routines_placeholder.VerticalAlignment(VerticalAlignment::Center);
        m_tb_day_routines_placeholder.Foreground(SolidColorBrush(Colors::Gray()));
        Grid::SetRow(m_tb_day_routines_placeholder, 1);
        m_grid_day.Children().Append(m_tb_day_routines_placeholder);
        m_grid_day_root.Children().Append(m_grid_day);
        m_tb_day_placeholder.HorizontalAlignment(HorizontalAlignment::Center);
        m_tb_day_placeholder.VerticalAlignment(VerticalAlignment::Center);
        m_tb_day_placeholder.Text(L"选择一天以查看明细");
        m_grid_day_root.Children().Append(m_tb_day_placeholder);
        m_root_splitview.Pane(m_grid_day_root);
        m_root_splitview.Content(m_calendar_view);
    }
    UIElement MonthViewContainerPresenter::get_root() {
        return m_root_splitview;
    }
    void MonthViewContainerPresenter::refresh_content(bool user_changed) {
        m_cur_tz_offset = get_current_time_zone_offset();
        // Update existing items' destiny colors
        auto hack_get_cvdi_container_fn = [this]() -> UIElement {
            using util::winrt::get_child_elem;
            auto elem = get_child_elem(m_calendar_view);            // [Border]
            elem = get_child_elem(elem);                            // [Grid]
            elem = get_child_elem(elem, L"Views");                  // Views [Grid]
            elem = get_child_elem(elem, L"MonthView");              // MonthView [Grid]
            elem = get_child_elem(elem, L"MonthViewScrollViewer");  // MonthViewScrollViewer [ScrollViewer]
            if (auto sv = elem.try_as<ScrollViewer>()) {
                return sv.Content().as<UIElement>();
            }
            return nullptr;
        };
        auto cvdi_container = hack_get_cvdi_container_fn();
        if (cvdi_container) {
            auto children_count = VisualTreeHelper::GetChildrenCount(cvdi_container);
            for (decltype(children_count) i = 0; i < children_count; i++) {
                if (auto item = VisualTreeHelper::GetChild(cvdi_container, i).try_as<CalendarViewDayItem>()) {
                    update_calendar_view_day_item(item);
                }
            }
        }
        // Update selection to be today
        auto cur_datetime = winrt::clock::from_sys(std::chrono::system_clock::now());
        m_calendar_view.SelectedDates().Clear();
        m_calendar_view.SelectedDates().Append(cur_datetime);
    }
    void MonthViewContainerPresenter::CalendarView_DayItemChanging(
        CalendarView const& sender,
        CalendarViewDayItemChangingEventArgs const& e
    ) {
        auto phase = e.Phase();
        if (phase == 0 || phase == 1) {
            e.RegisterUpdateCallback({ this, &MonthViewContainerPresenter::CalendarView_DayItemChanging });
            return;
        }
        update_calendar_view_day_item(e.Item());
    }
    void MonthViewContainerPresenter::update_calendar_view_day_item(CalendarViewDayItem const& item) {
        // NOTE: winrt::clock::to_sys is required in order to ensure consistent epochs
        auto cur_time_begin = time_point_to_secs_since_epoch(winrt::clock::to_sys(item.Date()) + m_cur_tz_offset);
        cur_time_begin = (cur_time_begin / SECS_PER_DAY) * SECS_PER_DAY;
        cur_time_begin -= duration_to_secs(m_cur_tz_offset);
        std::vector<RoutineArranger::Core::RoutineDesc> routines;
        if (!m_root_pre->get_model()->try_get_routines_from_user_view(
            m_root_pre->get_active_user_id(),
            cur_time_begin,
            cur_time_begin + SECS_PER_DAY,
            routines
        )) {
            // Fail silently
            return;
        }
        std::vector<Color> colors;
        std::transform(
            routines.begin(), routines.end(),
            std::back_inserter(colors),
            [](RoutineArranger::Core::RoutineDesc const& v) {
                return u32_color_to_winrt(v.color);
            }
        );
        item.SetDensityColors(colors);
    }
    void MonthViewContainerPresenter::update_day_routines_ui_item(uint32_t idx) {
        // Mostly from DayViewContainerPresenter::update_routines_ui_item
        RoutineArranger::Core::RoutineDesc& rd = m_cur_routines[idx];
        auto fm_symbol = FontFamily(L"Segoe MDL2 Assets");

        auto grid = Grid();
        auto grid_rd1 = RowDefinition();
        grid_rd1.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        grid.RowDefinitions().Append(grid_rd1);
        auto grid_rd2 = RowDefinition();
        grid_rd2.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        grid.RowDefinitions().Append(grid_rd2);
        auto grid_cd1 = ColumnDefinition();
        grid_cd1.Width(GridLengthHelper::FromPixels(40));
        grid.ColumnDefinitions().Append(grid_cd1);
        auto grid_cd2 = ColumnDefinition();
        grid_cd2.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        grid.ColumnDefinitions().Append(grid_cd2);
        auto grid_cd3 = ColumnDefinition();
        grid_cd3.Width(GridLengthHelper::FromPixels(40));
        grid.ColumnDefinitions().Append(grid_cd3);
        auto cb = CheckBox();
        cb.IsChecked(rd.is_ended);
        cb.Margin(ThicknessHelper::FromLengths(10, 0, 0, 0));
        auto cb_handler_gen_fn = [&](bool target_value) {
            return [=](IInspectable const&, RoutedEventArgs const&) {
                m_cur_routines[idx].is_ended = target_value;
                m_root_pre->get_model()->try_update_routine_from_user_view(
                    m_root_pre->get_active_user_id(),
                    m_cur_routines[idx]
                );
            };
        };
        cb.Checked(cb_handler_gen_fn(true));
        cb.Unchecked(cb_handler_gen_fn(false));
        Grid::SetRow(cb, 0);
        Grid::SetColumn(cb, 0);
        Grid::SetRowSpan(cb, 2);
        grid.Children().Append(cb);
        auto tb_name = TextBlock();
        tb_name.Margin(ThicknessHelper::FromLengths(0, 0, 0, 4));
        tb_name.VerticalAlignment(VerticalAlignment::Bottom);
        tb_name.TextLineBounds(TextLineBounds::Tight);
        tb_name.TextTrimming(TextTrimming::CharacterEllipsis);
        tb_name.Text(rd.name);
        Grid::SetRow(tb_name, 0);
        Grid::SetColumn(tb_name, 1);
        grid.Children().Append(tb_name);
        auto tb_time = TextBlock();
        tb_time.Margin(ThicknessHelper::FromLengths(0, 4, 0, 0));
        tb_time.VerticalAlignment(VerticalAlignment::Top);
        tb_time.TextLineBounds(TextLineBounds::Tight);
        tb_time.Foreground(SolidColorBrush(Colors::Gray()));
        auto tb_time_r1 = Windows::UI::Xaml::Documents::Run();
        tb_time_r1.FontSize(11);
        tb_time_r1.FontFamily(fm_symbol);
        tb_time_r1.Text(L"\xE823  ");       // Recent
        tb_time.Inlines().Append(tb_time_r1);
        auto tb_time_r2 = Windows::UI::Xaml::Documents::Run();
        auto time_begin_day_secs = (rd.start_secs_since_epoch + duration_to_secs(m_cur_tz_offset)) % SECS_PER_DAY;
        auto time_end_day_secs = (time_begin_day_secs + rd.duration_secs) % SECS_PER_DAY;
        tb_time_r2.Text(wstrprintf(
            L"%02" PRIu64 ":%02" PRIu64 " ~ %02" PRIu64 ":%02" PRIu64,
            time_begin_day_secs / (SECS_PER_HOUR),
            (time_begin_day_secs % SECS_PER_HOUR) / SECS_PER_MINUTE,
            time_end_day_secs / (SECS_PER_HOUR),
            (time_end_day_secs % SECS_PER_HOUR) / SECS_PER_MINUTE
        ));
        tb_time.Inlines().Append(tb_time_r2);
        // Mark if routine comes from public
        if (m_root_pre->get_model()->try_lookup_routine(::winrt::guid{ GUID{} }, rd.id, nullptr)) {
            auto tb_time_rs = Windows::UI::Xaml::Documents::Run();
            tb_time_rs.FontWeight(FontWeights::Bold());
            tb_time_rs.Text(L"  ·  ");
            tb_time.Inlines().Append(tb_time_rs);
            auto tb_time_r3 = Windows::UI::Xaml::Documents::Run();
            tb_time_r3.FontSize(11);
            tb_time_r3.FontFamily(fm_symbol);
            tb_time_r3.Text(L"\xE7C3");     // Page
            tb_time.Inlines().Append(tb_time_r3);
        }
        // Mark if routine comes from / is personal repeating
        if (!std::holds_alternative<std::nullptr_t>(rd.template_options)) {
            auto tb_time_rs = Windows::UI::Xaml::Documents::Run();
            tb_time_rs.FontWeight(FontWeights::Bold());
            tb_time_rs.Text(L"  ·  ");
            tb_time.Inlines().Append(tb_time_rs);
            auto tb_time_r4 = Windows::UI::Xaml::Documents::Run();
            tb_time_r4.FontSize(11);
            tb_time_r4.FontFamily(fm_symbol);
            tb_time_r4.Text(L"\xE8EE");     // RepeatAll
            tb_time.Inlines().Append(tb_time_r4);
        }
        Grid::SetRow(tb_time, 1);
        Grid::SetColumn(tb_time, 1);
        grid.Children().Append(tb_time);
        auto border_clr = Border();
        border_clr.Width(20);
        border_clr.Height(20);
        border_clr.CornerRadius(CornerRadiusHelper::FromUniformRadius(10));
        auto border_clr_bgclr = u32_color_to_winrt(rd.color);
        border_clr.Background(SolidColorBrush(border_clr_bgclr));
        border_clr.BorderThickness(ThicknessHelper::FromUniformLength(1));
        auto contrast_color = get_text_color_from_background(border_clr_bgclr);
        border_clr.BorderBrush(SolidColorBrush(contrast_color));
        Grid::SetRow(border_clr, 0);
        Grid::SetColumn(border_clr, 2);
        Grid::SetRowSpan(border_clr, 2);
        grid.Children().Append(border_clr);
        m_lv_day_routines.Items().GetAt(idx).as<ListViewItem>().Content(grid);
    }
    void MonthViewContainerPresenter::update_day(Windows::Foundation::DateTime const& dt) {
        auto given_time = winrt::clock::to_sys(dt);
        std::tm tm_cur = time_point_to_tm(std::chrono::system_clock::now());
        std::tm tm_given = time_point_to_tm(given_time);
        std::wstring day_str;
        if (tm_cur.tm_year != tm_given.tm_year) {
            day_str = wstrftime(L"%Y年%#m月%#d日", &tm_given);
        }
        else {
            day_str = wstrftime(L"%#m月%#d日", &tm_given);
        }
        switch (tm_given.tm_wday) {
        case 0:     day_str += L" 星期日";     break;
        case 1:     day_str += L" 星期一";     break;
        case 2:     day_str += L" 星期二";     break;
        case 3:     day_str += L" 星期三";     break;
        case 4:     day_str += L" 星期四";     break;
        case 5:     day_str += L" 星期五";     break;
        case 6:     day_str += L" 星期六";     break;
        }
        switch (tm_given.tm_yday - tm_cur.tm_yday) {
        case -1:    day_str = L"昨天 (" + day_str + L")";     break;
        case 0:     day_str = L"今天 (" + day_str + L")";     break;
        case 1:     day_str = L"明天 (" + day_str + L")";     break;
        }
        m_tb_day_top.Text(day_str);

        m_lv_day_routines.Items().Clear();
        m_cur_routines.clear();
        auto add_routine_item_fn = [this](uint32_t idx, bool is_last) {
            auto lvi = Windows::UI::Xaml::Markup::XamlReader::Load(LR"(
<ListViewItem xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
              Padding="0" Height="50" BorderThickness="0,0,0,1"
              HorizontalContentAlignment="Stretch" VerticalContentAlignment="Stretch"
              BorderBrush="{ThemeResource SystemControlBackgroundBaseLowBrush}">
</ListViewItem>)").as<ListViewItem>();
            lvi.Margin(ThicknessHelper::FromLengths(0, 0, 0, is_last ? 0 : -1));
            m_lv_day_routines.Items().Append(lvi);
            this->update_day_routines_ui_item(idx);
        };
        auto cur_time = time_point_to_secs_since_epoch(given_time + m_cur_tz_offset);
        cur_time = (cur_time / SECS_PER_DAY) * SECS_PER_DAY;
        cur_time -= duration_to_secs(m_cur_tz_offset);
        if (!m_root_pre->get_model()->try_get_routines_from_user_view(
            m_root_pre->get_active_user_id(),
            cur_time,
            cur_time + SECS_PER_DAY,
            m_cur_routines
        )) {
            m_tb_day_routines_placeholder.Visibility(Visibility::Visible);
            m_tb_day_routines_placeholder.Text(L"错误: 无法加载日程数据");
            return;
        }
        if (m_cur_routines.empty()) {
            m_tb_day_routines_placeholder.Visibility(Visibility::Visible);
            m_tb_day_routines_placeholder.Text(L"当天尚未设置日程");
            return;
        }

        m_tb_day_routines_placeholder.Visibility(Visibility::Collapsed);
        size_t cur_idx = 0;
        for (; cur_idx + 1 < m_cur_routines.size(); cur_idx++) {
            add_routine_item_fn(static_cast<uint32_t>(cur_idx), false);
        }
        add_routine_item_fn(static_cast<uint32_t>(cur_idx), true);
    }

    SearchContainerPresenter::SearchContainerPresenter(RootContainerPresenter* root) :
        m_root_grid(Grid()), m_grid_search_box_bg(Grid()), m_search_box(nullptr),
        m_lv_routines(ListView()), m_tb_lv_routines_footer(TextBlock()),
        m_root_pre(root)
    {
        auto root_grid_rd1 = RowDefinition();
        root_grid_rd1.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Auto));
        m_root_grid.RowDefinitions().Append(root_grid_rd1);
        auto root_grid_rd2 = RowDefinition();
        root_grid_rd2.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        m_root_grid.RowDefinitions().Append(root_grid_rd2);
        Grid::SetRow(m_grid_search_box_bg, 0);
        m_root_grid.Children().Append(m_grid_search_box_bg);
        m_search_box = Windows::UI::Xaml::Markup::XamlReader::Load(LR"(
<AutoSuggestBox xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
         xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
         PlaceholderText="输入筛选器...">
    <AutoSuggestBox.Resources>
        <ResourceDictionary>
            <ResourceDictionary.ThemeDictionaries>
                <ResourceDictionary x:Key="Light">
                    <SolidColorBrush x:Key="TextControlForegroundFocused" Color="Black"/>
                </ResourceDictionary>
                <ResourceDictionary x:Key="Dark">
                    <SolidColorBrush x:Key="TextControlForegroundFocused" Color="White"/>
                </ResourceDictionary>
                <ResourceDictionary x:Key="HighContrast">
                    <SolidColorBrush x:Key="TextControlForegroundFocused" Color="{ThemeResource SystemBaseHighColor}"/>
                </ResourceDictionary>
            </ResourceDictionary.ThemeDictionaries>
            <SolidColorBrush x:Key="TextControlBackgroundFocused" Color="Transparent"/>
            <SolidColorBrush x:Key="TextControlPlaceholderForegroundFocused" Color="Gray"/>
        </ResourceDictionary>
    </AutoSuggestBox.Resources>
</AutoSuggestBox>)").as<AutoSuggestBox>();
        // Hack AutoSuggestBox to disable spell check
        auto et_search_box_loaded = std::make_shared<event_token>();
        *et_search_box_loaded = m_search_box.Loaded([=](IInspectable const& sender, RoutedEventArgs const&) {
            if (auto tb = util::winrt::get_child_elem(
                util::winrt::get_child_elem(sender.as<UIElement>(), L"LayoutRoot"),
                L"TextBox").try_as<TextBox>())
            {
                tb.IsSpellCheckEnabled(false);
            }
            sender.as<AutoSuggestBox>().Loaded(*et_search_box_loaded);
        });
        m_search_box.Loaded([=](IInspectable const& sender, RoutedEventArgs const&) {
            sender.as<Control>().Focus(FocusState::Programmatic);
        });
        m_search_box.TextChanged({ this, &SearchContainerPresenter::SearchBox_TextChanged });
        Grid::SetRow(m_search_box, 0);
        m_root_grid.Children().Append(m_search_box);
        m_tb_lv_routines_footer.HorizontalAlignment(HorizontalAlignment::Center);
        m_tb_lv_routines_footer.Padding(ThicknessHelper::FromLengths(0, 0, 0, 2));
        m_tb_lv_routines_footer.Foreground(SolidColorBrush(Colors::Gray()));
        m_lv_routines.Footer(m_tb_lv_routines_footer);
        m_lv_routines.SelectionMode(ListViewSelectionMode::None);
        m_lv_routines.IsItemClickEnabled(true);
        m_lv_routines.ItemClick([this](IInspectable const& sender, ItemClickEventArgs const& e) {
            auto routine_id = util::winrt::to_guid(e.ClickedItem().as<FrameworkElement>().Name());
            m_root_pre->jump_to_day_view_routine(routine_id);
        });
        Grid::SetRow(m_lv_routines, 1);
        m_root_grid.Children().Append(m_lv_routines);
    }
    UIElement SearchContainerPresenter::get_root() {
        return m_root_grid;
    }
    void SearchContainerPresenter::refresh_content(bool user_changed) {
        m_cur_tz_offset = get_current_time_zone_offset();
        m_search_box.Text(L"");
        this->show_input_error(!this->update_search_results(L""));
    }
    void SearchContainerPresenter::SearchBox_TextChanged(
        AutoSuggestBox const& sender,
        AutoSuggestBoxTextChangedEventArgs const& e
    ) {
        // For some reason, programmatic changes in method refresh_content do not
        // fire this event. So we will just ignore e.Reason() and update directly.
        /*
        if (e.Reason() == AutoSuggestionBoxTextChangeReason::UserInput) {
            this->show_input_error(!this->update_search_results(sender.Text()));
        }
        */
        this->show_input_error(!this->update_search_results(sender.Text()));
    }
    void SearchContainerPresenter::show_input_error(bool show) {
        if (show) {
            m_grid_search_box_bg.Background(SolidColorBrush(ColorHelper::FromArgb(0x80, 0xff, 0x00, 0x00)));
        }
        else {
            m_grid_search_box_bg.Background(nullptr);
        }
    }
    bool SearchContainerPresenter::update_search_results(std::wstring_view filter_str) {
        using namespace std::chrono_literals;

        /* Filter string specification:
        * [d:([([+]date)|([-]digits)].[([+]date)|([-]digits)])|[([+]date)|([-]digits)]
        * [t:([clock].[clock])|[clock]]
        * [e:<t|f>]
        * [desc:[description]]
        * [title]
        * Field names are case sensitive.
        * "..." can be used to escape spaces.
        * `\` escaping is currently not considered.
        * Field d & t & e shall not appear more than once.
        * Field d:
        *     Specifies date range.
        *     Examples:
        *         d:2022/3/14.2022/3/15         2022/3/14~2022/3/15
        *         d:+12.+14                     <Y>/<M>/12~<Y>/<M>/14
        *         d:-12.                        <Y>/<M>/<D>-12~<Y>/<M>/<D>
        *         d:.                           <Y>/<M>/<D>~<Y>/<M>/<D>
        *         d:12.14                       <Y>/<M>/<D>+12~<Y>/<M>/<D>+14
        *         d:/3/.//4                     <Y>/3/<D>~<Y>/<M>/4
        *         d:                            <Y>/<M>/<D>~<Y>/<M>/<D>
        *     1. `+` forces recognition of date, otherwise date format can be
        *        deduced from `/`.
        *     2. `-` and `<empty>` specifies relative day offsets based on today,
        *        one for negatives and one for positives.
        *     3. `.` specifies a date range.
        *     4. If date/digits field is empty, today will be used instead.
        * Field t:
        *     Specifies time range within one day (in 24hrs format). Any related
        *     Examples:
        *         t:13-40.15-30                 13:40~15:30
        *         t:-15.15-                     <H>:15~15:<M>
        *         t:.                           <H>:<M>~<H>:<M>
        *         t:12-13                       12:13~12:13
        *     1. Time must be written in format `[H]-[M]`.
        * Field e:
        *     Specifies whether routine is ended.
        */

        struct FilterFieldDesc {
            std::wstring name;
            std::wstring data;
        };

        bool failed = false;

        // Extract filter fields        
        auto extract_fields_fn = [&](std::wstring_view str) -> std::vector<FilterFieldDesc> {
            std::vector<FilterFieldDesc> descs;
            enum class ParsePhase {
                None, Name, Data
            } field_phase = ParsePhase::None;
            bool last_in_space = true, cur_in_quotes = false;
            FilterFieldDesc cur_desc;
            for (auto ch : str) {
                if (ch == L'"') {
                    cur_in_quotes = !cur_in_quotes;
                    continue;
                }
                if (cur_in_quotes) {
                    switch (field_phase) {
                    case ParsePhase::None:
                        field_phase = ParsePhase::Name;
                        cur_desc.name.push_back(ch);
                        break;
                    case ParsePhase::Name:
                        cur_desc.name.push_back(ch);
                        break;
                    case ParsePhase::Data:
                        cur_desc.data.push_back(ch);
                        break;
                    }
                    continue;
                }
                if (std::iswspace(ch)) {
                    // Should transit to ParsePhase::None
                    if (field_phase != ParsePhase::None) {
                        if (field_phase == ParsePhase::Name) {
                            // No `:` detected; field contains only data
                            cur_desc.data = std::move(cur_desc.name);
                            cur_desc.name.clear();
                        }
                        descs.push_back(cur_desc);
                        cur_desc.name.clear();
                        cur_desc.data.clear();
                        field_phase = ParsePhase::None;
                    }
                    continue;
                }
                if (ch == L':') {
                    // Should transit to ParsePhase::Data
                    if (field_phase == ParsePhase::Data) {
                        failed = true;
                        return {};
                    }
                    field_phase = ParsePhase::Data;
                    continue;
                }
                switch (field_phase) {
                case ParsePhase::None:
                    field_phase = ParsePhase::Name;
                    cur_desc.name.push_back(ch);
                    break;
                case ParsePhase::Name:
                    cur_desc.name.push_back(ch);
                    break;
                case ParsePhase::Data:
                    cur_desc.data.push_back(ch);
                    break;
                }
            }
            if (cur_in_quotes) {
                failed = true;
                return {};
            }
            // Should transit to ParsePhase::None finally
            if (field_phase != ParsePhase::None) {
                if (field_phase == ParsePhase::Name) {
                    // No `:` detected; field contains only data
                    cur_desc.data = std::move(cur_desc.name);
                    cur_desc.name.clear();
                }
                descs.push_back(cur_desc);
                cur_desc.name.clear();
                cur_desc.data.clear();
                field_phase = ParsePhase::None;
            }
            return descs;
        };
        auto fields = extract_fields_fn(filter_str);
        if (failed) {
            return false;
        }
        auto count_fields_fn = [&](std::wstring_view name) {
            return std::count_if(
                fields.begin(), fields.end(),
                [&](FilterFieldDesc const& v) {
                    return v.name == name;
                }
            );
        };
        if (count_fields_fn(L"d") > 1 || count_fields_fn(L"t") > 1 || count_fields_fn(L"e") > 1) {
            return false;
        }

        // Set up default data
        auto cur_sys_time = std::chrono::system_clock::now();
        std::tm cur_tm = time_point_to_tm(cur_sys_time);
        uint64_t day_start, day_end;    // Unit: day
        day_start = time_point_to_secs_since_epoch(cur_sys_time + m_cur_tz_offset - 15 * 24h) / SECS_PER_DAY;
        day_end = time_point_to_secs_since_epoch(cur_sys_time + m_cur_tz_offset + 15 * 24h) / SECS_PER_DAY;
        uint64_t day_secs_start, day_secs_end;  // Unit: second
        day_secs_start = 0;
        day_secs_end = SECS_PER_DAY;
        std::vector<std::wstring_view> filter_descriptions;
        std::vector<std::wstring_view> filter_titles;
        bool filter_ended_set = false, filter_ended;

        // Extract data from fields
        auto extract_field_date_fn = [&](std::wstring_view data) {
            struct date {
                int y, m, d;
            };
            const date cur_date = { cur_tm.tm_year + 1900, cur_tm.tm_mon + 1, cur_tm.tm_mday };
            date start_date, end_date;
            auto parse_date_fn = [&](std::wstring_view str, date& out_date) {
                std::tm temp_tm{};
                out_date = cur_date;
                int relative_sign;
                // Check for empty string
                if (str == L"") {
                    return true;
                }
                // Check for date format
                bool is_date_fmt;
                switch (str[0]) {
                case L'+':
                    is_date_fmt = true;
                    str = str.substr(1);
                    break;
                case L'-':
                    is_date_fmt = false;
                    relative_sign = -1;
                    str = str.substr(1);
                    break;
                default:
                    relative_sign = 1;
                    is_date_fmt = (str.find(L'/') != str.npos);
                    break;
                }
                // Parse date
                auto try_apply_digits_fn = [](std::wstring_view str, auto fn) {
                    if (auto num = parse_digits_strict(str)) {
                        if (auto int_num = util::num::try_into<int>(*num)) {
                            fn(*int_num);
                        }
                        else {
                            return false;
                        }
                    }
                    else if (str == L"") {
                        // Do nothing
                    }
                    else {
                        // Invalid digits string
                        return false;
                    }
                    return true;
                };
                if (is_date_fmt) {
                    // Date format
                    std::wstring_view::size_type slash_pos1, slash_pos2;
                    slash_pos1 = str.find(L'/');
                    if (slash_pos1 == str.npos) {
                        // Day field only
                        if (!try_apply_digits_fn(str,
                            [&](auto n) {
                                out_date.d = n;
                            }
                        )) {
                            return false;
                        }
                    }
                    else {
                        slash_pos2 = str.rfind(L'/');
                        if (slash_pos2 == slash_pos1) {
                            // Month & day fields
                            if (!try_apply_digits_fn(str.substr(0, slash_pos1),
                                [&](auto n) {
                                    out_date.m = n;
                                }
                            )) {
                                return false;
                            }
                            if (!try_apply_digits_fn(str.substr(slash_pos1 + 1),
                                [&](auto n) {
                                    out_date.d = n;
                                }
                            )) {
                                return false;
                            }
                        }
                        else {
                            // Year & month & day fields
                            if (str.find(L'/', slash_pos1 + 1) != slash_pos2) {
                                // Too many slashes
                                return false;
                            }
                            if (!try_apply_digits_fn(str.substr(0, slash_pos1),
                                [&](auto n) {
                                    out_date.y = n;
                                }
                            )) {
                                return false;
                            }
                            if (!try_apply_digits_fn(str.substr(slash_pos1 + 1, slash_pos2 - slash_pos1 - 1),
                                [&](auto n) {
                                    out_date.m = n;
                                }
                            )) {
                                return false;
                            }
                            if (!try_apply_digits_fn(str.substr(slash_pos2 + 1),
                                [&](auto n) {
                                    out_date.d = n;
                                }
                            )) {
                                return false;
                            }
                        }
                    }
                }
                else {
                    // Relative day format
                    if (!try_apply_digits_fn(str,
                        [&](auto n) {
                            out_date.d = util::num::saturating_add(
                                out_date.d,
                                util::num::saturating_mul(relative_sign, n)
                            );
                        }
                    )) {
                        return false;
                    }
                }
                // Verify & convert date where necessary
                temp_tm.tm_year = out_date.y - 1900;
                temp_tm.tm_mon = out_date.m - 1;
                temp_tm.tm_mday = out_date.d;
                if (is_date_fmt) {
                    // Perform strict check
                    if (!is_tm_in_normal_range(temp_tm)) {
                        return false;
                    }
                }
                else {
                    // Ignore check
                    std::mktime(&temp_tm);
                    out_date.y = temp_tm.tm_year + 1900;
                    out_date.m = temp_tm.tm_mon + 1;
                    out_date.d = temp_tm.tm_mday;
                }
                return true;
            };
            if (auto pos = data.find(L'.'); pos == data.npos) {
                // 1 date
                if (!parse_date_fn(data, start_date)) {
                    return false;
                }
                end_date = start_date;
            }
            else if (pos == data.rfind(L'.')) {
                // 2 dates
                if (!parse_date_fn(data.substr(0, pos), start_date)) {
                    return false;
                }
                if (!parse_date_fn(data.substr(pos + 1), end_date)) {
                    return false;
                }
            }
            else {
                // `.` has occurred multiple times; fail the process
                return false;
            }
            // Flush date information
            auto date_to_days_since_epoch = [&](date const& in_date) {
                std::tm temp_tm{};
                temp_tm.tm_year = in_date.y - 1900;
                temp_tm.tm_mon = in_date.m - 1;
                temp_tm.tm_mday = in_date.d;
                auto sys_clk = std::chrono::system_clock::from_time_t(std::mktime(&temp_tm));
                return time_point_to_secs_since_epoch(sys_clk + m_cur_tz_offset) / SECS_PER_DAY;
            };
            day_start = date_to_days_since_epoch(start_date);
            day_end = date_to_days_since_epoch(end_date);
            if (day_start > day_end) {
                return false;
            }
            return true;
        };
        auto extract_field_time_fn = [&](std::wstring_view data) {
            struct time {
                int h, min;
            };
            const time cur_time = { cur_tm.tm_hour, cur_tm.tm_min };
            time start_time, end_time;
            auto parse_time_fn = [&](std::wstring_view str, time& out_time) {
                out_time = cur_time;
                if (str == L"") {
                    return true;
                }
                auto try_apply_digits_fn = [](std::wstring_view str, auto fn) {
                    if (auto num = parse_digits_strict(str)) {
                        if (auto int_num = util::num::try_into<int>(*num)) {
                            fn(*int_num);
                        }
                        else {
                            return false;
                        }
                    }
                    else if (str == L"") {
                        // Do nothing
                    }
                    else {
                        // Invalid digits string
                        return false;
                    }
                    return true;
                };
                if (auto hyphen_pos = str.find(L'-'); hyphen_pos == str.npos) {
                    // Invalid time string
                    return false;
                }
                else {
                    if (!try_apply_digits_fn(str.substr(0, hyphen_pos),
                        [&](auto n) {
                            out_time.h = n;
                        }
                    )) {
                        return false;
                    }
                    if (!try_apply_digits_fn(str.substr(hyphen_pos + 1),
                        [&](auto n) {
                            out_time.min = n;
                        }
                    )) {
                        return false;
                    }
                }
                if (out_time.h < 0 || out_time.h >= 24) {
                    return false;
                }
                if (out_time.min < 0 || out_time.min >= 60) {
                    return false;
                }
                return true;
            };
            if (auto pos = data.find(L'.'); pos == data.npos) {
                // 1 date
                if (!parse_time_fn(data, start_time)) {
                    return false;
                }
                end_time = start_time;
            }
            else if (pos == data.rfind(L'.')) {
                // 2 times
                if (!parse_time_fn(data.substr(0, pos), start_time)) {
                    return false;
                }
                if (!parse_time_fn(data.substr(pos + 1), end_time)) {
                    return false;
                }
            }
            else {
                // `.` has occurred multiple times; fail the process
                return false;
            }
            // Flush time information
            auto time_to_day_secs = [&](time const& in_time) {
                return in_time.h * SECS_PER_HOUR + in_time.min * SECS_PER_MINUTE;
            };
            day_secs_start = time_to_day_secs(start_time);
            day_secs_end = time_to_day_secs(end_time);
            if (day_secs_start > day_secs_end) {
                return false;
            }
            return true;
        };
        auto extract_field_ended_fn = [&](std::wstring_view data) {
            if (data == L"t" || data == L"T") {
                filter_ended = true;
            }
            else if (data == L"f" || data == L"F") {
                filter_ended = false;
            }
            else {
                return false;
            }
            filter_ended_set = true;
            return true;
        };
        auto extract_field_description_fn = [&](std::wstring_view data) {
            filter_descriptions.push_back(data);
            return true;
        };
        auto extract_field_title_fn = [&](std::wstring_view data) {
            filter_titles.push_back(data);
            return true;
        };
        for (auto const& i : fields) {
            if (i.name == L"d") {
                failed = !extract_field_date_fn(i.data);
            }
            else if (i.name == L"t") {
                failed = !extract_field_time_fn(i.data);
            }
            else if (i.name == L"e") {
                failed = !extract_field_ended_fn(i.data);
            }
            else if (i.name == L"desc") {
                failed = !extract_field_description_fn(i.data);
            }
            else if (i.name == L"") {
                failed = !extract_field_title_fn(i.data);
            }
            else {
                // Unrecognized field; fail the process
                failed = true;
            }
            if (failed) {
                return false;
            }
        }

        // --------------------
        std::vector<RoutineArranger::Core::RoutineDesc> result_routines;

        m_root_pre->get_model()->try_get_routines_from_user_view(
            m_root_pre->get_active_user_id(),
            day_start * SECS_PER_DAY - duration_to_secs(m_cur_tz_offset),
            (day_end + 1) * SECS_PER_DAY - duration_to_secs(m_cur_tz_offset),
            result_routines
        );
        // Routines will be removed where pred returns true
        auto filter_routines_fn = [&](auto pred) {
            result_routines.erase(
                std::remove_if(result_routines.begin(), result_routines.end(), pred),
                result_routines.end()
            );
        };
        // Filter time
        filter_routines_fn([&](RoutineArranger::Core::RoutineDesc const& rd) {
            auto start_time = rd.start_secs_since_epoch + duration_to_secs(m_cur_tz_offset);
            auto day_start_time = start_time % SECS_PER_DAY;
            return !(day_secs_start <= day_start_time && day_start_time <= day_secs_end);
        });
        // Filter is_ended
        filter_routines_fn([&](RoutineArranger::Core::RoutineDesc const& rd) {
            if (!filter_ended_set) {
                return false;
            }
            return rd.is_ended != filter_ended;
        });
        // Filter description
        filter_routines_fn([&](RoutineArranger::Core::RoutineDesc const& rd) {
            return std::any_of(
                filter_descriptions.begin(), filter_descriptions.end(),
                [&](std::wstring_view str) {
                    return rd.description.find(str) == rd.description.npos;
                }
            );
        });
        // Filter title
        filter_routines_fn([&](RoutineArranger::Core::RoutineDesc const& rd) {
            return std::any_of(
                filter_titles.begin(), filter_titles.end(),
                [&](std::wstring_view str) {
                    return rd.name.find(str) == rd.name.npos;
                }
            );
        });

        m_tb_lv_routines_footer.Text(wstrprintf(
            L"找到了 %zu 项日程",
            result_routines.size()
        ));

        // TODO: Make routines add / remove transition more precise
        m_lv_routines.Items().Clear();
        if (result_routines.empty()) {
            return true;
        }
        auto add_routine_item_fn = [&](uint32_t idx, bool is_last) {
            auto lvi = Windows::UI::Xaml::Markup::XamlReader::Load(LR"(
<ListViewItem xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
              Padding="0" Height="50" BorderThickness="0,0,0,1"
              HorizontalContentAlignment="Stretch" VerticalContentAlignment="Stretch"
              BorderBrush="{ThemeResource SystemControlBackgroundBaseLowBrush}">
</ListViewItem>)").as<ListViewItem>();
            lvi.Margin(ThicknessHelper::FromLengths(0, 0, 0, is_last ? 0 : -1));
            auto make_item_content_fn = [&](RoutineArranger::Core::RoutineDesc const& rd) {
                auto fm_symbol = FontFamily(L"Segoe MDL2 Assets");
                auto grid = Grid();
                grid.Name(util::winrt::to_wstring(result_routines[idx].id));
                auto grid_rd1 = RowDefinition();
                grid_rd1.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
                grid.RowDefinitions().Append(grid_rd1);
                auto grid_rd2 = RowDefinition();
                grid_rd2.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
                grid.RowDefinitions().Append(grid_rd2);
                auto grid_cd1 = ColumnDefinition();
                grid_cd1.Width(GridLengthHelper::FromPixels(40));
                grid.ColumnDefinitions().Append(grid_cd1);
                auto grid_cd2 = ColumnDefinition();
                grid_cd2.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
                grid.ColumnDefinitions().Append(grid_cd2);
                auto grid_cd3 = ColumnDefinition();
                grid_cd3.Width(GridLengthHelper::FromPixels(40));
                grid.ColumnDefinitions().Append(grid_cd3);
                auto cb = CheckBox();
                cb.IsChecked(rd.is_ended);
                cb.Margin(ThicknessHelper::FromLengths(10, 0, 0, 0));
                auto cb_handler_gen_fn = [&](bool target_value) {
                    return [=, id = rd.id](IInspectable const&, RoutedEventArgs const&) {
                        RoutineArranger::Core::RoutineDesc temp_rd;
                        if (!m_root_pre->get_model()->try_lookup_routine(
                            m_root_pre->get_active_user_id(),
                            id,
                            &temp_rd
                        )) {
                            throw hresult_error(E_FAIL, L"无法根据日程 ID 找到当前用户的日程");
                        }
                        temp_rd.is_ended = target_value;
                        m_root_pre->get_model()->try_update_routine_from_user_view(
                            m_root_pre->get_active_user_id(),
                            temp_rd
                        );
                    };
                };
                cb.Checked(cb_handler_gen_fn(true));
                cb.Unchecked(cb_handler_gen_fn(false));
                Grid::SetRow(cb, 0);
                Grid::SetColumn(cb, 0);
                Grid::SetRowSpan(cb, 2);
                grid.Children().Append(cb);
                auto tb_name = TextBlock();
                tb_name.Margin(ThicknessHelper::FromLengths(0, 0, 0, 4));
                tb_name.VerticalAlignment(VerticalAlignment::Bottom);
                tb_name.TextLineBounds(TextLineBounds::Tight);
                tb_name.TextTrimming(TextTrimming::CharacterEllipsis);
                tb_name.Text(rd.name);
                Grid::SetRow(tb_name, 0);
                Grid::SetColumn(tb_name, 1);
                grid.Children().Append(tb_name);
                auto tb_time = TextBlock();
                tb_time.Margin(ThicknessHelper::FromLengths(0, 4, 0, 0));
                tb_time.VerticalAlignment(VerticalAlignment::Top);
                tb_time.TextLineBounds(TextLineBounds::Tight);
                tb_time.Foreground(SolidColorBrush(Colors::Gray()));
                auto tb_time_rs_date = Windows::UI::Xaml::Documents::Run();
                tb_time_rs_date.FontSize(11);
                tb_time_rs_date.FontFamily(fm_symbol);
                tb_time_rs_date.Text(L"\xE787  ");      // Calendar
                tb_time.Inlines().Append(tb_time_rs_date);
                auto tb_time_rt_date = Windows::UI::Xaml::Documents::Run();
                auto make_date_str_fn = [&] {
                    using namespace std::chrono_literals;
                    auto given_time = std::chrono::system_clock::time_point{}
                        + rd.start_secs_since_epoch * 1s;
                    std::tm tm_cur = time_point_to_tm(cur_sys_time);
                    std::tm tm_given = time_point_to_tm(given_time);
                    std::wstring day_str;
                    if (tm_cur.tm_year != tm_given.tm_year) {
                        day_str = wstrftime(L"%Y年%#m月%#d日", &tm_given);
                    }
                    else {
                        day_str = wstrftime(L"%#m月%#d日", &tm_given);
                    }
                    switch (tm_given.tm_wday) {
                    case 0:     day_str += L" 星期日";     break;
                    case 1:     day_str += L" 星期一";     break;
                    case 2:     day_str += L" 星期二";     break;
                    case 3:     day_str += L" 星期三";     break;
                    case 4:     day_str += L" 星期四";     break;
                    case 5:     day_str += L" 星期五";     break;
                    case 6:     day_str += L" 星期六";     break;
                    }
                    switch (tm_given.tm_yday - tm_cur.tm_yday) {
                    case -1:    day_str = L"昨天 (" + day_str + L")";     break;
                    case 0:     day_str = L"今天 (" + day_str + L")";     break;
                    case 1:     day_str = L"明天 (" + day_str + L")";     break;
                    }
                    return day_str;
                };
                tb_time_rt_date.Text(make_date_str_fn() + L"  ");
                tb_time.Inlines().Append(tb_time_rt_date);
                auto tb_time_r1 = Windows::UI::Xaml::Documents::Run();
                tb_time_r1.FontSize(11);
                tb_time_r1.FontFamily(fm_symbol);
                tb_time_r1.Text(L"\xE823  ");       // Recent
                tb_time.Inlines().Append(tb_time_r1);
                auto tb_time_r2 = Windows::UI::Xaml::Documents::Run();
                auto time_begin_day_secs = (rd.start_secs_since_epoch + duration_to_secs(m_cur_tz_offset)) % SECS_PER_DAY;
                auto time_end_day_secs = (time_begin_day_secs + rd.duration_secs) % SECS_PER_DAY;
                tb_time_r2.Text(wstrprintf(
                    L"%02" PRIu64 ":%02" PRIu64 " ~ %02" PRIu64 ":%02" PRIu64,
                    time_begin_day_secs / (SECS_PER_HOUR),
                    (time_begin_day_secs % SECS_PER_HOUR) / SECS_PER_MINUTE,
                    time_end_day_secs / (SECS_PER_HOUR),
                    (time_end_day_secs % SECS_PER_HOUR) / SECS_PER_MINUTE
                ));
                tb_time.Inlines().Append(tb_time_r2);
                // Mark if routine comes from public
                if (m_root_pre->get_model()->try_lookup_routine(::winrt::guid{ GUID{} }, rd.id, nullptr)) {
                    auto tb_time_rs = Windows::UI::Xaml::Documents::Run();
                    tb_time_rs.FontWeight(FontWeights::Bold());
                    tb_time_rs.Text(L"  ·  ");
                    tb_time.Inlines().Append(tb_time_rs);
                    auto tb_time_r3 = Windows::UI::Xaml::Documents::Run();
                    tb_time_r3.FontSize(11);
                    tb_time_r3.FontFamily(fm_symbol);
                    tb_time_r3.Text(L"\xE7C3");     // Page
                    tb_time.Inlines().Append(tb_time_r3);
                }
                // Mark if routine comes from / is personal repeating
                if (!std::holds_alternative<std::nullptr_t>(rd.template_options)) {
                    auto tb_time_rs = Windows::UI::Xaml::Documents::Run();
                    tb_time_rs.FontWeight(FontWeights::Bold());
                    tb_time_rs.Text(L"  ·  ");
                    tb_time.Inlines().Append(tb_time_rs);
                    auto tb_time_r4 = Windows::UI::Xaml::Documents::Run();
                    tb_time_r4.FontSize(11);
                    tb_time_r4.FontFamily(fm_symbol);
                    tb_time_r4.Text(L"\xE8EE");     // RepeatAll
                    tb_time.Inlines().Append(tb_time_r4);
                }
                Grid::SetRow(tb_time, 1);
                Grid::SetColumn(tb_time, 1);
                grid.Children().Append(tb_time);
                auto border_clr = Border();
                border_clr.Width(20);
                border_clr.Height(20);
                border_clr.CornerRadius(CornerRadiusHelper::FromUniformRadius(10));
                auto border_clr_bgclr = u32_color_to_winrt(rd.color);
                border_clr.Background(SolidColorBrush(border_clr_bgclr));
                border_clr.BorderThickness(ThicknessHelper::FromUniformLength(1));
                auto contrast_color = get_text_color_from_background(border_clr_bgclr);
                border_clr.BorderBrush(SolidColorBrush(contrast_color));
                Grid::SetRow(border_clr, 0);
                Grid::SetColumn(border_clr, 2);
                Grid::SetRowSpan(border_clr, 2);
                grid.Children().Append(border_clr);
                return grid;
            };
            lvi.Content(make_item_content_fn(result_routines[idx]));
            m_lv_routines.Items().Append(lvi);
        };

        size_t cur_idx = 0;
        for (; cur_idx + 1 < result_routines.size(); cur_idx++) {
            add_routine_item_fn(static_cast<uint32_t>(cur_idx), false);
        }
        add_routine_item_fn(static_cast<uint32_t>(cur_idx), true);

        return true;
    }

    SettingsContainerPresenter::SettingsContainerPresenter(RootContainerPresenter* root) :
        m_root_sv(ScrollViewer()), m_sp(StackPanel()), m_tb_sub_account(TextBlock()),
        m_rb_theme_follow_system(RadioButton()), m_rb_theme_light(RadioButton()), m_rb_theme_dark(RadioButton()),
        m_cb_verify_identity_before_login(CheckBox()), m_tb_verify_identity_before_login_warn(TextBlock()),
        m_root_pre(root)
    {
        auto append_heading_tb_fn = [this](::winrt::hstring const& str) {
            auto tb = TextBlock();
            tb.Text(str);
            tb.Margin(ThicknessHelper::FromLengths(0, 10, 0, 0));
            tb.FontWeight(FontWeights::Bold());
            tb.FontSize(20);
            m_sp.Children().Append(tb);
        };
        auto append_radios_collection_fn = [this](::winrt::hstring const& group_name, auto const&... rbs) {
            auto sp_rbs = StackPanel();
            //sp_rbs.Margin(ThicknessHelper::FromLengths(0, 5, 0, 0));
            sp_rbs.Orientation(Orientation::Horizontal);
            auto add_rb_fn = [&](RadioButton const& rb) {
                rb.GroupName(group_name);
                sp_rbs.Children().Append(rb);
            };
            int dummy[] = { (add_rb_fn(rbs), 0)... };
            m_sp.Children().Append(sp_rbs);
        };
        auto gen_theme_rb_check_handler_fn = [this](RoutineArranger::Core::ThemePreference value) {
            return [=](IInspectable const&, RoutedEventArgs const&) {
                switch (value) {
                case RoutineArranger::Core::ThemePreference::FollowSystem:
                    m_root_pre->update_window_theme(windowing::WindowTheme::FollowSystem);
                    break;
                case RoutineArranger::Core::ThemePreference::Light:
                    m_root_pre->update_window_theme(windowing::WindowTheme::Light);
                    break;
                case RoutineArranger::Core::ThemePreference::Dark:
                    m_root_pre->update_window_theme(windowing::WindowTheme::Dark);
                    break;
                default:
                    throw hresult_error(E_FAIL, L"无法将更新的主题设置理解为窗口主题");
                }
                RoutineArranger::Core::UserDesc user_info;
                if (!m_root_pre->get_model()->try_lookup_user(m_root_pre->get_active_user_id(), user_info)) {
                    throw hresult_error(E_FAIL, L"提交设置时获取用户信息失败");
                }
                user_info.preferences.theme = value;
                if (!m_root_pre->get_model()->try_update_user(user_info)) {
                    throw hresult_error(E_FAIL, L"无法提交用户设置");
                }
            };
        };

        m_sp.Margin(ThicknessHelper::FromLengths(15, 0, 0, 0));
        m_sp.Padding(ThicknessHelper::FromLengths(0, 0, 0, 15));
        append_heading_tb_fn(L"账号");
        m_tb_sub_account.Margin(ThicknessHelper::FromLengths(0, 5, 0, 0));
        m_sp.Children().Append(m_tb_sub_account);
        auto btn_switch_account = Button();
        btn_switch_account.Margin(ThicknessHelper::FromLengths(0, 5, 0, 5));
        btn_switch_account.Content(box_value(L"切换账号"));
        btn_switch_account.Click([this](IInspectable const&, RoutedEventArgs const&) {
            m_root_pre->request_switch_account(true);
        });
        m_sp.Children().Append(btn_switch_account);
        append_heading_tb_fn(L"外观");
        auto tb_sub_theme = TextBlock();
        tb_sub_theme.Margin(ThicknessHelper::FromLengths(0, 5, 0, 0));
        tb_sub_theme.Text(L"主题");
        m_sp.Children().Append(tb_sub_theme);
        m_rb_theme_follow_system.Content(box_value(L"跟随系统"));
        m_rb_theme_follow_system.Checked(
            gen_theme_rb_check_handler_fn(RoutineArranger::Core::ThemePreference::FollowSystem)
        );
        m_rb_theme_light.Content(box_value(L"亮"));
        m_rb_theme_light.Checked(
            gen_theme_rb_check_handler_fn(RoutineArranger::Core::ThemePreference::Light)
        );
        m_rb_theme_dark.Content(box_value(L"暗"));
        m_rb_theme_dark.Checked(
            gen_theme_rb_check_handler_fn(RoutineArranger::Core::ThemePreference::Dark)
        );
        append_radios_collection_fn(
            L"ThemePreference",
            m_rb_theme_follow_system, m_rb_theme_light, m_rb_theme_dark
        );
        append_heading_tb_fn(L"安全");
        auto tb_sub_identity_verify = TextBlock();
        tb_sub_identity_verify.Margin(ThicknessHelper::FromLengths(0, 5, 0, 0));
        tb_sub_identity_verify.Text(L"身份验证");
        m_sp.Children().Append(tb_sub_identity_verify);
        //m_cb_verify_identity_before_login.Margin(ThicknessHelper::FromLengths(0, 5, 0, 0));
        m_cb_verify_identity_before_login.Content(box_value(L"在登录时通过 Windows Hello 验证身份"));
        auto gen_cb_verify_identity_before_login_handler_fn = [this](bool value) {
            return [=](IInspectable const&, RoutedEventArgs const&) {
                RoutineArranger::Core::UserDesc user_info;
                if (!m_root_pre->get_model()->try_lookup_user(m_root_pre->get_active_user_id(), user_info)) {
                    throw hresult_error(E_FAIL, L"提交设置时获取用户信息失败");
                }
                user_info.preferences.verify_identity_before_login = value;
                if (!m_root_pre->get_model()->try_update_user(user_info)) {
                    throw hresult_error(E_FAIL, L"无法提交用户设置");
                }
            };
        };
        m_cb_verify_identity_before_login.Checked(gen_cb_verify_identity_before_login_handler_fn(true));
        m_cb_verify_identity_before_login.Unchecked(gen_cb_verify_identity_before_login_handler_fn(false));
        m_sp.Children().Append(m_cb_verify_identity_before_login);
        //m_tb_verify_identity_before_login_warn.Margin(ThicknessHelper::FromLengths(0, 5, 0, 0));
        m_tb_verify_identity_before_login_warn.Foreground(SolidColorBrush(Colors::Red()));
        m_sp.Children().Append(m_tb_verify_identity_before_login_warn);
        append_heading_tb_fn(L"关于");
        auto tb_sub_about = TextBlock();
        tb_sub_about.Margin(ThicknessHelper::FromLengths(0, 5, 0, 0));
        tb_sub_about.TextWrapping(TextWrapping::WrapWholeWords);
        tb_sub_about.Text(L""
            "日程安排者 v0.1.0\n"
            "这是一份课程设计作业，使用的第三方库为 C++/WinRT。\n"
            "由于第三方库限制，本程序需要使用 Visual Studio 2022 进行编译，并至少使用 C++17 标准。\n"
            "由于系统限制，本程序只能在 Windows 10 1903+ 上运行，并可能存在部分 BUG，敬请谅解。\n"
            "作者: apkipa"
        );
        m_sp.Children().Append(tb_sub_about);

        m_root_sv.IsScrollInertiaEnabled(true);
        m_root_sv.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        m_root_sv.VerticalScrollMode(ScrollMode::Auto);
        m_root_sv.HorizontalScrollMode(ScrollMode::Disabled);
        m_root_sv.Content(m_sp);
    }
    UIElement SettingsContainerPresenter::get_root() {
        return m_root_sv;
    }
    void SettingsContainerPresenter::refresh_content(bool user_changed) {
        // TODO: Add user day view preference?
        RoutineArranger::Core::UserDesc user_info;
        if (!m_root_pre->get_model()->try_lookup_user(m_root_pre->get_active_user_id(), user_info)) {
            throw hresult_error(E_FAIL, L"刷新设置页面时获取用户信息失败");
        }
        m_tb_sub_account.Text(wstrprintf(
            L"当前已作为 `%ls` (%ls) 登录。%ls",
            user_info.nickname.c_str(), user_info.name.c_str(),
            user_info.is_admin ? L"\n您是管理员。" : L""
        ));
        switch (user_info.preferences.theme) {
        case RoutineArranger::Core::ThemePreference::FollowSystem:
            m_rb_theme_follow_system.IsChecked(true);
            break;
        case RoutineArranger::Core::ThemePreference::Light:
            m_rb_theme_light.IsChecked(true);
            break;
        case RoutineArranger::Core::ThemePreference::Dark:
            m_rb_theme_dark.IsChecked(true);
            break;
        default:
            throw hresult_error(E_FAIL, L"无法理解用户的主题设置");
        }
        m_cb_verify_identity_before_login.IsChecked(user_info.preferences.verify_identity_before_login);
        [](auto copied_this) -> fire_forget_except {
            using namespace Windows::Security::Credentials::UI;
            hstring warn_msg;
            try {
                switch (co_await UserConsentVerifier::CheckAvailabilityAsync()) {
                case UserConsentVerifierAvailability::Available:
                    copied_this->m_cb_verify_identity_before_login.IsEnabled(true);
                    copied_this->m_tb_verify_identity_before_login_warn.Visibility(Visibility::Collapsed);
                    co_return;
                case UserConsentVerifierAvailability::DeviceBusy:
                    warn_msg = L"身份验证设备正忙，无法使用。";
                    break;
                case UserConsentVerifierAvailability::DeviceNotPresent:
                    warn_msg = L"无法在你的电脑上找到任何身份验证设备。";
                    break;
                case UserConsentVerifierAvailability::DisabledByPolicy:
                    warn_msg = L"你的组织已通过策略禁用了身份验证设备。";
                    break;
                case UserConsentVerifierAvailability::NotConfiguredForUser:
                    warn_msg = L"找到了身份验证设备，但尚未为当前用户配置为可用。";
                    break;
                default:
                    warn_msg = L"获取 Windows Hello 状态时发生了未知错误。";
                    break;
                }
            }
            catch (hresult_error const& e) {
                warn_msg = wstrprintf(
                    L"获取 Windows Hello 状态时发生了未知错误。(0x%08x: %ls)",
                    static_cast<uint32_t>(e.code()),
                    e.message().c_str()
                );
            }
            copied_this->m_cb_verify_identity_before_login.IsEnabled(false);
            copied_this->m_tb_verify_identity_before_login_warn.Visibility(Visibility::Visible);
            copied_this->m_tb_verify_identity_before_login_warn.Text(warn_msg);
        }(this);
    }

    RootContainerPresenter::RootContainerPresenter(RoutineArranger::Core::CoreAppModel model, windowing::XamlWindow* wnd) :
        m_root_container(StackedContentControlHelper(true)), m_nav_container(StackedContentControlHelper(true)),
        m_nav_view(NavigationView()), m_root_border(Border()),
        m_model(model), m_wnd(wnd), m_active_user_id(GUID{}), m_last_active_user_changed(false),
        m_account_mgr_pre(model), m_day_view_pre(this), m_month_view_pre(this),
        m_search_pre(this), m_settings_pre(this)
    {
        {
            auto fe = m_root_container.get_root().as<FrameworkElement>();
            fe.ActualThemeChanged([=](FrameworkElement const& sender, IInspectable const&) {
                if (sender.ActualTheme() == Windows::UI::Xaml::ElementTheme::Dark) {
                    m_root_border.Background(SolidColorBrush(Colors::Black()));
                }
                else {
                    m_root_border.Background(SolidColorBrush(Colors::White()));
                }
            });
        }

        auto nav_item_day_view = NavigationViewItem();
        nav_item_day_view.Icon(SymbolIcon(Symbol::CalendarDay));
        nav_item_day_view.Content(box_value(L"日视图"));
        auto nav_item_month_view = NavigationViewItem();
        nav_item_month_view.Icon(SymbolIcon(Symbol::Calendar));
        nav_item_month_view.Content(box_value(L"月视图"));
        auto nav_item_search_view = NavigationViewItem();
        nav_item_search_view.Icon(SymbolIcon(Symbol::Find));
        nav_item_search_view.Content(box_value(L"搜索"));

        m_nav_view.IsBackButtonVisible(NavigationViewBackButtonVisible::Collapsed);
        m_nav_view.IsPaneOpen(false);
        m_nav_view.PaneDisplayMode(NavigationViewPaneDisplayMode::LeftCompact);
        m_nav_view.MenuItems().ReplaceAll({ nav_item_day_view, nav_item_month_view, nav_item_search_view });
        m_nav_view.SelectionChanged([=](NavigationView const&, NavigationViewSelectionChangedEventArgs const& e) {
            if (e.IsSettingsSelected()) {
                m_settings_pre.refresh_content(m_last_active_user_changed);
                m_nav_container.navigate_to(m_settings_pre.get_root(), true);
            }
            else {
                auto selected_item = e.SelectedItem();
                if (selected_item == nullptr) {
                    // Ignore the operation
                    return;
                }
                if (selected_item == nav_item_day_view) {
                    m_day_view_pre.refresh_content(m_last_active_user_changed);
                    m_nav_container.navigate_to(m_day_view_pre.get_root(), true);
                }
                else if (selected_item == nav_item_month_view) {
                    m_month_view_pre.refresh_content(m_last_active_user_changed);
                    m_nav_container.navigate_to(m_month_view_pre.get_root(), true);
                }
                else if (selected_item == nav_item_search_view) {
                    m_search_pre.refresh_content(m_last_active_user_changed);
                    m_nav_container.navigate_to(m_search_pre.get_root(), true);
                }
                else {
                    throw hresult_error(E_FAIL, L"用户选中了不存在的导航项");
                }
            }
            m_last_active_user_changed = false;
        });
        m_nav_view.Content(m_nav_container.get_root());

        m_root_container.navigate_to(m_nav_view);

        [&]() -> fire_forget_except {
            co_safe_capture(nav_item_day_view);
            auto copied_this = this;
            if (!co_await copied_this->request_switch_account(false)) {
                throw hresult_error(E_FAIL, L"第一次选择账号时不应出现错误, 但实际出现了");
            }
            copied_this->m_nav_view.SelectedItem(nav_item_day_view);
        }();

        m_root_border.Child(m_root_container.get_root());
    }
    UIElement RootContainerPresenter::get_root() {
        //return m_root_container.get_root();
        return m_root_border;
    }
    RoutineArranger::Core::CoreAppModel RootContainerPresenter::get_model(void) {
        return m_model;
    }
    ::winrt::guid RootContainerPresenter::get_active_user_id(void) {
        return m_active_user_id;
    }
    bool RootContainerPresenter::jump_to_day_view_routine(::winrt::guid routine_id) {
        // Switch to DayView
        m_nav_view.SelectedItem(m_nav_view.MenuItems().GetAt(0));
        // Jump to routine
        return m_day_view_pre.jump_to_routine(routine_id);
    }
    IAsyncOperation<bool> RootContainerPresenter::request_switch_account(bool user_cancellable) {
        static const ::winrt::guid zero_guid = GUID{};

        m_root_container.navigate_to(m_account_mgr_pre.get_root());
        auto result_guid = co_await m_account_mgr_pre.start(user_cancellable);
        if (result_guid == zero_guid) {
            m_root_container.go_back();
            co_return false;
        }
        m_active_user_id = result_guid;
        m_last_active_user_changed = true;

        // Refresh last selection to trigger UI updates
        auto last_selected_item = m_nav_view.SelectedItem();
        m_nav_view.SelectedItem(nullptr);
        m_nav_view.SelectedItem(last_selected_item);
        // Workaround NavigationView pane glitches when expanded
        m_nav_view.IsPaneOpen(true);
        m_nav_view.IsPaneOpen(false);

        RoutineArranger::Core::UserDesc user_info;
        if (!m_model->try_lookup_user(result_guid, user_info)) {
            throw hresult_error(E_FAIL, L"无法查询登录过程返回的用户信息");
        }
        m_wnd->set_window_title(user_info.nickname.c_str());
        switch (user_info.preferences.theme) {
        case RoutineArranger::Core::ThemePreference::FollowSystem:
            m_wnd->window_theme(windowing::WindowTheme::FollowSystem);
            break;
        case RoutineArranger::Core::ThemePreference::Light:
            m_wnd->window_theme(windowing::WindowTheme::Light);
            break;
        case RoutineArranger::Core::ThemePreference::Dark:
            m_wnd->window_theme(windowing::WindowTheme::Dark);
            break;
        default:
            throw hresult_error(E_FAIL, L"无法将用户的主题设置理解为窗口主题");
        }

        m_root_container.go_back();

        co_return true;
    }
    void RootContainerPresenter::update_window_theme(windowing::WindowTheme theme) {
        m_wnd->window_theme(theme);
    }
}
