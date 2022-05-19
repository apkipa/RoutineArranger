#include "pch.h"

#include <algorithm>
#include <cwctype>

#include "RoutineArranger_Core.h"
#include "util.h"

const uint64_t SECS_PER_DAY = 60 * 60 * 24;

template<typename Container, typename T, typename Pred>
void ordered_insert(Container& c, T&& v, Pred pred) {
    // TODO: Is UB possible if the next two lines are combined?
    auto it = std::lower_bound(c.begin(), c.end(), v, pred);
    c.insert(it, std::forward<T>(v));
}

namespace RoutineArranger::Core::implementation {
    bool pred_routine_desc_less_than(RoutineDesc const& a, RoutineDesc const& b) {
        return a.start_secs_since_epoch < b.start_secs_since_epoch;
    }
    bool pred_routine_is_ghost(RoutineDesc const& v) {
        return v.is_ghost;
    }
    bool pred_derived_routines_in_same_day(RoutineDesc const& a, RoutineDesc const& b) {
        auto derived_a = std::get_if<RoutineDescTemplate_Derived>(&a.template_options);
        auto derived_b = std::get_if<RoutineDescTemplate_Derived>(&b.template_options);
        // Do not take non-derived routines into account
        if (!(derived_a && derived_b)) {
            return false;
        }
        if (derived_a->source_routine != derived_b->source_routine) {
            return false;
        }
        uint64_t ta = a.start_secs_since_epoch / SECS_PER_DAY;
        uint64_t tb = b.start_secs_since_epoch / SECS_PER_DAY;
        if (ta != tb) {
            return false;
        }
        return true;
    }

