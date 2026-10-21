#ifndef VARIABLES_H
#define VARIABLES_H

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Shell variables. A variable lives either in the process environment (exported, so children inherit it) or
// in a private table (plain shell variable). `export` moves a variable from the table to the environment.
class Variables {
public:
    std::optional<std::string> get(const std::string& name) const;
    // Sets a value; an already exported variable stays exported.
    void set(const std::string& name, const std::string& value);
    // export NAME: marks an existing shell variable for export. Does nothing if NAME is not set.
    void exportExisting(const std::string& name);
    // export NAME=value
    void exportValue(const std::string& name, const std::string& value);
    void unset(const std::string& name);
    // Exported variables, sorted by name.
    std::vector<std::pair<std::string, std::string>> exported() const;

    static bool isValidName(const std::string& name);

private:
    std::map<std::string, std::string> locals_;
};

#endif // VARIABLES_H
