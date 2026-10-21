#include "variables.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

extern char** environ;

std::optional<std::string> Variables::get(const std::string& name) const {
    auto it = locals_.find(name);
    if (it != locals_.end()) return it->second;
    const char* v = std::getenv(name.c_str());
    if (v != nullptr) return std::string(v);
    return std::nullopt;
}

void Variables::set(const std::string& name, const std::string& value) {
    if (std::getenv(name.c_str()) != nullptr) {
        setenv(name.c_str(), value.c_str(), 1);
    } else {
        locals_[name] = value;
    }
}

void Variables::exportExisting(const std::string& name) {
    auto it = locals_.find(name);
    if (it == locals_.end()) return;  // already exported, or not set at all
    setenv(name.c_str(), it->second.c_str(), 1);
    locals_.erase(it);
}

void Variables::exportValue(const std::string& name, const std::string& value) {
    locals_.erase(name);
    setenv(name.c_str(), value.c_str(), 1);
}

void Variables::unset(const std::string& name) {
    locals_.erase(name);
    unsetenv(name.c_str());
}

std::vector<std::pair<std::string, std::string>> Variables::exported() const {
    std::map<std::string, std::string> sorted;
    for (char** e = environ; e != nullptr && *e != nullptr; ++e) {
        const char* eq = std::strchr(*e, '=');
        if (eq == nullptr) continue;
        sorted[std::string(*e, static_cast<size_t>(eq - *e))] = eq + 1;
    }
    return {sorted.begin(), sorted.end()};
}

bool Variables::isValidName(const std::string& name) {
    if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_')) return false;
    for (char c : name)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    return true;
}
