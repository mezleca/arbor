#include <arbor/style/variables.hpp>

using namespace ui;

void StyleVariableStore::set(std::string_view key, StyleValue value) {
    if (StyleValue* existing = find(key)) {
        *existing = std::move(value);
        return;
    }

    m_vars.emplace(key, std::move(value));
}

StyleValue* StyleVariableStore::find(std::string_view key) {
    auto value_it = m_vars.find(key);
    return value_it == m_vars.end() ? nullptr : &value_it->second;
}

const StyleValue* StyleVariableStore::find(std::string_view key) const {
    return const_cast<StyleVariableStore*>(this)->find(key);
}