    CoreAppModel::CoreAppModel() :
        m_storage_path(L""), m_file_lock(), m_index_cfg_need_flush(false), m_routines_cfg_need_flush(false),
        m_users(), m_routines_public(), m_routines_personal()
    {}
    CoreAppModel::~CoreAppModel() {
        // Sync & disconnect storage if required
        this->try_flush_storage();
    }
    RoutineArrangerResultErrorKind CoreAppModel::try_connect_storage(const wchar_t* path, bool write_only) {
        // Success, or the original connection will remain unchanged

        if (*path == L'\0') {   // path == L""
            // Connect to nothing (disconnect existing storage)
            m_storage_path = L"";
            this->try_flush_storage();
            m_file_lock.close();
            if (!write_only) {
                // Reading from nothing is the same as clearing data
                m_users.clear();
                m_routines_public.clear();
                m_routines_personal.clear();
            }
            return RoutineArrangerResultErrorKind::Ok;
        }

        // Helper functions
        auto open_cfg_file_fn = [&](const wchar_t* cfg_name, std::fstream& file) {
            // Assuming parameter file is NOT open
            std::wstring cfg_path = std::wstring{ path } + L"/" + cfg_name;
            if (!util::fs::path_exists(cfg_path.c_str())) {
                std::fstream temp_file;
                temp_file.open(cfg_path, std::ios::out | std::ios::binary);
                if (!temp_file.is_open()) {
                    return false;
                }
            }

            // NOTE: Microsoft-C++-specific syntax for exclusive file access
            file.open(cfg_path, std::ios::in | std::ios::out | std::ios::binary, _SH_DENYRW);
            if (!file.is_open()) {
                return false;
            }

            if (file.peek() == std::fstream::traits_type::eof()) {
                // File is empty; initialize file with `{}`
                file.clear();
                file.write("{}", 2);
                file.flush();
                file.seekg(0, std::ios::beg);
            }

            return true;
        };
        auto parse_json_from_file = [](std::fstream& file, json::JsonObject& jo) {
            std::vector<char> data{ std::istreambuf_iterator(file), std::istreambuf_iterator<char>() };
            if (file.fail()) {
                return false;
            }
            auto json_value = json::JsonValue();
            if (!json_value.try_deserialize_from_utf8(data)) {
                return false;
            }
            if (!json_value.is_object()) {
                return false;
            }
            jo = json_value.get<json::JsonObject>();
            return true;
        };

        // Try to acquire lock
        std::fstream file_lock;
        file_lock.open(
            std::wstring{ path } + L"/.lockfile",
            std::ios::in | std::ios::out | std::ios::trunc | std::ios::binary,
            _SH_DENYRW
        );
        if (!file_lock.is_open()) {
            return RoutineArrangerResultErrorKind::StorageNotAccessible;
        }

        // Try to connect to storage and parse data
        std::fstream index_cfg, routines_cfg;
        bool index_cfg_need_flush = false, routines_cfg_need_flush = false;
        if (!open_cfg_file_fn(L"index.cfg", index_cfg)) {
            return RoutineArrangerResultErrorKind::StorageNotAccessible;
        }
        if (!open_cfg_file_fn(L"routines.cfg", routines_cfg)) {
            return RoutineArrangerResultErrorKind::StorageNotAccessible;
        }

        // Short-circuit immediately if user does not want to read data
        if (write_only) {
            this->try_flush_storage();
            m_storage_path = path;
            m_index_cfg_need_flush = true;
            m_routines_cfg_need_flush = true;
            return RoutineArrangerResultErrorKind::Ok;
        }

        json::JsonObject index_jo, routines_jo;
        if (!parse_json_from_file(index_cfg, index_jo)) {
            return RoutineArrangerResultErrorKind::StorageCorrupted;
        }
        if (!parse_json_from_file(routines_cfg, routines_jo)) {
            return RoutineArrangerResultErrorKind::StorageCorrupted;
        }

        // Initialize json data if they are empty
        if (index_jo.empty()) {
            index_jo[L"version"] = 1;
            index_jo[L"users"] = json::JsonArray{};
            index_cfg_need_flush = true;
        }
        if (routines_jo.empty()) {
            routines_jo[L"public"] = json::JsonArray{};
            routines_jo[L"personal"] = json::JsonObject{};
            routines_cfg_need_flush = true;
        }

        // Verify and extract json data
        // TODO: Silently merge routines that have the same start time (?)
        std::vector<UserDesc> users;
        std::vector<RoutineDesc> routines_public;
        std::map<::winrt::guid, std::vector<RoutineDesc>> routines_personal;

        auto parse_index_jo_fn = [&] {
            try {
                if (index_jo[L"version"].get_value<int>() != 1) {
                    return false;
                }
                for (auto& i : index_jo[L"users"].get<json::JsonArray>()) {
                    UserDesc user;
                    user.id = util::winrt::to_guid(i[L"id"].get<std::wstring>());
                    user.name = i[L"name"].get<std::wstring>();
                    user.nickname = i[L"nickname"].get<std::wstring>();
                    user.is_admin = i[L"is_admin"].get<bool>();
                    user.last_routines_update_ts = i[L"last_routines_update_ts"].get_value<uint64_t>();
                    user.preferences.day_view_prefer_timeline =
                        i[L"preferences"][L"day_view_prefer_timeline"].get<bool>();
                    {
                        auto const& theme_prefer_str = i[L"preferences"][L"theme"].get<std::wstring>();
                        if (theme_prefer_str == L"system") {
                            user.preferences.theme = ThemePreference::FollowSystem;
                        }
                        else if (theme_prefer_str == L"light") {
                            user.preferences.theme = ThemePreference::Light;
                        }
                        else if (theme_prefer_str == L"dark") {
                            user.preferences.theme = ThemePreference::Dark;
                        }
                        else {
                            return false;
                        }
                    }
                    user.preferences.verify_identity_before_login =
                        i[L"preferences"][L"verify_identity_before_login"].get<bool>();
                    users.push_back(std::move(user));
                }
                return true;
            }
            catch (...) {
                return false;
            }
        };
        if (!parse_index_jo_fn()) {
            return RoutineArrangerResultErrorKind::StorageCorrupted;
        }
        // NOTE: Routines are loaded in ascending order
        auto parse_routines_jo_fn = [&] {
            try {
                auto parse_routine_fn = [](json::JsonObject& jo) -> RoutineDesc {
                    RoutineDesc routine;
                    routine.id = util::winrt::to_guid(jo[L"id"].get<std::wstring>());
                    routine.start_secs_since_epoch = jo[L"start_secs_since_epoch"].get_value<uint64_t>();
                    routine.duration_secs = jo[L"duration_secs"].get_value<uint64_t>();
                    routine.is_ghost = false;
                    routine.name = jo[L"name"].get<std::wstring>();
                    routine.description = jo[L"description"].get<std::wstring>();
                    routine.color = jo[L"color"].get_value<uint32_t>();
                    routine.end_trigger_kind =
                        static_cast<RoutineEndTriggerKind>(jo[L"end_trigger_kind"].get_value<uint32_t>());
                    routine.is_ended = jo[L"is_ended"].get<bool>();
                    auto& template_options = jo[L"template_options"];
                    if (template_options.is_null()) {
                        //routine.template_kind = RoutineTemplateKind::Normal;
                        routine.template_options = nullptr;
                    }
                    else {  // Assume template_options is JsonObject
                        auto& data = template_options.get<json::JsonObject>();
                        if (data.contains(L"source_routine")) {
                            RoutineDescTemplate_Derived derived;
                            //routine.template_kind = RoutineTemplateKind::Derived;
                            derived.source_routine =
                                util::winrt::to_guid(data[L"source_routine"].get<std::wstring>());
                            routine.template_options = std::move(derived);
                        }
                        else {  // Assume type is Repeating
                            RoutineDescTemplate_Repeating repeating;
                            //routine.template_kind = RoutineTemplateKind::Repeating;
                            repeating.repeat_days_cycle = data[L"repeat_days_cycle"].get_value<uint32_t>();
                            repeating.repeat_cycles = data[L"repeat_cycles"].get_value<uint32_t>();
                            auto& flags = data[L"repeat_days_flags"].get<json::JsonArray>();
                            if (flags.size() != repeating.repeat_days_cycle) {
                                throw std::exception("Repeat days and flags mismatch");
                            }
                            repeating.repeat_days_flags.resize(repeating.repeat_days_cycle);
                            std::transform(
                                flags.begin(), flags.end(),
                                repeating.repeat_days_flags.begin(),
                                [](json::JsonValue const& v) {
                                    return static_cast<bool>(v.get_value<uint32_t>());
                                }
                            );
                            routine.template_options = std::move(repeating);
                        }
                    }
                    return routine;
                };

                for (auto& i : routines_jo[L"public"].get<json::JsonArray>()) {
                    RoutineDesc routine = parse_routine_fn(i.get<json::JsonObject>());
                    if (std::holds_alternative<RoutineDescTemplate_Derived>(routine.template_options)) {
                        // Public derived routines are forbidden
                        return false;
                    }
                    ordered_insert(routines_public, std::move(routine), pred_routine_desc_less_than);
                }
                for (auto& i : routines_jo[L"personal"].get<json::JsonObject>()) {
                    ::winrt::guid user_id = util::winrt::to_guid(i.first);
                    if (std::find_if(
                        users.begin(), users.end(),
                        [&](UserDesc const& i) { return i.id == user_id; }
                    ) == users.end())
                    {
                        // User does not exist (may have been deleted); drop these
                        // routines without owners
                        continue;
                    }

                    std::vector<RoutineDesc> routines;
                    for (auto& item : i.second.get<json::JsonArray>()) {
                        ordered_insert(
                            routines,
                            parse_routine_fn(item.get<json::JsonObject>()),
                            pred_routine_desc_less_than
                        );
                    }

                    routines_personal.emplace(user_id, std::move(routines));
                }
                return true;
            }
            catch (...) {
                return false;
            }
        };
        if (!parse_routines_jo_fn()) {
            return RoutineArrangerResultErrorKind::StorageCorrupted;
        }

        // Finally, update members
        this->try_flush_storage();
        m_storage_path = path;
        m_file_lock = std::move(file_lock);
        m_index_cfg_need_flush = index_cfg_need_flush;
        m_routines_cfg_need_flush = routines_cfg_need_flush;
        m_users = std::move(users);
        m_routines_public = std::move(routines_public);
        m_routines_personal = std::move(routines_personal);

        return RoutineArrangerResultErrorKind::Ok;
    }
    bool CoreAppModel::try_flush_storage(void) {
        if (m_storage_path == L"") {
            // Syncing without storage should always succeed
            return true;
        }

        auto write_data_to_file_fn = [this](const wchar_t* cfg_name, json::JsonValue const& jv) {
            std::wstring cfg_path = m_storage_path + L"/" + cfg_name;
            if (util::fs::path_exists(cfg_path.c_str())) {
                // Backup old file
                std::wstring cfg_bak_path = cfg_path + L".bak";
                util::fs::delete_file(cfg_bak_path.c_str());
                util::fs::rename_path(cfg_path.c_str(), cfg_bak_path.c_str());
            }
            std::ofstream f{ cfg_path, std::ios::out | std::ios::binary | std::ios::trunc, _SH_DENYRW };
            if (!f.is_open()) {
                return false;
            }
            auto data = jv.serialize_into_utf8();
            std::copy(data.begin(), data.end(), std::ostreambuf_iterator(f));
            f.flush();
            return static_cast<bool>(f);
        };

        if (m_index_cfg_need_flush) {
            json::JsonObject jo;
            jo[L"version"] = 1;
            {
                json::JsonArray ja_users;
                for (auto const& i : m_users) {
                    json::JsonObject jo_user;
                    jo_user[L"id"] = util::winrt::to_wstring(i.id);
                    jo_user[L"name"] = i.name;
                    jo_user[L"nickname"] = i.nickname;
                    jo_user[L"is_admin"] = i.is_admin;
                    jo_user[L"last_routines_update_ts"] = i.last_routines_update_ts;
                    {
                        json::JsonObject jo_prefers;
                        std::wstring theme_str;
                        jo_prefers[L"day_view_prefer_timeline"] = i.preferences.day_view_prefer_timeline;
                        switch (i.preferences.theme) {
                        case ThemePreference::FollowSystem:
                            theme_str = L"system";
                            break;
                        case ThemePreference::Light:
                            theme_str = L"light";
                            break;
                        case ThemePreference::Dark:
                            theme_str = L"dark";
                            break;
                        default:
                            throw std::exception("Integrity check for user.preferences.theme has failed");
                        }
                        jo_prefers[L"theme"] = std::move(theme_str);
                        jo_prefers[L"verify_identity_before_login"] = i.preferences.verify_identity_before_login;
                        jo_user[L"preferences"] = std::move(jo_prefers);
                    }
                    ja_users.push_back(std::move(jo_user));
                }
                jo[L"users"] = std::move(ja_users);
            }
            if (!write_data_to_file_fn(L"index.cfg", json::JsonValue{std::move(jo)})) {
                return false;
            }
            m_index_cfg_need_flush = false;
        }
        if (m_routines_cfg_need_flush) {
            json::JsonObject jo;
            auto gen_routine_jo_fn = [](RoutineDesc const& data) {
                json::JsonObject jo;
                jo[L"id"] = util::winrt::to_wstring(data.id);
                jo[L"start_secs_since_epoch"] = data.start_secs_since_epoch;
                jo[L"duration_secs"] = data.duration_secs;
                jo[L"name"] = data.name;
                jo[L"description"] = data.description;
                jo[L"color"] = data.color;
                jo[L"end_trigger_kind"] = static_cast<uint32_t>(data.end_trigger_kind);
                jo[L"is_ended"] = data.is_ended;
                if (auto p = std::get_if<std::nullptr_t>(&data.template_options)) {
                    jo[L"template_options"] = nullptr;
                }
                else if (auto p = std::get_if<RoutineDescTemplate_Repeating>(&data.template_options)) {
                    json::JsonObject jo_repeating;
                    jo_repeating[L"repeat_days_cycle"] = p->repeat_days_cycle;
                    jo_repeating[L"repeat_cycles"] = p->repeat_cycles;
                    {
                        json::JsonArray ja_flags;
                        for (auto const& i : p->repeat_days_flags) {
                            ja_flags.push_back(static_cast<uint32_t>(i));
                        }
                        jo_repeating[L"repeat_days_flags"] = std::move(ja_flags);
                    }
                    jo[L"template_options"] = std::move(jo_repeating);
                }
                else if (auto p = std::get_if<RoutineDescTemplate_Derived>(&data.template_options)) {
                    json::JsonObject jo_derived;
                    jo_derived[L"source_routine"] = util::winrt::to_wstring(p->source_routine);
                    jo[L"template_options"] = std::move(jo_derived);
                }
                else {
                    throw std::exception("Integrity check for routine.template_options has failed");
                }
                return jo;
            };
            {
                json::JsonArray ja_public;
                for (auto const& i : m_routines_public) {
                    if (i.is_ghost) {
                        throw std::exception("Integrity check for routine.is_ghost has failed");
                    }
                    ja_public.push_back(gen_routine_jo_fn(i));
                }
                jo[L"public"] = std::move(ja_public);
            }
            {
                json::JsonObject jo_personal;
                for (auto const& i : m_routines_personal) {
                    json::JsonArray ja_routines;
                    for (auto const& i : i.second) {
                        if (i.is_ghost) {
                            continue;
                        }
                        ja_routines.push_back(gen_routine_jo_fn(i));
                    }
                    jo_personal[util::winrt::to_wstring(i.first)] = std::move(ja_routines);
                }
                jo[L"personal"] = std::move(jo_personal);
            }
            if (!write_data_to_file_fn(L"routines.cfg", json::JsonValue{std::move(jo)})) {
                return false;
            }
            m_routines_cfg_need_flush = false;
        }

        return true;
    }
    const wchar_t* CoreAppModel::get_current_storage_path(void) {
        return m_storage_path.c_str();
    }
    bool CoreAppModel::create_user(const wchar_t* name, const wchar_t* nickname, bool is_admin) {
        if (name == nullptr) {
            name = L"";
        }
        if (nickname == nullptr) {
            nickname = L"";
        }
        if (*name == L'\0' || *nickname == L'\0') {
            return false;
        }
        for (auto ch : std::wstring_view{ name }) {
            bool valid = ch < 0x80 && std::iswalnum(ch);
            if (!valid) {
                return false;
            }
        }
        // User names should not collide
        for (auto const& i : m_users) {
            if (name == i.name) {
                return false;
            }
        }

        auto user_id = util::winrt::gen_random_guid();
        UserDesc user;
        user.id = user_id;
        user.name = name;
        user.nickname = nickname;
        user.is_admin = is_admin;
        user.last_routines_update_ts = 0;
        user.preferences.day_view_prefer_timeline = true;
        user.preferences.theme = ThemePreference::FollowSystem;
        user.preferences.verify_identity_before_login = false;
        m_users.push_back(std::move(user));

        m_index_cfg_need_flush = true;

        m_routines_personal.emplace(user_id, std::vector<RoutineDesc>{});
        m_routines_cfg_need_flush = true;

        return true;
    }
    bool CoreAppModel::try_lookup_user(::winrt::guid user_id, UserDesc& desc) {
        for (auto const& i : m_users) {
            if (user_id == i.id) {
                desc = i;
                return true;
            }
        }
        return false;
    }
    bool CoreAppModel::try_update_user(UserDesc const& desc) {
        for (auto& i : m_users) {
            if (desc.id == i.id) {
                i.nickname = desc.nickname;
                i.is_admin = desc.is_admin;
                i.last_routines_update_ts = desc.last_routines_update_ts;
                i.preferences = desc.preferences;

                m_index_cfg_need_flush = true;
                return true;
            }
        }
        return false;
    }
    bool CoreAppModel::try_remove_user(::winrt::guid user_id) {
        for (auto it = m_users.begin(); it != m_users.end(); it++) {
            if (it->id == user_id) {
                m_users.erase(it);
                for (auto it2 = m_routines_personal.begin(); it2 != m_routines_personal.end(); it2++) {
                    if (it2->first == user_id) {
                        m_routines_personal.erase(it2);
                        m_routines_cfg_need_flush = true;
                        break;
                    }
                }
                m_index_cfg_need_flush = true;
                return true;
            }
        }
        return false;
    }
    bool CoreAppModel::try_lookup_routine(::winrt::guid user_id, ::winrt::guid routine_id, RoutineDesc* routine) {
        // Search public routines
        if (user_id == ::winrt::guid{ GUID{} }) {
            for (auto const& i : m_routines_public) {
                if (i.id == routine_id) {
                    if (routine != nullptr) {
                        *routine = i;
                    }
                    return true;
                }
            }
            return false;
        }
        // Search personal routines
        for (auto const& i : m_routines_personal) {
            if (i.first == user_id) {
                for (auto const& j : i.second) {
                    if (j.id == routine_id) {
                        if (routine != nullptr) {
                            *routine = j;
                        }
                        return true;
                    }
                }
                break;
            }
        }
        return false;
    }
    bool CoreAppModel::try_get_routines_from_user_view(
        ::winrt::guid user_id,
        uint64_t secs_since_epoch_start,
        uint64_t secs_since_epoch_end,
        std::vector<RoutineDesc>& routines
    ) {
        // Generate ghosts from public routines (preserving id)
        auto personal_it = m_routines_personal.begin();
        for (; personal_it != m_routines_personal.end(); personal_it++) {
            if (personal_it->first == user_id) {
                break;
            }
        }
        // TODO: Insert empty list if user-routines pair does not exist
        if (personal_it == m_routines_personal.end()) {
            return false;
        }
        auto& user_routines = personal_it->second;
        for (auto const& i : m_routines_public) {
            if (i.start_secs_since_epoch >= secs_since_epoch_end) {
                break;
            }
            auto it = std::find_if(
                user_routines.begin(), user_routines.end(),
                [&](RoutineDesc const& v) {
                    return v.id == i.id;
                }
            );
            if (it != user_routines.end()) {
                continue;
            }
            RoutineDesc copied_routine = i;
            copied_routine.is_ghost = true;
            ordered_insert(user_routines, copied_routine, pred_routine_desc_less_than);
            //m_routines_cfg_need_flush = true;
        }
        // Generate ghosts from personal routines (regardless of is_ghost)
        std::vector<RoutineDesc> user_repeating_routines;
        for (auto const& i : user_routines) {
            if (i.start_secs_since_epoch >= secs_since_epoch_end) {
                break;
            }
            if (std::holds_alternative<RoutineDescTemplate_Repeating>(i.template_options)) {
                user_repeating_routines.push_back(i);
            }
        }
        for (auto const& i : user_repeating_routines) {
            auto& repeating = std::get<RoutineDescTemplate_Repeating>(i.template_options);
            auto repeat_cycles = static_cast<size_t>(repeating.repeat_cycles);
            if (repeat_cycles == 0) {
                repeat_cycles = std::numeric_limits<size_t>::max();
            }
            for (size_t times = 0; times < repeat_cycles; times++) {
                uint64_t start_secs_offset = times * SECS_PER_DAY * repeating.repeat_days_cycle;
                if (i.start_secs_since_epoch + start_secs_offset >= secs_since_epoch_end) {
                    break;
                }
                RoutineDesc copied_routine = i;
                copied_routine.is_ghost = true;
                copied_routine.template_options = RoutineDescTemplate_Derived{ i.id };
                // TODO: Cache last updated time when routines remain unchanged
                //       to avoid redundant calculations and improve performance
                // Avoid collision with first day if flag is set
                for (size_t idx = times == 0 ? 1 : 0; idx < repeating.repeat_days_cycle; idx++) {
                    if (!repeating.repeat_days_flags[idx]) {
                        continue;
                    }
                    copied_routine.start_secs_since_epoch =
                        i.start_secs_since_epoch + start_secs_offset + idx * SECS_PER_DAY;
                    copied_routine.id = util::winrt::gen_random_guid();
                    //copied_routine.template_kind = RoutineTemplateKind::Derived;
                    /* NOTE: Unoptimized version
                    auto it = std::find_if(
                        user_routines.begin(), user_routines.end(),
                        [&](RoutineDesc const& v) {
                            return pred_derived_routines_in_same_day(v, copied_routine);
                        }
                    );
                    if (it != user_routines.end()) {
                        continue;
                    }
                    */
                    auto try_find_derived_routine_in_same_day = [&] {
                        auto find_start_secs =
                            (copied_routine.start_secs_since_epoch / SECS_PER_DAY) * SECS_PER_DAY;
                        auto find_end_secs = find_start_secs + SECS_PER_DAY;
                        auto it = std::lower_bound(
                            user_routines.begin(), user_routines.end(),
                            find_start_secs,
                            [](RoutineDesc const& rd, auto const& v) {
                                return rd.start_secs_since_epoch < v;
                            }
                        );
                        auto it_end = user_routines.end();
                        for (; it != it_end; it++) {
                            if (it->start_secs_since_epoch >= find_end_secs) {
                                break;
                            }
                            auto derived = std::get_if<RoutineDescTemplate_Derived>(&it->template_options);
                            if (!derived) {
                                continue;
                            }
                            if (derived->source_routine != i.id) {
                                continue;
                            }
                            return true;
                        }
                        return false;
                    };
                    if (try_find_derived_routine_in_same_day()) {
                        continue;
                    }
                    ordered_insert(user_routines, copied_routine, pred_routine_desc_less_than);
                    //m_routines_cfg_need_flush = true;
                }
            }
        }
        // Collect all routines in ascending order and return
        routines.clear();
        auto user_routines_it = std::lower_bound(
            user_routines.begin(), user_routines.end(),
            secs_since_epoch_start,
            [](RoutineDesc const& rd, uint64_t const& start_secs) {
                return rd.start_secs_since_epoch < start_secs;
            }
        );
        for (; user_routines_it != user_routines.end(); user_routines_it++) {
            if (user_routines_it->start_secs_since_epoch >= secs_since_epoch_end) {
                break;
            }
            routines.push_back(*user_routines_it);
        }
        return true;
    }
    bool CoreAppModel::try_update_routine_from_user_view(::winrt::guid user_id, RoutineDesc const& routine) {
        for (auto it = m_routines_personal.begin(); it != m_routines_personal.end(); it++) {
            if (it->first == user_id) {
                auto it2 = std::find_if(
                    it->second.begin(), it->second.end(),
                    [&](RoutineDesc const& a) {
                        return a.id == routine.id;
                    }
                );
                if (it2 != it->second.end()) {
                    it->second.erase(it2);
                    // TODO: Remove all *related* ghost routines if necessary
                    it->second.erase(
                        std::remove_if(it->second.begin(), it->second.end(), pred_routine_is_ghost),
                        it->second.end()
                    );
                }
                RoutineDesc copied_routine = routine;
                copied_routine.is_ghost = false;
                ordered_insert(it->second, copied_routine, pred_routine_desc_less_than);
                m_routines_cfg_need_flush = true;
                return true;
            }
        }
        return false;
    }
    bool CoreAppModel::try_remove_routine_from_user_view(::winrt::guid user_id, ::winrt::guid routine_id) {
        for (auto it = m_routines_personal.begin(); it != m_routines_personal.end(); it++) {
            if (it->first == user_id) {
                for (auto it2 = it->second.begin(); it2 != it->second.end(); it2++) {
                    if (it2->id == routine_id) {
                        // TODO: Users are not allowed to delete a routine if it
                        //       comes directly from public ones
                        it->second.erase(it2);
                        // TODO: Remove all *related* ghost routines if necessary
                        it->second.erase(
                            std::remove_if(it->second.begin(), it->second.end(), pred_routine_is_ghost),
                            it->second.end()
                        );
                        m_routines_cfg_need_flush = true;
                        return true;
                    }
                }
                break;
            }
        }
        return false;
    }
    void CoreAppModel::update_public_routine(RoutineDesc const& routine) {
        auto it = std::find_if(
            m_routines_public.begin(), m_routines_public.end(),
            [&](RoutineDesc const& i) {
                return i.id == routine.id;
            }
        );
        if (it != m_routines_public.end()) {
            m_routines_public.erase(it);
            // TODO: Remove all *related* ghost routines if necessary
            for (auto& i : m_routines_personal) {
                i.second.erase(
                    std::remove_if(i.second.begin(), i.second.end(), pred_routine_is_ghost),
                    i.second.end()
                );
            }
        }
        RoutineDesc copied_routine = routine;
        copied_routine.is_ghost = false;
        // Public routine cannot be ended ones
        copied_routine.is_ended = false;
        // Public routine cannot be derived ones
        if (std::holds_alternative<RoutineDescTemplate_Derived>(copied_routine.template_options)) {
            copied_routine.template_options = nullptr;
        }
        ordered_insert(m_routines_public, copied_routine, pred_routine_desc_less_than);
        m_routines_cfg_need_flush = true;
    }
    bool CoreAppModel::try_remove_public_routine(::winrt::guid routine_id) {
        for (auto it = m_routines_public.begin(); it != m_routines_public.end(); it++) {
            if (it->id == routine_id) {
                m_routines_public.erase(it);
                // TODO: Remove all *related* ghost routines if necessary
                for (auto& i : m_routines_personal) {
                    i.second.erase(
                        std::remove_if(i.second.begin(), i.second.end(), pred_routine_is_ghost),
                        i.second.end()
                    );
                }
                m_routines_cfg_need_flush = true;
                return true;
            }
        }
        return false;
    }
}
