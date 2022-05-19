#pragma once

#include "RoutineArranger.h"

#include <fstream>
#include "json.h"

namespace RoutineArranger {
    namespace Core {
#define internal_define_arc_type(type)                  \
    namespace implementation { struct type; }
#define internal_export_arc_type(type)                  \
    internal_define_arc_type(type);                     \
    using type = std::shared_ptr<implementation::type>

        // Model: Acts as basic data objects (for example, searching is not handled)
        internal_export_arc_type(CoreAppModel);

#undef internal_export_arc_type
#undef internal_define_arc_type

        enum class RoutineArrangerResultErrorKind {
            Ok = 0,
            StorageNotAccessible = -1,
            StorageCorrupted = -2,
        };
        inline bool is_result_success(RoutineArrangerResultErrorKind kind) {
            return kind == RoutineArrangerResultErrorKind::Ok;
        }

        enum ThemePreference {
            FollowSystem,
            Light,
            Dark,
        };
        struct UserPreferences {
            bool day_view_prefer_timeline;
            ThemePreference theme;
            bool verify_identity_before_login;
        };
        struct UserDesc {
            ::winrt::guid id;
            std::wstring name;
            std::wstring nickname;
            bool is_admin;
            uint64_t last_routines_update_ts;
            UserPreferences preferences;
        };
        // Bitmask
        enum RoutineEndTriggerKind {
            Manual = 0x0,
            Expiry = 0x1,
        };
        enum RoutineTemplateKind {
            Normal = 1,
            Repeating,
            Derived,    // Derived from a template
        };
        struct RoutineDescTemplate_Repeating {
            uint32_t repeat_days_cycle;
            uint32_t repeat_cycles;
            std::vector<bool> repeat_days_flags;
        };
        struct RoutineDescTemplate_Derived {
            ::winrt::guid source_routine;
        };
        // NOTE: Should NOT be identical to JSON data
        struct RoutineDesc {
            ::winrt::guid id;
            uint64_t start_secs_since_epoch;
            uint64_t duration_secs;
            // NOTE: Ghost routines are generated on the fly and will not
            //       be stored unless modified
            bool is_ghost;
            std::wstring name;
            std::wstring description;
            uint32_t color;
            RoutineEndTriggerKind end_trigger_kind;
            bool is_ended;

            // NOTE: For hint only
            //RoutineTemplateKind template_kind;
            std::variant<
                std::nullptr_t,
                RoutineDescTemplate_Repeating,
                RoutineDescTemplate_Derived
            > template_options;
        };

        struct implementation::CoreAppModel {
            CoreAppModel();
            ~CoreAppModel();

            // NOTE: path: A folder where app data are stored
            // NOTE: This method by default updates self data from the storage.
            //       If this is undesired behavior, set write_only to true.
            // WARN: This method flushes data to previously connected storage and
            //       ignores any errors. For robustness, manually sync before
            //       connecting to another storage.
            RoutineArrangerResultErrorKind try_connect_storage(const wchar_t* path, bool write_only = false);
            // NOTE: This method only flushes data to disk.
            // WARN: [NOT FAIL-SAFE] If this method returns false, the underlying
            //       files have a good chance of being CORRUPTED. In such case,
            //       disconnect the storage, then try to recover from backup files.
            bool try_flush_storage(void);
            const wchar_t* get_current_storage_path(void);

