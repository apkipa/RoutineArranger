#pragma once

#include "RoutineArranger.h"
#include "RoutineArranger_Core.h"

#include "windowing.h"

#include <memory>

namespace RoutineArranger {
    namespace UI {
#define internal_define_arc_type(type)                  \
    namespace implementation { struct type; }
#define internal_export_arc_type(type)                  \
    internal_define_arc_type(type);                     \
    using type = std::shared_ptr<implementation::type>

        internal_export_arc_type(StackedContentControlHelper);
        // ContainerPresenter: Presents a view in a UI container; process view business and
        //                     communicates with the model
        internal_export_arc_type(RootContainerPresenter);
        // Internal types are not exposed to (or used by) user, so no need to export wrapped types
        internal_define_arc_type(UserAccountManageContainerPresenter);
        internal_define_arc_type(DayViewContainerPresenter);
        internal_define_arc_type(MonthViewContainerPresenter);
        internal_define_arc_type(SearchContainerPresenter);
        internal_define_arc_type(SettingsContainerPresenter);

#undef internal_export_arc_type
#undef internal_define_arc_type

        namespace implementation {
            using namespace ::winrt;
            using namespace Windows::Foundation;
            using Windows::Foundation::IInspectable;
        }

        // Implementation declarations
        struct implementation::StackedContentControlHelper {
            StackedContentControlHelper(bool enable_transition = false);
            Windows::UI::Xaml::UIElement get_root();
            void navigate_to(IInspectable const& ins, bool clear_stack = false);
            bool go_back(void);
            bool can_go_back(void);
            bool enable_transition(bool new_value);
        private:
            bool m_enable_transition;
            Windows::UI::Xaml::Controls::ContentControl m_root_ctrl;
            std::vector<IInspectable> m_nav_stack;
            bool m_resize_removed_transition;
            Windows::UI::Xaml::Controls::ContentControl::SizeChanged_revoker m_event_revoker;
        };

        struct implementation::UserAccountManageContainerPresenter {
            UserAccountManageContainerPresenter(RoutineArranger::Core::CoreAppModel model);
            Windows::UI::Xaml::UIElement get_root();

            // Starts the login process, returns after user selected an account
            // NOTE: Zero guid indicates failure
            IAsyncOperation<::winrt::guid> start(bool user_cancellable);
        private:
            void update_users_list(void);

            Windows::UI::Xaml::Controls::Grid m_root_grid;
            Windows::UI::Xaml::Controls::ListView m_lv_accounts;
            Windows::UI::Xaml::Controls::StackPanel m_sp_adduser;
            Windows::UI::Xaml::Controls::AppBarButton m_abtn_back, m_abtn_close;
            Windows::UI::Xaml::Controls::TextBox m_tb_adduser_name, m_tb_adduser_nickname;
            Windows::UI::Xaml::Controls::CheckBox m_cb_adduser_admin;

            RoutineArranger::Core::CoreAppModel m_model;
        };

        struct implementation::DayViewContainerPresenter {
            DayViewContainerPresenter(RootContainerPresenter* root);
            Windows::UI::Xaml::UIElement get_root();
            void refresh_content(bool user_changed);
            bool jump_to_routine(::winrt::guid routine_id);
        private:
            void update_cur_day_routines_ui_item(uint32_t idx);
            void update_cur_day_routines(void);
            void update_cur_routine_details(void);
            void flush_cur_routine_details(void);

            Windows::UI::Xaml::Controls::SplitView m_root_splitview;
            Windows::UI::Xaml::Controls::Grid m_grid_day;
            Windows::UI::Xaml::Controls::TextBlock m_tb_day_top;
            Windows::UI::Xaml::Controls::ListView m_lv_routines;
            Windows::UI::Xaml::Controls::TextBlock m_tb_routines_placeholder;
            Windows::UI::Xaml::Controls::TextBox m_tb_day_bottom_name;
            Windows::UI::Xaml::Controls::Grid m_grid_details_root;
            Windows::UI::Xaml::Controls::ScrollViewer m_sv_details;
            Windows::UI::Xaml::Controls::CheckBox m_cb_details_ended;
            Windows::UI::Xaml::Controls::TextBox m_tb_details_title;
            Windows::UI::Xaml::Controls::Border m_border_details_info;
            Windows::UI::Xaml::Controls::TextBlock m_tb_details_info;
            Windows::UI::Xaml::Controls::TimePicker m_tp_details_start;
            Windows::UI::Xaml::Controls::TimePicker m_tp_details_end;
            Windows::UI::Xaml::Controls::Button m_btn_details_color_selection;
            Windows::UI::Xaml::Controls::Border m_border_clr_details_color_selection;
            Windows::UI::Xaml::Controls::TextBlock m_tb_details_color_selection;
            Windows::UI::Xaml::Controls::Border m_border_details_repeating;
            Windows::UI::Xaml::Controls::ComboBox m_cb_details_repeat_type;
            Windows::UI::Xaml::Controls::Grid m_grid_details_repeat_by_day;
            Windows::UI::Xaml::Controls::TextBox m_tb_details_repeat_by_day_days;
            Windows::UI::Xaml::Controls::TextBox m_tb_details_repeat_by_day_cycles;
            Windows::UI::Xaml::Controls::StackPanel m_sp_details_repeat_by_day_sel_flags;
            Windows::UI::Xaml::Controls::TextBox m_tb_details_description;
            Windows::UI::Xaml::Controls::Button m_btn_details_delete;
            Windows::UI::Xaml::Controls::TextBlock m_tb_details_placeholder;

