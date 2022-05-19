#pragma once

namespace RoutineArranger {
    // Delegate constructor
    template<typename T, typename U = typename T::element_type, typename... Types>
    T make(Types&&... args) {
        return std::make_shared<U>(std::forward<Types>(args)...);
    }
}