            /*
            * NOTE:
            * Storage:
            *     StorageRoot |- index.cfg (all users' account data & preferences)
            *                 |- routines.cfg (all users' existing routines)
            *                 |- attachments (folder, ???, may be reserved for image attachments)
            * User: name(unique, ascii only), nickname(display only)
            * User can either be admin or normal user (bool is_admin).
            * add_user(const wchar_t* name, const wchar_t* nickname, bool is_admin)
            * User ids are seen as an identifier for resources.
            * Routines belong to users, and admin can create routines applicable
            *     to all users.
            * get_all_routines() returns all existing routines for a given user.
            * get_range_routine() returns all routines (including ghost ones)
            *     within a given range (useful for UI-related operations).
            * Routines are identified with guids.
            * User routines can derive from public templates(normal / repeating)
            *     or personal templates(repeating).
            * PublicRepeating -> PersonalRepeating (same id, ghost) -> PersonalDerived (ghost)
            * Users should not be permitted to modify derived routines
            * If ghost routines are edited, they will become concrete (similar to cow)
            */
            /*
            * index.cfg:
            * {
            *     // Version is always 1
            *     "version": 1,
            *     "users": [
            *         {
            *             "id": "b555a2be-7a53-42cb-b71f-31953edce43e",
            *             "name": "user1",
            *             "nickname": "成员 1",
            *             "is_admin": true,
            *             "verify_identity_before_login": false,
            *             "last_routines_update_ts": 1648279654,
            *             "preferences": {
            *                 "day_view_prefer_timeline": true
            *                 "theme": "system" <OR> "light" <OR> "dark"
            *             }
            *         }
            *     ]
            * }
            * routines.cfg:
            * {
            *     "public": [
            *         // <snip>
            *         // Public routines can only be non-derived ones and
            *         // do not have owners
            *     ],
            *     "personal": {
            *         "b555a2be-7a53-42cb-b71f-31953edce43e": [
            *             {
            *                 "id": "a9febbbc-4fac-40d3-a6d2-b2152e14cf3e",
            *                 "start_secs_since_epoch": 1648279631,
            *                 "duration_secs": 3600,
            *                 "name": "测试事项 1",
            *                 "description": "这个重复事项用于测试程序的功能。",
            *                 "color": 16711680,        // #ff0000
            *                 "end_trigger_kind": 1,    // Bitmask; see above
            *                 "is_ended": false,
            *                 "template_options": null  // This is a normal routine
            *                 <OR> {   // This is a repeating routine
            *                     // Repeats every week
            *                     "repeat_days_cycle": 7,
            *                     // Repeat infinite times
            *                     "repeat_cycles": 0,
            *                     // If this routine starts on Tuesday, it
            *                     // will repeat every Thursday and Friday
            *                     "repeat_days_flags": [ 0, 0, 1, 1, 0, 0, 0 ]
            *                 } <OR> {  // This is a routine derived from a repeating one
            *                     "source_routine": "a9febbbc-4fac-40d3-a6d2-b2152e14cf3e"
            *                 }
            *             }
            *         ]
            *     }
            * }
            */

            const std::vector<UserDesc>& get_users() { return m_users; }
            bool create_user(const wchar_t* name, const wchar_t* nickname, bool is_admin);
            bool try_lookup_user(::winrt::guid user_id, UserDesc& desc);
            // NOTE: User name will be ignored during the update
            bool try_update_user(UserDesc const& desc);
            // NOTE: Routines which belong to the user will also be removed
            bool try_remove_user(::winrt::guid user_id);

            // NOTE: No authentication is applied here; user_id may be empty
            //       (in which case the public routines will be searched)
            bool try_lookup_routine(::winrt::guid user_id, ::winrt::guid routine_id, RoutineDesc* routine);
            bool try_get_routines_from_user_view(
                ::winrt::guid user_id,
                uint64_t secs_since_epoch_start,
                uint64_t secs_since_epoch_end,
                std::vector<RoutineDesc>& routines
            );
            bool try_update_routine_from_user_view(
                ::winrt::guid user_id,
                RoutineDesc const& routine
            );
            bool try_remove_routine_from_user_view(::winrt::guid user_id, ::winrt::guid routine_id);
            void update_public_routine(RoutineDesc const& routine);
            bool try_remove_public_routine(::winrt::guid routine_id);
        private:
            std::wstring m_storage_path;
            std::fstream m_file_lock;
            bool m_index_cfg_need_flush, m_routines_cfg_need_flush;

            std::vector<UserDesc> m_users;
            std::vector<RoutineDesc> m_routines_public;
            std::map<::winrt::guid, std::vector<RoutineDesc>> m_routines_personal;
        };
    }
}