            Windows::UI::Xaml::Controls::TextBox::LosingFocus_revoker m_rvk_tb_details_title_losing_focus;
            Windows::UI::Xaml::Controls::TextBox::LosingFocus_revoker m_rvk_tb_details_description_losing_focus;
            Windows::UI::Xaml::Controls::TextBox::LosingFocus_revoker m_rvk_tb_details_repeat_by_day_days_losing_focus;
            Windows::UI::Xaml::Controls::TextBox::LosingFocus_revoker m_rvk_tb_details_repeat_by_day_cycles_losing_focus;

            RootContainerPresenter* m_root_pre;

            std::chrono::system_clock::time_point m_cur_time;
            uint64_t m_day_offset;
            std::vector<RoutineArranger::Core::RoutineDesc> m_cur_routines;
            Windows::UI::Color m_cur_editing_color;
            IReference<TimeSpan> m_cur_editing_time_start;
            IReference<TimeSpan> m_cur_editing_time_end;
            std::chrono::minutes m_cur_tz_offset;
        };

        struct implementation::MonthViewContainerPresenter {
            MonthViewContainerPresenter(RootContainerPresenter* root);
            Windows::UI::Xaml::UIElement get_root();
            void refresh_content(bool user_changed);
        private:
            void CalendarView_DayItemChanging(
                Windows::UI::Xaml::Controls::CalendarView const& sender,
                Windows::UI::Xaml::Controls::CalendarViewDayItemChangingEventArgs const& e
            );
            void update_calendar_view_day_item(
                Windows::UI::Xaml::Controls::CalendarViewDayItem const& item
            );
            void update_day_routines_ui_item(uint32_t idx);
            void update_day(Windows::Foundation::DateTime const& dt);

            Windows::UI::Xaml::Controls::SplitView m_root_splitview;
            Windows::UI::Xaml::Controls::CalendarView m_calendar_view;
            Windows::UI::Xaml::Controls::Grid m_grid_day_root;
            Windows::UI::Xaml::Controls::Grid m_grid_day;
            Windows::UI::Xaml::Controls::TextBlock m_tb_day_top;
            Windows::UI::Xaml::Controls::ListView m_lv_day_routines;
            Windows::UI::Xaml::Controls::TextBlock m_tb_day_routines_placeholder;
            Windows::UI::Xaml::Controls::TextBlock m_tb_day_placeholder;

            RootContainerPresenter* m_root_pre;

            std::vector<RoutineArranger::Core::RoutineDesc> m_cur_routines;
            std::chrono::minutes m_cur_tz_offset;
        };

        struct implementation::SearchContainerPresenter {
            SearchContainerPresenter(RootContainerPresenter* root);
            Windows::UI::Xaml::UIElement get_root();
            void refresh_content(bool user_changed);
        private:
            void SearchBox_TextChanged(
                Windows::UI::Xaml::Controls::AutoSuggestBox const& sender,
                Windows::UI::Xaml::Controls::AutoSuggestBoxTextChangedEventArgs const& e
            );
            void show_input_error(bool show);
            bool update_search_results(std::wstring_view filter_str);

            Windows::UI::Xaml::Controls::Grid m_root_grid;
            Windows::UI::Xaml::Controls::Grid m_grid_search_box_bg;
            Windows::UI::Xaml::Controls::AutoSuggestBox m_search_box;
            Windows::UI::Xaml::Controls::ListView m_lv_routines;
            Windows::UI::Xaml::Controls::TextBlock m_tb_lv_routines_footer;

            RootContainerPresenter* m_root_pre;

            std::chrono::minutes m_cur_tz_offset;
        };

        struct implementation::SettingsContainerPresenter {
            SettingsContainerPresenter(RootContainerPresenter* root);
            Windows::UI::Xaml::UIElement get_root();
            void refresh_content(bool user_changed);
        private:
            Windows::UI::Xaml::Controls::ScrollViewer m_root_sv;
            Windows::UI::Xaml::Controls::StackPanel m_sp;
            Windows::UI::Xaml::Controls::TextBlock m_tb_sub_account;
            Windows::UI::Xaml::Controls::RadioButton m_rb_theme_follow_system;
            Windows::UI::Xaml::Controls::RadioButton m_rb_theme_light;
            Windows::UI::Xaml::Controls::RadioButton m_rb_theme_dark;
            Windows::UI::Xaml::Controls::CheckBox m_cb_verify_identity_before_login;
            Windows::UI::Xaml::Controls::TextBlock m_tb_verify_identity_before_login_warn;

            RootContainerPresenter* m_root_pre;
        };

        struct implementation::RootContainerPresenter {
            RootContainerPresenter(RoutineArranger::Core::CoreAppModel model, windowing::XamlWindow* wnd);
            Windows::UI::Xaml::UIElement get_root();
            RoutineArranger::Core::CoreAppModel get_model(void);
            ::winrt::guid get_active_user_id(void);

            bool jump_to_day_view_routine(::winrt::guid routine_id);

            // Settings presenter should have full access
            friend SettingsContainerPresenter;
        private:
            IAsyncOperation<bool> request_switch_account(bool user_cancellable);
            void update_window_theme(windowing::WindowTheme theme);

            StackedContentControlHelper m_root_container;
            StackedContentControlHelper m_nav_container;
            Windows::UI::Xaml::Controls::NavigationView m_nav_view;
            Windows::UI::Xaml::Controls::Border m_root_border;

            RoutineArranger::Core::CoreAppModel m_model;
            windowing::XamlWindow* m_wnd;
            ::winrt::guid m_active_user_id;
            bool m_last_active_user_changed;

            UserAccountManageContainerPresenter m_account_mgr_pre;
            DayViewContainerPresenter m_day_view_pre;
            MonthViewContainerPresenter m_month_view_pre;
            SearchContainerPresenter m_search_pre;
            SettingsContainerPresenter m_settings_pre;
        };
    }
}
